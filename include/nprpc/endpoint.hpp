// Copyright (c) 2021-2025, Nikita Pennie <nikitapnn1@gmail.com>
// SPDX-License-Identifier: MIT

#pragma once

#include <boost/asio/ip/address_v4.hpp>
#include <boost/system/errc.hpp>
#include <charconv>

#include <nprpc_base_ext.hpp>

namespace nprpc {
static constexpr std::string_view tcp_prefix = "tcp://";
static constexpr std::string_view web_prefix = "web://";
static constexpr std::string_view mem_prefix = "mem://";
static constexpr std::string_view quic_prefix = "quic://";

class EndPoint
{
  EndPointType type_;
  std::string hostname_; // or ip address, or channel ID for shared memory
  std::uint16_t port_;

public:
  static constexpr std::string_view to_string(EndPointType type) noexcept
  {
    switch (type) {
    case EndPointType::Tcp:
    case EndPointType::TcpPrivate:
      return tcp_prefix;
    case EndPointType::WebSocket:
    case EndPointType::SecuredWebSocket:
    case EndPointType::Http:
    case EndPointType::SecuredHttp:
    case EndPointType::WebTransport:
      return web_prefix;
    case EndPointType::SharedMemory:
      return mem_prefix;
    case EndPointType::Quic:
      return quic_prefix;
    default:
      assert(false);
      return "unknown://";
    }
  }

  std::string to_string() const noexcept
  {
    if (type_ == EndPointType::SharedMemory) {
      // Shared memory doesn't use port numbers
      return std::string(to_string(type_)) + hostname_;
    }
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

  EndPointType type() const noexcept { return type_; }
  std::string_view hostname() const noexcept { return hostname_; }
  std::uint16_t port() const noexcept { return port_; }
  bool empty() const noexcept { return hostname_.empty(); }
  bool is_ssl() const noexcept
  {
    return type_ == EndPointType::SecuredWebSocket ||
           type_ == EndPointType::SecuredHttp ||
           type_ == EndPointType::WebTransport;
  }

  // For shared memory endpoints, return the channel ID (stored in hostname_)
  std::string_view memory_channel_id() const noexcept
  {
    return (type_ == EndPointType::SharedMemory) ? hostname_
                                                 : std::string_view{};
  }

  std::string get_full() const noexcept
  {
    if (type_ == EndPointType::SharedMemory) {
      // For shared memory, just return the channel ID
      return hostname_;
    }
    return hostname_ + ":" + std::to_string(port_);
  }

  EndPoint() = default;

  EndPoint(EndPointType type, std::string_view hostname, std::uint16_t port) noexcept
      : type_{type}
      , hostname_{hostname}
      , port_{port}
  {
  }

  EndPoint(std::string_view url, std::optional<EndPointType> type = std::nullopt)
  {
    if (url.empty()) {
      throw std::invalid_argument("URL cannot be empty");
    }

    auto split = [this](std::string_view url, std::string_view prefix,
                        bool require_port = true) {
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

      if (end == std::string_view::npos) {
        // No port specified
        if (require_port) {
          throw std::invalid_argument("Missing port number");
        }
        this->hostname_ = url.substr(start);
        this->port_ = 0;
      } else {
        this->hostname_ = url.substr(start, end - start);
        this->port_ = to_uint16(url.substr(end + 1));
      }
    };

    if (url.find(tcp_prefix) == 0) {
      type_ = EndPointType::Tcp;
      split(url, tcp_prefix, true);
    } else if (url.find(web_prefix) == 0) {
      if (type.has_value()) {
        type_ = type.value();
      } else {
        throw std::invalid_argument( "Missing type for web transport endpoint");
      }
      split(url, web_prefix, true);
    } else if (url.find(mem_prefix) == 0) {
      type_ = EndPointType::SharedMemory;
      split(url, mem_prefix, false); // Port is optional for shared memory
    } else if (url.find(quic_prefix) == 0) {
      type_ = EndPointType::Quic;
      split(url, quic_prefix, true);
    } else {
      throw std::invalid_argument("Invalid URL format");
    }
  }
};

inline std::ostream& operator<<(std::ostream& os, const EndPoint& endpoint)
{
  return os << endpoint.to_string();
}

} // namespace nprpc
