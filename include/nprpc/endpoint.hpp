// Copyright (c) 2021-2025 nikitapnn1@gmail.com
// This file is a part of npsystem (Distributed Control System) and covered by
// LICENSING file in the topmost directory

#pragma once

#include <charconv>
#include <boost/system/errc.hpp>
#include <boost/beast/core/flat_buffer.hpp>
#include <boost/asio/ip/address_v4.hpp>

namespace nprpc {
static constexpr std::string_view tcp_prefix = "tcp://";
static constexpr std::string_view ws_prefix  = "ws://";
static constexpr std::string_view wss_prefix = "wss://";
static constexpr std::string_view mem_prefix = "mem://";

enum class EndPointType {
  Tcp,
  TcpTethered,
  WebSocket,
  SecuredWebSocket,
  SharedMemory,
};

class EndPoint
{
  EndPointType  type_;
  std::string   hostname_; // or ip address
  std::uint16_t port_;

 public:
  static constexpr std::string_view to_string(EndPointType type) noexcept
  {
    switch (type) {
      case EndPointType::Tcp:
      case EndPointType::TcpTethered:
        return tcp_prefix;
      case EndPointType::WebSocket:
        return ws_prefix;
      case EndPointType::SecuredWebSocket:
        return wss_prefix;
      case EndPointType::SharedMemory:
        return mem_prefix;
      default:
        assert(false);
        return "unknown://";
    }
  }

  std::string to_string() const noexcept
  {
    return std::string(to_string(type_)) + hostname_ + ":" +
           std::to_string(port_);
  }

  bool operator==(const EndPoint& other) const noexcept
  {
    return type_ == other.type_ && hostname_ == other.hostname_ &&
           port_ == other.port_;
  }

  bool operator!=(const EndPoint& other) const noexcept
  {
    return !(*this == other);
  }

  EndPointType     type() const noexcept { return type_; }
  std::string_view hostname() const noexcept { return hostname_; }
  std::uint16_t    port() const noexcept { return port_; }
  bool             empty() const noexcept { return hostname_.empty(); }
  bool             is_ssl() const noexcept { return type_ == EndPointType::SecuredWebSocket; }
  std::string      get_full() const noexcept
  {
    return hostname_ + ":" + std::to_string(port_);
  }

  EndPoint() = default;
  
  EndPoint(
    EndPointType type, std::string_view hostname, std::uint16_t port) noexcept
    : type_ {type}, hostname_ {hostname}, port_ {port}
  {
  }
  
  EndPoint(std::string_view url)
  {
    if (url.empty()) {
      throw std::invalid_argument("URL cannot be empty");
    }

    auto split = [this](std::string_view url, std::string_view prefix) {
      auto to_uint16 = [](const std::string_view& str) {
        std::uint16_t port;
        auto [ptr, ec] =
          std::from_chars(str.data(), str.data() + str.size(), port);
        if (ec == std::errc::invalid_argument ||
          ec == std::errc::result_out_of_range) {
          throw std::invalid_argument("Invalid port number");
        }
        return port;
      };
      auto start = prefix.length();
      auto end = url.find(':', start);
      
      this->hostname_ = url.substr(start, end - start);
      this->port_ = to_uint16(url.substr(end + 1));
    };

    if (url.find(tcp_prefix) == 0) {
      type_ = EndPointType::Tcp;
      split(url, tcp_prefix);
    } else if (url.find(ws_prefix) == 0) {
      type_ = EndPointType::WebSocket;
      split(url, ws_prefix);
    } else if (url.find(wss_prefix) == 0) {
      type_ = EndPointType::SecuredWebSocket;
      split(url, wss_prefix);
    } else if (url.find(mem_prefix) == 0) {
      type_ = EndPointType::SharedMemory;
      split(url, mem_prefix);
    } else {
      throw std::invalid_argument("Invalid URL format");
    }
  }
};

inline std::ostream& operator<<(
  std::ostream& os, const EndPoint& endpoint)
{
  return os << EndPoint::to_string(endpoint.type()) << endpoint.hostname()
            << ":" << endpoint.port();
}

}  // namespace nprpc
