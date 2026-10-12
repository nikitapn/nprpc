// Copyright (c) 2021-2026, Nikita Pennie <nikitapnn1@gmail.com>
// SPDX-License-Identifier: MIT

#include <nprpc/flat.hpp>
#include <nprpc/unique_fd.hpp>
#include <nprpc_base.hpp>

#include <cerrno>
#include <system_error>

#if defined(_WIN32)
# include <io.h>
#else
# include <fcntl.h>
# include <unistd.h>
#endif

namespace nprpc {

UniqueFd UniqueFd::dup(int fd)
{
  if (fd < 0)
    return {};
#if defined(_WIN32)
  const int copy = ::_dup(fd);
#else
  const int copy = ::fcntl(fd, F_DUPFD_CLOEXEC, 0);
#endif
  if (copy < 0)
    throw std::system_error(errno, std::generic_category(), "dup");
  return UniqueFd(copy);
}

void UniqueFd::reset(int fd) noexcept
{
  if (fd_ >= 0 && fd_ != fd) {
#if defined(_WIN32)
    ::_close(fd_);
#else
    ::close(fd_);
#endif
  }
  fd_ = fd;
}

namespace impl {

void throw_bad_fd_index()
{
  throw ExceptionBadInput();
}

} // namespace impl

} // namespace nprpc
