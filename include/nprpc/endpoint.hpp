// Copyright (c) 2021-2025, Nikita Pennie <nikitapnn1@gmail.com>
// SPDX-License-Identifier: MIT

#pragma once

#include <boost/asio/ip/address_v4.hpp>
#include <boost/system/errc.hpp>
#include <charconv>

#include <nprpc_base_ext.hpp>

namespace nprpc {
/// URL scheme of native TCP endpoints.
static constexpr std::string_view tcp_prefix = "tcp://";
/// URL scheme shared by HTTP, WebSocket and WebTransport endpoints.
static constexpr std::string_view web_prefix = "web://";
/// URL scheme of shared-memory endpoints (same machine only).
static constexpr std::string_view mem_prefix = "mem://";
/// URL scheme of native QUIC endpoints.
static constexpr std::string_view quic_prefix = "quic://";

/// A transport plus an address: host and port, or a shared-memory channel
/// id. One entry of `ObjectId::urls()`, parsed.
class EndPoint
{
  EndPointType type_;
  std::string hostname_; // or ip address, or channel ID for shared memory
  std::uint16_t port_;

public:
  /// The URL scheme for `type`, e.g. `tcp://`.
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

  /// The endpoint as a URL: `tcp://host:port`, or `mem://<channel>`.
  std::string to_string() const noexcept
  {
    if (type_ == EndPointType::SharedMemory) {
      // Shared memory doesn't use port numbers
      return std::string(to_string(type_)) + hostname_;
    }
    return std::string(to_string(type_)) + hostname_ + ":" +
           std::to_string(port_);
  }

  /// Same transport, host and port.
  bool operator==(const EndPoint& other) const noexcept
  {
    return type_ == other.type_ && hostname_ == other.hostname_ &&
           port_ == other.port_;
  }

  /// Differs in transport, host or port.
  bool operator!=(const EndPoint& other) const noexcept
  {
    return !(*this == other);
  }

  /// The transport.
  EndPointType type() const noexcept { return type_; }
  /// Host name or IP address; the channel id for shared memory.
  std::string_view hostname() const noexcept { return hostname_; }
  /// Port; 0 for shared memory.
  std::uint16_t port() const noexcept { return port_; }
  /// Whether no address is set.
  bool empty() const noexcept { return hostname_.empty(); }
  /// Whether the transport is encrypted with TLS (WSS, HTTPS,
  /// WebTransport).
  bool is_ssl() const noexcept
  {
    return type_ == EndPointType::SecuredWebSocket ||
           type_ == EndPointType::SecuredHttp ||
           type_ == EndPointType::WebTransport;
  }

  /// For shared memory endpoints, the channel id; empty otherwise.
  std::string_view memory_channel_id() const noexcept
  {
    return (type_ == EndPointType::SharedMemory) ? hostname_
                                                 : std::string_view{};
  }

  /// `host:port` without the scheme; the channel id for shared memory.
  std::string get_full() const noexcept
  {
    if (type_ == EndPointType::SharedMemory) {
      // For shared memory, just return the channel ID
      return hostname_;
    }
    return hostname_ + ":" + std::to_string(port_);
  }

  /// An empty endpoint.
  EndPoint() = default;

  /// An endpoint from its parts.
  EndPoint(EndPointType type, std::string_view hostname, std::uint16_t port) noexcept
      : type_{type}
      , hostname_{hostname}
      , port_{port}
  {
  }

  /// Parses a URL such as `tcp://host:port` or `mem://<channel>`.
  ///
  /// `web://` URLs serve several transports, so `type` says which one this
  /// is. Throws `std::invalid_argument` for an unknown scheme, a missing or
  /// malformed port, or a `web://` URL without `type`.
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

/// Prints the endpoint as a URL.
inline std::ostream& operator<<(std::ostream& os, const EndPoint& endpoint)
{
  return os << endpoint.to_string();
}

} // namespace nprpc
