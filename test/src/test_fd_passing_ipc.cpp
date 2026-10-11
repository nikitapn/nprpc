// Copyright (c) 2021-2026, Nikita Pennie <nikitapnn1@gmail.com>
// SPDX-License-Identifier: MIT

// File descriptors between two processes over shared memory: a forked child
// serves TestFd, the parent calls it.  Its own executable, because the fork
// has to happen before either side starts an Rpc (and its threads).

#include <cerrno>
#include <fcntl.h>
#include <string>
#include <sys/wait.h>
#include <unistd.h>

#include <gtest/gtest.h>

#include <nprpc/impl/nprpc_impl.hpp>
#include <nprpc_test.hpp>

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

UniqueFd make_pipe(std::string_view content)
{
  int fds[2];
  if (::pipe2(fds, O_CLOEXEC) != 0)
    throw std::system_error(errno, std::generic_category(), "pipe2");
  UniqueFd rd(fds[0]), wr(fds[1]);
  if (::write(wr.get(), content.data(), content.size()) !=
      static_cast<ssize_t>(content.size()))
    throw std::system_error(errno, std::generic_category(), "write");
  return rd;
}

// Says which process it ran in, so the caller can tell the call crossed.
class TestFdImpl : public nprpc::test::ITestFd_Servant
{
  static std::string pid_tag() { return "@" + std::to_string(::getpid()); }

public:
  std::string ReadAll(UniqueFd fd) override { return read_all(fd.get()) + pid_tag(); }

  UniqueFd MakePipe(nprpc::flat::Span<char> content) override
  {
    return make_pipe(std::string((std::string_view)content) + pid_tag());
  }

  void MakePipeOut(nprpc::flat::Span<char> content,
                   uint32_t& length,
                   UniqueFd& fd) override
  {
    length = static_cast<uint32_t>(content.size());
    fd = make_pipe(std::string((std::string_view)content) + pid_tag());
  }
};

nprpc::Rpc* make_rpc()
{
  auto* rpc = nprpc::RpcBuilder()
                  .set_log_level(nprpc::LogLevel::error)
                  .with_hostname("localhost")
                  .build();
  rpc->start_thread_pool(2);
  return rpc;
}

// The child: serves until the parent closes `done`.
[[noreturn]] void serve(int ior_out, int done)
{
  int status = 1;
  try {
    auto* rpc = make_rpc();
    auto* poa = rpc->create_poa()
                    .with_max_objects(4)
                    .with_lifespan(nprpc::PoaPolicy::Lifespan::Persistent)
                    .build();
    TestFdImpl servant;
    const auto oid =
        poa->activate_object(&servant, nprpc::ObjectActivationFlags::shm);

    const auto ior = oid.to_string() + "\n";
    if (::write(ior_out, ior.data(), ior.size()) ==
        static_cast<ssize_t>(ior.size())) {
      ::close(ior_out);
      char c;
      while (::read(done, &c, 1) < 0 && errno == EINTR) {
      }
      status = 0;
    }
    rpc->destroy();
  } catch (const std::exception& e) {
    std::fprintf(stderr, "child: %s\n", e.what());
  }
  ::_exit(status);
}

} // namespace

TEST(FdPassingIpc, AcrossProcesses)
{
  int ior_pipe[2], done_pipe[2];
  ASSERT_EQ(::pipe2(ior_pipe, O_CLOEXEC), 0);
  ASSERT_EQ(::pipe2(done_pipe, O_CLOEXEC), 0);

  const pid_t child = ::fork();
  ASSERT_GE(child, 0);
  if (child == 0) {
    ::close(ior_pipe[0]);
    ::close(done_pipe[1]);
    serve(ior_pipe[1], done_pipe[0]);
  }
  ::close(ior_pipe[1]);
  ::close(done_pipe[0]);

  const std::string ior = read_all(ior_pipe[0]);
  ::close(ior_pipe[0]);
  ASSERT_FALSE(ior.empty()) << "child did not publish its object";

  auto* rpc = make_rpc();
  {
    nprpc::Object* raw = nprpc::Object::from_string(
        std::string_view(ior).substr(0, ior.find('\n')));
    ASSERT_NE(raw, nullptr);
    auto obj = nprpc::ObjectPtr(nprpc::narrow<nprpc::test::TestFd>(raw));
    ASSERT_TRUE(obj);

    const std::string child_tag = "@" + std::to_string(child);

    auto pipe = make_pipe("to the child");
    EXPECT_EQ(obj->ReadAll(pipe.get()), "to the child" + child_tag);
    EXPECT_NE(::fcntl(pipe.get(), F_GETFD), -1);

    auto returned = obj->MakePipe("from the child");
    ASSERT_TRUE(returned);
    EXPECT_EQ(read_all(returned.get()), "from the child" + child_tag);

    uint32_t length = 0;
    UniqueFd out;
    obj->MakePipeOut("out param", length, out);
    EXPECT_EQ(length, 9u);
    ASSERT_TRUE(out);
    EXPECT_EQ(read_all(out.get()), "out param" + child_tag);
  }

  ::close(done_pipe[1]);
  int status = 0;
  ASSERT_EQ(::waitpid(child, &status, 0), child);
  EXPECT_TRUE(WIFEXITED(status) && WEXITSTATUS(status) == 0);

  // Server gone first: the client still has to come down cleanly.
  rpc->destroy();
}
