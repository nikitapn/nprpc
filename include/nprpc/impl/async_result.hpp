// Copyright (c) 2021-2026, Nikita Pennie <nikitapnn1@gmail.com>
// SPDX-License-Identifier: MIT

#pragma once

#include <nprpc/flat_buffer.hpp>
#include <nprpc/impl/result.hpp>
#include <atomic>
#include <functional>
#include <memory>

namespace nprpc::impl {

using SendReceiveResultHandler = std::function<void(Result<void>, flat_buffer&)>;

// Owns completion until either initiation fails or a transport callback wins.
// The handler must not throw and the response is borrowed during its invocation.
class ResultCompletion {
  std::atomic<bool> completed_{false};
  SendReceiveResultHandler handler_;

public:
  explicit ResultCompletion(SendReceiveResultHandler handler) : handler_(std::move(handler)) {}

  // Returns true if completion already took ownership of the handler/context.
  bool abandon() noexcept { return completed_.exchange(true); }

  void complete(const boost::system::error_code& ec, flat_buffer& response) noexcept {
    if (completed_.exchange(true)) return;
    auto result = capture_result([&]() -> Result<void> {
      if (ec) return std::unexpected(transport_error(ec));
      return {};
    });
    handler_(std::move(result), response);
  }
};

// Initiation failure: return an error, never call the handler later.
// Success: the handler is called once, possibly inline. If a broken transport
// calls back and then throws, completion wins and initiation reports success.
template<class Start>
Result<void> start_result_operation(SendReceiveResultHandler handler, Start&& start) noexcept {
  if (!handler)
    return std::unexpected(Error(ErrorCode::invalid_argument, "Missing RPC completion handler"));
  std::shared_ptr<ResultCompletion> completion;
  auto started = capture_result([&]() -> Result<void> {
    completion = std::make_shared<ResultCompletion>(std::move(handler));
    start([state = completion](const boost::system::error_code& ec, flat_buffer& response) noexcept {
      state->complete(ec, response);
    });
    return {};
  }, ErrorCode::connection_failure);
  if (!started && completion && completion->abandon()) return {};
  return started;
}

} // namespace nprpc::impl
