// Internal error translation; keep std::expected and NPRPC internals out of Swift headers.
#pragma once

#include "include/nprpc_bridge.hpp"
#include <nprpc/impl/result.hpp>
#include <atomic>
#include <utility>

namespace nprpc_swift::detail {

static_assert(static_cast<int32_t>(nprpc::ErrorCode::invalid_argument) == NPRPC_BRIDGE_INVALID_ARGUMENT);
static_assert(static_cast<int32_t>(nprpc::ErrorCode::no_endpoint) == NPRPC_BRIDGE_NO_ENDPOINT);
static_assert(static_cast<int32_t>(nprpc::ErrorCode::not_initialized) == NPRPC_BRIDGE_NOT_INITIALIZED);
static_assert(static_cast<int32_t>(nprpc::ErrorCode::timeout) == NPRPC_BRIDGE_TIMEOUT);
static_assert(static_cast<int32_t>(nprpc::ErrorCode::cancelled) == NPRPC_BRIDGE_CANCELLED);
static_assert(static_cast<int32_t>(nprpc::ErrorCode::connection_failure) == NPRPC_BRIDGE_CONNECTION_FAILURE);
static_assert(static_cast<int32_t>(nprpc::ErrorCode::out_of_memory) == NPRPC_BRIDGE_OUT_OF_MEMORY);
static_assert(static_cast<int32_t>(nprpc::ErrorCode::internal_error) == NPRPC_BRIDGE_INTERNAL_ERROR);
static_assert(static_cast<int32_t>(nprpc::ErrorCode::unsupported_transport) == NPRPC_BRIDGE_UNSUPPORTED_TRANSPORT);

struct BridgeError : nprpc::Error {
    BridgeError(NprpcBridgeErrorKind kind, const char* text,
                int32_t native = 0, const char* category = "") noexcept
        : Error(static_cast<nprpc::ErrorCode>(kind), text, native, category) {}

    BridgeError(const nprpc::Error& error) noexcept : Error(error) {}

    NprpcBridgeError view() const noexcept {
        return {static_cast<NprpcBridgeErrorKind>(code), native_code,
                native_category.data(), message.data()};
    }
};

template<class T> using BridgeResult = std::expected<T, BridgeError>;

inline BridgeError transport_error(const boost::system::error_code& ec) {
    return nprpc::impl::transport_error(ec);
}

template<class F>
auto capture(F&& fn) noexcept -> decltype(fn()) {
    return nprpc::impl::capture_result(std::forward<F>(fn));
}

// Shared between initiation and transport completion: even an inline completion
// followed by an initiation exception must not consume a Swift continuation twice.
class AsyncCompletion {
    std::atomic<bool> completed_{false};
public:
    void* context;
    swift_rpc_completion callback = nullptr;
    swift_async_callback legacy_callback = nullptr;
    swift_async_receive_callback legacy_receive = nullptr;
    bool receive_response;

    AsyncCompletion(void* ctx, bool receive) : context(ctx), receive_response(receive) {}

    // Legacy initiation failure leaves context ownership with the caller.
    // Atomically prevent a later completion, unless one already took ownership.
    bool abandon() noexcept { return completed_.exchange(true); }

    void deliver(const BridgeResult<void*>& result) noexcept {
        if (callback) {
            if (result) callback(context, nullptr, *result);
            else {
                const auto error = result.error().view();
                callback(context, &error, nullptr);
            }
        } else if (legacy_receive) {
            legacy_receive(context, result ? 0 : -3,
                           result ? nullptr : result.error().message.data(),
                           result ? *result : nullptr);
        } else {
            legacy_callback(context, result ? 0 : -3,
                            result ? nullptr : result.error().message.data());
        }
    }

    void fail(const BridgeError& error) noexcept {
        if (!completed_.exchange(true)) deliver(std::unexpected(error));
    }

    void complete(const boost::system::error_code& ec, nprpc::flat_buffer& buf) noexcept {
        if (completed_.exchange(true)) return;
        const auto result = capture([&]() -> BridgeResult<void*> {
            if (ec) return std::unexpected(transport_error(ec));
            if (!receive_response) return nullptr;
            auto* response = static_cast<nprpc::flat_buffer*>(nprpc_flatbuffer_acquire());
            *response = std::move(buf);
            return response;
        });
        deliver(result);
    }
};

} // namespace nprpc_swift::detail
