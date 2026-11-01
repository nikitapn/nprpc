#include "../../nprpc_swift/Sources/CNprpc/nprpc_bridge_error.hpp"
#include <nprpc/nprpc.hpp>
#include <nprpc/impl/nprpc_impl.hpp>
#include <nprpc/impl/async_result.hpp>
#include <gtest/gtest.h>
#include <future>
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
        completion.complete(std::unexpected(nprpc::impl::transport_error(errors[i])), buffer);
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

TEST(TransportResultTests, NativeErrorsReachCompletionAsValues) {
    const boost::system::error_code errors[] = {
        {}, boost::asio::error::timed_out, boost::asio::error::operation_aborted,
        boost::asio::error::connection_reset};
    for (const auto& ec : errors) {
        int calls = 0;
        nprpc::flat_buffer response;
        auto started = nprpc::impl::start_result_operation(
            [&](nprpc::Result<void> status, nprpc::flat_buffer& received) {
                ++calls;
                EXPECT_EQ(&received, &response);
                EXPECT_EQ(status.has_value(), !ec);
                if (!status) {
                    EXPECT_EQ(status.error().code, nprpc::impl::transport_error_code(ec));
                    EXPECT_EQ(status.error().native_code, ec.value());
                    EXPECT_STREQ(status.error().native_category.data(), ec.category().name());
                }
            }, [&](auto complete) { complete(ec, response); });
        EXPECT_TRUE(started);
        EXPECT_EQ(calls, 1);
    }
}

TEST(TransportResultTests, InitiationFailureSuppressesLateCallback) {
    int calls = 0;
    std::function<void(const boost::system::error_code&, nprpc::flat_buffer&)> late;
    auto started = nprpc::impl::start_result_operation(
        [&](nprpc::Result<void>, nprpc::flat_buffer&) { ++calls; },
        [&](auto complete) {
            late = complete;
            throw std::bad_alloc();
        });
    ASSERT_FALSE(started);
    EXPECT_EQ(started.error().code, nprpc::ErrorCode::out_of_memory);
    nprpc::flat_buffer response;
    late({}, response);
    EXPECT_EQ(calls, 0);
}

TEST(TransportResultTests, InlineCompletionWinsOverLaterInitiationException) {
    int calls = 0;
    nprpc::flat_buffer response;
    auto started = nprpc::impl::start_result_operation(
        [&](nprpc::Result<void> status, nprpc::flat_buffer&) { ++calls; EXPECT_TRUE(status); },
        [&](auto complete) {
            complete({}, response);
            complete({}, response);
            throw std::runtime_error("initiation failed after delivery");
        });
    EXPECT_TRUE(started);
    EXPECT_EQ(calls, 1);
}

TEST(TransportResultTests, ConcurrentCallbacksCompleteOnlyOnce) {
    std::atomic<int> calls{0};
    std::function<void(const boost::system::error_code&, nprpc::flat_buffer&)> late;
    auto started = nprpc::impl::start_result_operation(
        [&](nprpc::Result<void> status, nprpc::flat_buffer&) { ++calls; EXPECT_TRUE(status); },
        [&](auto complete) { late = complete; });
    ASSERT_TRUE(started);
    nprpc::flat_buffer response;
    std::vector<std::thread> threads;
    for (int i = 0; i < 8; ++i) threads.emplace_back([&] { late({}, response); });
    for (auto& thread : threads) thread.join();
    EXPECT_EQ(calls, 1);
}

TEST(TransportResultTests, MissingHandlerDoesNotStartTransport) {
    bool called = false;
    auto result = nprpc::impl::start_result_operation({}, [&](auto) { called = true; });
    ASSERT_FALSE(result);
    EXPECT_EQ(result.error().code, nprpc::ErrorCode::invalid_argument);
    EXPECT_FALSE(called);
}

namespace {
nprpc::flat_buffer request_for_unknown_poa() {
    constexpr auto size = sizeof(nprpc::impl::Header) + sizeof(nprpc::impl::CallHeader);
    nprpc::flat_buffer request;
    std::memset(request.prepare(size).data(), 0, size);
    request.commit(size);
    auto* header = static_cast<nprpc::impl::Header*>(request.data().data());
    header->size = size;
    header->msg_id = nprpc::impl::MessageId::FunctionCall;
    header->msg_type = nprpc::impl::MessageType::Request;
    nprpc::impl::flat::CallHeader_Direct call(request, sizeof(nprpc::impl::Header));
    call.poa_idx() = UINT16_MAX;
    return request;
}
}

