// Copyright (c) 2021-2026, Nikita Pennie <nikitapnn1@gmail.com>
// SPDX-License-Identifier: MIT

#pragma once

#include <nprpc/result.hpp>
#include <nprpc_base.hpp>
#include <boost/asio/error.hpp>
#include <boost/system/system_error.hpp>
#include <new>
#include <system_error>

namespace nprpc::impl {

inline ErrorCode transport_error_code(const boost::system::error_code& ec) noexcept {
  if (ec == boost::asio::error::timed_out || ec == boost::system::errc::timed_out)
    return ErrorCode::timeout;
  if (ec == boost::asio::error::operation_aborted || ec == boost::system::errc::operation_canceled)
    return ErrorCode::cancelled;
  return ErrorCode::connection_failure;
}

inline Error transport_error(const boost::system::error_code& ec) {
  return {transport_error_code(ec), ec.message().c_str(), ec.value(), ec.category().name()};
}

// Transitional boundary for code whose dependencies still throw. Normal
// failures are returned as Result values; translating exceptions allocates no
// memory. The callable may also return expected<T, E> when E accepts Error.
// A caller may classify generic dependency exceptions using its own operation
// context; unknown non-standard exceptions always remain internal errors.
template<class F>
auto capture_result(F&& fn, ErrorCode exception_code = ErrorCode::internal_error) noexcept -> decltype(fn()) {
  try {
    return fn();
  } catch (const nprpc::ExceptionCommFailure& e) {
    return std::unexpected(Error(ErrorCode::connection_failure, e.message.c_str()));
  } catch (const nprpc::ExceptionTimeout&) {
    return std::unexpected(Error(ErrorCode::timeout, "Timed out"));
  } catch (const nprpc::OperationCancelled& e) {
    return std::unexpected(Error(ErrorCode::cancelled, e.what()));
  } catch (const boost::system::system_error& e) {
    const auto& ec = e.code();
    return std::unexpected(Error(transport_error_code(ec), e.what(), ec.value(), ec.category().name()));
  } catch (const std::system_error& e) {
    const auto& ec = e.code();
    auto code = ErrorCode::connection_failure;
    if (ec == std::errc::timed_out) code = ErrorCode::timeout;
    else if (ec == std::errc::operation_canceled) code = ErrorCode::cancelled;
    return std::unexpected(Error(code, e.what(), ec.value(), ec.category().name()));
  } catch (const std::bad_alloc&) {
    return std::unexpected(Error(ErrorCode::out_of_memory, "Out of memory"));
  } catch (const std::invalid_argument& e) {
    return std::unexpected(Error(ErrorCode::invalid_argument, e.what()));
  } catch (const std::exception& e) {
    return std::unexpected(Error(exception_code, e.what()));
  } catch (...) {
    return std::unexpected(Error(ErrorCode::internal_error, "Unknown C++ exception"));
  }
}

} // namespace nprpc::impl
