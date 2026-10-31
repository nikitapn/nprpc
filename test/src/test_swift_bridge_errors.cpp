#include "../../nprpc_swift/Sources/CNprpc/nprpc_bridge_error.hpp"
#include <nprpc/nprpc.hpp>
#include <gtest/gtest.h>
#include <thread>
#include <vector>

using namespace nprpc_swift::detail;

namespace {
struct CompletionResult {
    std::atomic<int> calls{0};
    bool success = false;
    NprpcBridgeErrorKind kind{};
    int native_code = 0;
    std::string category;
    std::string message;
    void* response = nullptr;

    static void receive(void* context, const NprpcBridgeError* error, void* response) {
        auto& result = *static_cast<CompletionResult*>(context);
        ++result.calls;
        result.success = !error;
        result.response = response;
        if (error) {
            result.kind = error->kind;
            result.native_code = error->native_code;
            result.category = error->native_category;
            result.message = error->message;
        }
    }
};

class TestObject : public nprpc::Object {};
}

TEST(SwiftBridgeErrors, InvalidArgumentsCompleteInlineExactlyOnce) {
    for (bool response : {false, true}) {
        CompletionResult result;
        nprpc_object_send_async_result(nullptr, nullptr, &result, CompletionResult::receive, 1, response);
        EXPECT_EQ(result.calls, 1);
        EXPECT_EQ(result.kind, NPRPC_BRIDGE_INVALID_ARGUMENT);
        EXPECT_FALSE(result.message.empty());
        EXPECT_EQ(result.response, nullptr);
    }
}

TEST(SwiftBridgeErrors, MissingRuntimeIsReportedWithoutDereferencingIt) {
    TestObject object;
    nprpc::flat_buffer buffer;
    CompletionResult result;
    nprpc_object_send_async_result(&object, &buffer, &result, CompletionResult::receive, 1, true);
    EXPECT_EQ(result.calls, 1);
    EXPECT_EQ(result.kind, NPRPC_BRIDGE_NOT_INITIALIZED);
}

TEST(SwiftBridgeErrors, LegacyInitiationFailureDoesNotCallBack) {
    int calls = 0;
    auto callback = +[](void* ctx, int, const char*) { ++*static_cast<int*>(ctx); };
    EXPECT_EQ(nprpc_object_send_async(nullptr, nullptr, &calls, callback, 1), -1);
    auto receive = +[](void* ctx, int, const char*, void*) { ++*static_cast<int*>(ctx); };
    EXPECT_EQ(nprpc_object_send_async_receive(nullptr, nullptr, &calls, receive, 1), -1);
    EXPECT_EQ(calls, 0);
}

TEST(SwiftBridgeErrors, TransportErrorsPreserveNativeDetails) {
    const boost::system::error_code errors[] = {
        boost::asio::error::timed_out, boost::asio::error::operation_aborted,
        boost::asio::error::connection_refused,
        boost::system::errc::make_error_code(boost::system::errc::timed_out),
        boost::system::errc::make_error_code(boost::system::errc::operation_canceled)};
    const NprpcBridgeErrorKind kinds[] = {
        NPRPC_BRIDGE_TIMEOUT, NPRPC_BRIDGE_CANCELLED, NPRPC_BRIDGE_CONNECTION_FAILURE,
        NPRPC_BRIDGE_TIMEOUT, NPRPC_BRIDGE_CANCELLED};
    for (size_t i = 0; i < std::size(errors); ++i) {
        CompletionResult result;
        AsyncCompletion completion(&result, true);
        completion.callback = CompletionResult::receive;
        nprpc::flat_buffer buffer;
        completion.complete(errors[i], buffer);
        EXPECT_EQ(result.calls, 1);
        EXPECT_EQ(result.kind, kinds[i]);
        EXPECT_EQ(result.native_code, errors[i].value());
        EXPECT_EQ(result.category, errors[i].category().name());
        EXPECT_EQ(result.message, errors[i].message());
        EXPECT_EQ(result.response, nullptr);
    }
}

TEST(SwiftBridgeErrors, ExceptionBoundaryPreservesCommunicationDiagnostics) {
    auto result = capture([]() -> BridgeResult<void> {
        throw nprpc::ExceptionCommFailure("TCP was compiled out");
    });
    ASSERT_FALSE(result);
    EXPECT_EQ(result.error().kind, NPRPC_BRIDGE_CONNECTION_FAILURE);
    EXPECT_STREQ(result.error().message.data(), "TCP was compiled out");
}

TEST(SwiftBridgeErrors, ExceptionBoundaryHandlesSystemErrorsAllocationAndUnknownExceptions) {
    auto timeout = capture([]() -> BridgeResult<void> {
        throw std::system_error(std::make_error_code(std::errc::timed_out));
    });
    ASSERT_FALSE(timeout);
    EXPECT_EQ(timeout.error().kind, NPRPC_BRIDGE_TIMEOUT);
    EXPECT_EQ(timeout.error().native_code, static_cast<int>(std::errc::timed_out));
    auto allocation = capture([]() -> BridgeResult<void> { throw std::bad_alloc(); });
    ASSERT_FALSE(allocation);
    EXPECT_EQ(allocation.error().kind, NPRPC_BRIDGE_OUT_OF_MEMORY);
    auto unknown = capture([]() -> BridgeResult<void> { throw 42; });
    ASSERT_FALSE(unknown);
    EXPECT_EQ(unknown.error().kind, NPRPC_BRIDGE_INTERNAL_ERROR);
}

TEST(SwiftBridgeErrors, CompletionTransfersResponseOnlyOnceEvenIfStartupLaterFails) {
    CompletionResult result;
    AsyncCompletion completion(&result, true);
    completion.callback = CompletionResult::receive;
    nprpc::flat_buffer buffer;
    std::memcpy(buffer.prepare(4).data(), "test", 4);
    buffer.commit(4);
    completion.complete({}, buffer);
    completion.fail({NPRPC_BRIDGE_INTERNAL_ERROR, "late initiation exception"});
    completion.complete({}, buffer);
    EXPECT_EQ(result.calls, 1);
    ASSERT_TRUE(result.success);
    ASSERT_NE(result.response, nullptr);
    auto* response = static_cast<nprpc::flat_buffer*>(result.response);
    EXPECT_EQ(response->size(), 4);
    EXPECT_EQ(std::memcmp(response->data_ptr(), "test", 4), 0);
    nprpc_flatbuffer_release(response);
}

TEST(SwiftBridgeErrors, LegacyAbandonSuppressesLateCompletion) {
    CompletionResult result;
    AsyncCompletion completion(&result, true);
    completion.callback = CompletionResult::receive;
    EXPECT_FALSE(completion.abandon());
    nprpc::flat_buffer buffer;
    completion.complete({}, buffer);
    EXPECT_EQ(result.calls, 0);
}

TEST(SwiftBridgeErrors, ConcurrentFailuresConsumeContextExactlyOnce) {
    CompletionResult result;
    AsyncCompletion completion(&result, true);
    completion.callback = CompletionResult::receive;
    std::vector<std::thread> threads;
    for (int i = 0; i < 8; ++i)
        threads.emplace_back([&] { completion.fail({NPRPC_BRIDGE_CANCELLED, "cancelled"}); });
    for (auto& thread : threads) thread.join();
    EXPECT_EQ(result.calls, 1);
    EXPECT_EQ(result.kind, NPRPC_BRIDGE_CANCELLED);
}