TEST_F(SessionResultTests, ResultCallsDeliverRemoteErrorRepliesWithoutChangingTheWireProtocol) {
    rpc->start_thread_pool(2);
    const nprpc::EndPoint endpoint(nprpc::EndPointType::SharedMemory,
                                  nprpc::impl::g_server_listener_uuid, 0);
    auto request = request_for_unknown_poa();
    auto result = runtime().call_result(endpoint, request, 1000);
    ASSERT_TRUE(result);
    EXPECT_EQ(nprpc::impl::flat::Header_Direct(request, 0).msg_id(),
              nprpc::impl::MessageId::Error_PoaNotExist);

    struct Received {
        std::promise<void> done;
        nprpc::Result<void> status;
        nprpc::impl::MessageId id{};
    };
    auto received = std::make_shared<Received>();
    auto done = received->done.get_future();
    auto started = runtime().call_async_result(endpoint, request_for_unknown_poa(),
        [received](nprpc::Result<void> status, nprpc::flat_buffer& response) {
            received->status = std::move(status);
            if (received->status) received->id = nprpc::impl::flat::Header_Direct(response, 0).msg_id();
            received->done.set_value();
        }, 1000);
    ASSERT_TRUE(started);
    ASSERT_EQ(done.wait_for(std::chrono::seconds(5)), std::future_status::ready);
    EXPECT_TRUE(received->status);
    EXPECT_EQ(received->id, nprpc::impl::MessageId::Error_PoaNotExist);
}

TEST_F(SessionResultTests, AcquisitionFailureDoesNotConsumeRequestOrCallHandler) {
    const nprpc::EndPoint endpoint(nprpc::EndPointType::Http, "localhost", 1234);
    auto request = request_for_unknown_poa();
    const auto size = request.size();
    int calls = 0;
    auto result = runtime().call_async_result(endpoint, std::move(request),
        [&](nprpc::Result<void>, nprpc::flat_buffer&) { ++calls; }, 1);
    ASSERT_FALSE(result);
    EXPECT_EQ(result.error().code, nprpc::ErrorCode::unsupported_transport);
    EXPECT_EQ(request.size(), size);
    EXPECT_EQ(calls, 0);
}

#ifdef NPRPC_TCP_ENABLED
TEST_F(SessionResultTests, SocketTimeoutPreservesNativeErrorAndLegacyException) {
    rpc->start_thread_pool(2);
    nprpc::impl::g_cfg.use_uring_tcp = false;
    boost::asio::io_context peer_io;
    boost::asio::ip::tcp::acceptor silent_peer(peer_io,
        {boost::asio::ip::address_v4::loopback(), 0});
    const nprpc::EndPoint endpoint(nprpc::EndPointType::Tcp, "127.0.0.1",
                                  silent_peer.local_endpoint().port());
    auto request = request_for_unknown_poa();
    auto result = runtime().call_result(endpoint, request, 30);
    ASSERT_FALSE(result);
    EXPECT_EQ(result.error().code, nprpc::ErrorCode::timeout);
    const boost::system::error_code expected = boost::asio::error::timed_out;
    EXPECT_EQ(result.error().native_code, expected.value());
    EXPECT_STREQ(result.error().native_category.data(), expected.category().name());
    auto legacy_request = request_for_unknown_poa();
    EXPECT_THROW(runtime().call(endpoint, legacy_request, 30), nprpc::ExceptionCommFailure);
}
#endif

#if defined(NPRPC_HTTP_ENABLED) && defined(NPRPC_WEBSOCKET_ENABLED)
TEST(WebSocketResultTests, SyncCallsPreserveReplyAndTimeoutDetails) {
    boost::asio::io_context io;
    boost::asio::ip::tcp::acceptor reserve(io, {boost::asio::ip::address_v4::loopback(), 0});
    const auto port = reserve.local_endpoint().port();
    reserve.close();
    auto* rpc = nprpc::RpcBuilder().with_http(port).build();
    struct Cleanup { nprpc::Rpc* rpc; ~Cleanup() { rpc->destroy(); } } cleanup{rpc};
    rpc->start_thread_pool(1);
    auto& runtime = *static_cast<nprpc::impl::RpcImpl*>(rpc);
    const nprpc::EndPoint endpoint(nprpc::EndPointType::WebSocket, "127.0.0.1", port);
    auto request = request_for_unknown_poa();
    ASSERT_TRUE(runtime.call_result(endpoint, request, 1000));
    EXPECT_EQ(nprpc::impl::flat::Header_Direct(request, 0).msg_id(), nprpc::impl::MessageId::Error_PoaNotExist);
    auto legacy = request_for_unknown_poa();
    EXPECT_NO_THROW(runtime.call(endpoint, legacy, 1000));

    // A cancellation control frame gets no reply. Drive the existing session
    // timer after its request deadline to exercise the native timeout result.
    auto session = runtime.get_session_result(endpoint);
    ASSERT_TRUE(session);
    (*session)->set_timeout(200);
    (*session)->start_timeout_timer();
    constexpr auto size = sizeof(nprpc::impl::Header) + sizeof(nprpc::impl::flat::StreamCancel);
    nprpc::flat_buffer no_reply;
    std::memset(no_reply.prepare(size).data(), 0, size);
    no_reply.commit(size);
    auto* header = static_cast<nprpc::impl::Header*>(no_reply.data().data());
    header->size = size;
    header->msg_id = nprpc::impl::MessageId::StreamCancellation;
    header->msg_type = nprpc::impl::MessageType::Request;
    auto timed_out = runtime.call_result(endpoint, no_reply, 30);
    ASSERT_FALSE(timed_out);
    EXPECT_EQ(timed_out.error().code, nprpc::ErrorCode::timeout);
    const boost::system::error_code expected = boost::asio::error::timed_out;
    EXPECT_EQ(timed_out.error().native_code, expected.value());
}
#endif
