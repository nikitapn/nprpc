// Internal error translation; keep std::expected and NPRPC internals out of Swift headers.
#pragma once

#include "include/nprpc_bridge.hpp"
#include <nprpc_base.hpp>
#include <boost/asio/error.hpp>
#include <boost/system/system_error.hpp>
#include <algorithm>
#include <array>
#include <atomic>
#include <cstring>
#include <expected>
#include <new>
#include <system_error>

namespace nprpc_swift::detail {

struct BridgeError {
    NprpcBridgeErrorKind kind;
    int32_t native_code = 0;
    std::array<char, 64> native_category{};
    std::array<char, 512> message{};

    static void copy(char* out, size_t capacity, const char* text) noexcept {
        if (!text) return;
        const auto size = std::min(capacity - 1, std::strlen(text));
        std::memcpy(out, text, size);
        out[size] = '\0';
    }

    BridgeError(NprpcBridgeErrorKind code, const char* text,
                int32_t native = 0, const char* category = "") noexcept
        : kind(code), native_code(native) {
        copy(message.data(), message.size(), text);
        copy(native_category.data(), native_category.size(), category);
    }

    NprpcBridgeError view() const noexcept {
        return {kind, native_code, native_category.data(), message.data()};
    }
};

template<class T> using BridgeResult = std::expected<T, BridgeError>;

inline NprpcBridgeErrorKind transport_error_kind(const boost::system::error_code& ec) noexcept {
    auto kind = NPRPC_BRIDGE_CONNECTION_FAILURE;
    if (ec == boost::asio::error::timed_out || ec == boost::system::errc::timed_out)
        kind = NPRPC_BRIDGE_TIMEOUT;
    else if (ec == boost::asio::error::operation_aborted || ec == boost::system::errc::operation_canceled)
        kind = NPRPC_BRIDGE_CANCELLED;
    return kind;
}

inline BridgeError transport_error(const boost::system::error_code& ec) {
    return {transport_error_kind(ec), ec.message().c_str(), ec.value(), ec.category().name()};
}

// Failure translation itself allocates no memory, including the bad_alloc path.
// Functions passed here return BridgeResult<T>; expected failures stay values.
template<class F>
auto capture(F&& fn) noexcept -> decltype(fn()) {
    try {
        return fn();
    } catch (const nprpc::ExceptionCommFailure& e) {
        return std::unexpected(BridgeError(NPRPC_BRIDGE_CONNECTION_FAILURE, e.what.c_str()));
    } catch (const nprpc::ExceptionTimeout& e) {
        return std::unexpected(BridgeError(NPRPC_BRIDGE_TIMEOUT, e.what()));
    } catch (const nprpc::OperationCancelled& e) {
        return std::unexpected(BridgeError(NPRPC_BRIDGE_CANCELLED, e.what()));
    } catch (const boost::system::system_error& e) {
        const auto& ec = e.code();
        return std::unexpected(BridgeError(transport_error_kind(ec), e.what(), ec.value(), ec.category().name()));
    } catch (const std::system_error& e) {
        const auto& ec = e.code();
        auto kind = NPRPC_BRIDGE_CONNECTION_FAILURE;
        if (ec == std::errc::timed_out) kind = NPRPC_BRIDGE_TIMEOUT;
        else if (ec == std::errc::operation_canceled) kind = NPRPC_BRIDGE_CANCELLED;
        return std::unexpected(BridgeError(kind, e.what(), ec.value(), ec.category().name()));
    } catch (const std::bad_alloc&) {
        return std::unexpected(BridgeError(NPRPC_BRIDGE_OUT_OF_MEMORY, "Out of memory"));
    } catch (const std::invalid_argument& e) {
        return std::unexpected(BridgeError(NPRPC_BRIDGE_INVALID_ARGUMENT, e.what()));
    } catch (const std::exception& e) {
        return std::unexpected(BridgeError(NPRPC_BRIDGE_INTERNAL_ERROR, e.what()));
    } catch (...) {
        return std::unexpected(BridgeError(NPRPC_BRIDGE_INTERNAL_ERROR, "Unknown C++ exception"));
    }
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
