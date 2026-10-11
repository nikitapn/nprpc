// Copyright (c) 2021-2026, Nikita Pennie <nikitapnn1@gmail.com>
// SPDX-License-Identifier: MIT

// File descriptor passing (IDL `fd`).  No transport carries descriptors yet,
// so the generated servant code is driven directly with hand-built requests,
// and the proxies are checked to refuse the call over every transport.

#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <string>
#include <unistd.h>

#include <gtest/gtest.h>

#include <nprpc/impl/nprpc_impl.hpp>
#include <nprpc_nameserver.hpp>
#include <nprpc_test.hpp>

#include "common/helper.inl"

namespace nprpctest {

namespace {

using nprpc::UniqueFd;

std::string read_all(int fd)
{
  std::string out;
  char chunk[256];
  for (;;) {
    const auto n = ::read(fd, chunk, sizeof(chunk));
    if (n < 0 && errno == EINTR)
      continue;
    if (n <= 0)
      break;
    out.append(chunk, static_cast<size_t>(n));
  }
  return out;
}

// A pipe whose read end yields `content`, then end of file.
UniqueFd make_pipe(std::string_view content)
{
  int fds[2];
  if (::pipe2(fds, O_CLOEXEC) != 0)
    throw std::system_error(errno, std::generic_category(), "pipe2");
  UniqueFd rd(fds[0]), wr(fds[1]);
  EXPECT_EQ(::write(wr.get(), content.data(), content.size()),
            static_cast<ssize_t>(content.size()));
  return rd;
}

bool is_open(int fd) { return ::fcntl(fd, F_GETFD) != -1; }

class TestFdImpl : public nprpc::test::ITestFd_Servant
{
public:
  int calls = 0;

  std::string ReadAll(UniqueFd fd) override
  {
    ++calls;
    return read_all(fd.get());
  }

  UniqueFd MakePipe(nprpc::flat::Span<char> content) override
  {
    ++calls;
    return make_pipe((std::string_view)content);
  }

  void MakePipeOut(nprpc::flat::Span<char> content,
                   uint32_t& length,
                   UniqueFd& fd) override
  {
    ++calls;
    length = static_cast<uint32_t>(content.size());
    fd = make_pipe((std::string_view)content);
  }
};

constexpr uint32_t kArgs = 32; // Header + CallHeader
constexpr uint32_t kReply = 16; // Header

// Starts a FunctionCall to `function_idx` with `args_size` bytes of
// arguments, zeroed.
void begin_call(nprpc::flat_buffer& buf, uint8_t function_idx, uint32_t args_size)
{
  buf.consume(buf.size());
  auto mb = buf.prepare(kArgs + args_size);
  std::memset(mb.data(), 0, mb.size());
  buf.commit(kArgs + args_size);
  auto* h = static_cast<nprpc::impl::Header*>(buf.data().data());
  h->msg_id = nprpc::impl::MessageId::FunctionCall;
  h->msg_type = nprpc::impl::MessageType::Request;
  nprpc::impl::flat::CallHeader_Direct ch(buf, sizeof(nprpc::impl::Header));
  ch.function_idx() = function_idx;
}

void end_call(nprpc::flat_buffer& buf)
{
  static_cast<nprpc::impl::Header*>(buf.data().data())->size =
      static_cast<uint32_t>(buf.size());
}

uint32_t& u32_at(nprpc::flat_buffer& buf, uint32_t offset)
{
  return *reinterpret_cast<uint32_t*>(
      static_cast<std::byte*>(buf.data().data()) + offset);
}

nprpc::impl::MessageId reply_id(nprpc::flat_buffer& buf)
{
  return static_cast<nprpc::impl::Header*>(buf.data().data())->msg_id;
}

struct Dispatcher {
  nprpc::SessionContext ctx;
  nprpc::flat_buffer rx, tx;

  Dispatcher()
  {
    ctx.rx_buffer = &rx;
    ctx.tx_buffer = &tx;
  }

