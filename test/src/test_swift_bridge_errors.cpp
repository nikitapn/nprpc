#include "../../nprpc_swift/Sources/CNprpc/nprpc_bridge_error.hpp"
#include <nprpc/nprpc.hpp>
#include <nprpc/impl/nprpc_impl.hpp>
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
    EXPECT_EQ(nprpc_object_get_session(&object), nullptr);
    EXPECT_EQ(nprpc_object_get_stream_manager(&object), nullptr);
    EXPECT_NE(nprpc_object_check_fd_passing(&object), nullptr);
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
    EXPECT_EQ(result.error().code, nprpc::ErrorCode::connection_failure);
    EXPECT_STREQ(result.error().message.data(), "TCP was compiled out");
}

TEST(SwiftBridgeErrors, ExceptionBoundaryHandlesSystemErrorsAllocationAndUnknownExceptions) {
    auto timeout = capture([]() -> BridgeResult<void> {
        throw std::system_error(std::make_error_code(std::errc::timed_out));
    });
    ASSERT_FALSE(timeout);
    EXPECT_EQ(timeout.error().code, nprpc::ErrorCode::timeout);
    EXPECT_EQ(timeout.error().native_code, static_cast<int>(std::errc::timed_out));
    auto allocation = capture([]() -> BridgeResult<void> { throw std::bad_alloc(); });
    ASSERT_FALSE(allocation);
    EXPECT_EQ(allocation.error().code, nprpc::ErrorCode::out_of_memory);
    auto unknown = capture([]() -> BridgeResult<void> { throw 42; });
    ASSERT_FALSE(unknown);
    EXPECT_EQ(unknown.error().code, nprpc::ErrorCode::internal_error);
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

namespace {
class SessionResultTests : public testing::Test {
protected:
    nprpc::Rpc* rpc = nullptr;
    nprpc::impl::RpcImpl& runtime() { return *static_cast<nprpc::impl::RpcImpl*>(rpc); }
    void SetUp() override { rpc = nprpc::RpcBuilder().build(); }
    void TearDown() override { rpc->destroy(); }
};

}

TEST_F(SessionResultTests, CachedSessionsRemainAvailableToBothApis) {
    rpc->start_thread_pool(2);
    const nprpc::EndPoint endpoint(nprpc::EndPointType::SharedMemory,
                                  nprpc::impl::g_server_listener_uuid, 0);
    auto first = runtime().get_session_result(endpoint);
    ASSERT_TRUE(first);
    auto existing = *first;
    auto result = runtime().get_session_result(endpoint);
    ASSERT_TRUE(result);
    EXPECT_EQ(*result, existing);
    EXPECT_EQ(runtime().get_session(endpoint), existing);
    TestObject object;
    const_cast<nprpc::EndPoint&>(object.get_endpoint()) = endpoint;
    EXPECT_EQ(nprpc_object_get_session(&object), existing.get());
    EXPECT_EQ(nprpc_object_get_stream_manager(&object), existing->ctx().stream_manager);
}

TEST_F(SessionResultTests, PrivateConnectionFailureIsAValueAndLegacyApiStillThrows) {
    const nprpc::EndPoint endpoint(nprpc::EndPointType::TcpPrivate, "localhost", 1234);
    auto result = runtime().get_session_result(endpoint);
    ASSERT_FALSE(result);
    EXPECT_EQ(result.error().code, nprpc::ErrorCode::connection_failure);
    EXPECT_NE(std::string(result.error().message.data()).find("Ephemeral TCP"), std::string::npos);
    EXPECT_THROW(runtime().get_session(endpoint), nprpc::ExceptionCommFailure);
}

TEST_F(SessionResultTests, UnsupportedTransportRejectionReachesSwiftCompletion) {
    // HTTP endpoints cannot be used as native RPC sessions, even in a full build.
    const nprpc::EndPoint endpoint(nprpc::EndPointType::Http, "localhost", 1234);
    auto result = runtime().get_session_result(endpoint);
    ASSERT_FALSE(result);
    EXPECT_EQ(result.error().code, nprpc::ErrorCode::unsupported_transport);
    EXPECT_THROW(runtime().get_session(endpoint), nprpc::ExceptionCommFailure);

    TestObject object;
    // Set the already selected endpoint to exercise rejection after selection.
    const_cast<nprpc::EndPoint&>(object.get_endpoint()) = endpoint;
    nprpc::flat_buffer buffer;
    std::memcpy(buffer.prepare(4).data(), "test", 4);
    buffer.commit(4);
    CompletionResult completed;
    nprpc_object_send_async_result(&object, &buffer, &completed, CompletionResult::receive, 1, true);
    EXPECT_EQ(completed.calls, 1);
    EXPECT_EQ(completed.kind, NPRPC_BRIDGE_UNSUPPORTED_TRANSPORT);
    EXPECT_EQ(buffer.size(), 4);
    EXPECT_EQ(nprpc_object_get_session(&object), nullptr);
    EXPECT_EQ(nprpc_object_get_stream_manager(&object), nullptr);
    int destroyed = 0;
    nprpc_stream_register_reader(&object, 1, &destroyed, nullptr, nullptr, nullptr,
        +[](void* ctx) { ++*static_cast<int*>(ctx); });
    EXPECT_EQ(destroyed, 1);
    nprpc_stream_set_reader_unreliable(&object, 1, true);
}

TEST_F(SessionResultTests, ConstructorFailuresBecomeValuesWithoutChangingLegacyExceptionType) {
    const nprpc::EndPoint endpoint(nprpc::EndPointType::SharedMemory, "", 0);
    auto result = runtime().get_session_result(endpoint);
    ASSERT_FALSE(result);
    EXPECT_EQ(result.error().code, nprpc::ErrorCode::connection_failure);
    EXPECT_NE(std::string(result.error().message.data()).find("missing listener name"), std::string::npos);
    EXPECT_THROW(runtime().get_session(endpoint), nprpc::Exception);
}

TEST_F(SessionResultTests, DisabledTransportsReturnUnsupportedTransport) {
    std::vector<nprpc::EndPointType> disabled;
#ifndef NPRPC_TCP_ENABLED
    disabled.push_back(nprpc::EndPointType::Tcp);
#endif
#ifndef NPRPC_WEBSOCKET_ENABLED
    disabled.push_back(nprpc::EndPointType::WebSocket);
#endif
#if !defined(NPRPC_WEBSOCKET_ENABLED) || !defined(NPRPC_SSL_ENABLED)
    disabled.push_back(nprpc::EndPointType::SecuredWebSocket);
#endif
#ifndef NPRPC_QUIC_ENABLED
    disabled.push_back(nprpc::EndPointType::Quic);
#endif
    if (disabled.empty()) GTEST_SKIP() << "All native transports are compiled in";
    for (const auto type : disabled) {
        const nprpc::EndPoint endpoint(type, "localhost", 1234);
        auto result = runtime().get_session_result(endpoint);
        ASSERT_FALSE(result);
        EXPECT_EQ(result.error().code, nprpc::ErrorCode::unsupported_transport);
        EXPECT_THROW(runtime().get_session(endpoint), nprpc::ExceptionCommFailure);
    }
}