  void operator()(nprpc::ObjectServant& servant)
  {
    end_call(rx);
    servant.dispatch(ctx, false);
  }
};

} // namespace

TEST(FlatBufferFds, BelongToTheMessage)
{
  nprpc::flat_buffer buf;
  buf.prepare(8);
  buf.commit(8);

  auto pipe = make_pipe("x");
  const int raw = pipe.get();
  EXPECT_EQ(buf.attach_fd(std::move(pipe)), 0u);
  EXPECT_EQ(buf.attach_fd(UniqueFd::dup(raw)), 1u);
  EXPECT_TRUE(buf.has_fd(1));
  EXPECT_FALSE(buf.has_fd(2));

  // Moves carry them along.
  nprpc::flat_buffer moved(std::move(buf));
  ASSERT_EQ(moved.fds().size(), 2u);

  // Taking leaves an empty slot rather than shifting the indices.
  auto taken = moved.take_fd(0);
  EXPECT_EQ(taken.get(), raw);
  EXPECT_FALSE(moved.has_fd(0));
  EXPECT_TRUE(moved.has_fd(1));
  EXPECT_FALSE(moved.take_fd(0));

  // Emptying the buffer for the next message closes what was not taken.
  const int dup_fd = moved.fds()[1].get();
  moved.consume(moved.size());
  EXPECT_TRUE(moved.fds().empty());
  EXPECT_FALSE(is_open(dup_fd));
  EXPECT_TRUE(is_open(raw));
}

TEST(FdPassingDispatch, InArgument)
{
  TestFdImpl servant;
  Dispatcher call;

  begin_call(call.rx, 0, 4);
  u32_at(call.rx, kArgs) = call.rx.attach_fd(make_pipe("from client"));
  call(servant);

  ASSERT_EQ(reply_id(call.tx), nprpc::impl::MessageId::BlockResponse);
  EXPECT_EQ((std::string_view)nprpc::flat::String_Direct1(call.tx, kReply)(),
            "from client");
  EXPECT_TRUE(call.tx.fds().empty());
}

TEST(FdPassingDispatch, MissingDescriptorIsBadInput)
{
  TestFdImpl servant;
  Dispatcher call;

  // The index names a descriptor that did not arrive with the message.
  begin_call(call.rx, 0, 4);
  u32_at(call.rx, kArgs) = 3;
  call(servant);

  EXPECT_EQ(reply_id(call.tx), nprpc::impl::MessageId::Error_BadInput);
  EXPECT_EQ(servant.calls, 0);
}

TEST(FdPassingDispatch, ReturnValue)
{
  TestFdImpl servant;
  Dispatcher call;

  begin_call(call.rx, 1, 8);
  nprpc::flat::String_Direct1(call.rx, kArgs) = "from server";
  call(servant);

  ASSERT_EQ(reply_id(call.tx), nprpc::impl::MessageId::BlockResponse);
  auto fd = call.tx.take_fd(u32_at(call.tx, kReply));
  ASSERT_TRUE(fd);
  EXPECT_EQ(read_all(fd.get()), "from server");
}

TEST(FdPassingDispatch, OutArgument)
{
  TestFdImpl servant;
  Dispatcher call;

  begin_call(call.rx, 2, 8);
  nprpc::flat::String_Direct1(call.rx, kArgs) = "out param";
  call(servant);

  ASSERT_EQ(reply_id(call.tx), nprpc::impl::MessageId::BlockResponse);
  EXPECT_EQ(u32_at(call.tx, kReply), 9u);
  auto fd = call.tx.take_fd(u32_at(call.tx, kReply + 4));
  ASSERT_TRUE(fd);
  EXPECT_EQ(read_all(fd.get()), "out param");
}

// Until a transport carries descriptors, every proxy call with an fd must
// fail before anything is sent, and leave the caller's descriptor alone.
TEST_F(NprpcTest, FdPassingRefusedByTransport)
{
  TestFdImpl servant;
  auto exec_test = [this, &servant](nprpc::ObjectActivationFlags flags) {
    auto obj = bind_and_resolve<nprpc::test::TestFd>(servant, flags);
    auto pipe = make_pipe("unused");

    EXPECT_THROW(obj->ReadAll(pipe.get()), nprpc::Exception);
    EXPECT_THROW(obj->MakePipe("x"), nprpc::Exception);
    uint32_t length;
    UniqueFd out;
    EXPECT_THROW(obj->MakePipeOut("x", length, out), nprpc::Exception);

    EXPECT_TRUE(is_open(pipe.get()));
    EXPECT_EQ(servant.calls, 0);
  };

  exec_test(nprpc::ObjectActivationFlags::tcp);
  exec_test(nprpc::ObjectActivationFlags::shm);
}

} // namespace nprpctest
