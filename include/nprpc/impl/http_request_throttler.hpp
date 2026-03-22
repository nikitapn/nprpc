// Copyright (c) 2021-2025, Nikita Pennie <nikitapnn1@gmail.com>
// SPDX-License-Identifier: MIT

#pragma once

#include <ankerl/unordered_dense.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <boost/asio/ip/address.hpp>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <mutex>

namespace nprpc::impl {

struct IpAddressKey {
  uint64_t hi = 0;
  uint64_t lo = 0;

  friend bool operator==(const IpAddressKey&, const IpAddressKey&) = default;
};

struct IpAddressKeyHash {
  using is_avalanching = void;

  [[nodiscard]] auto operator()(const IpAddressKey& key) const noexcept
      -> uint64_t
  {
    return mix(key.hi, key.lo);
  }

private:
  [[nodiscard]] static auto avalanche(uint64_t value) noexcept -> uint64_t
  {
    value ^= value >> 33U;
    value *= 0xff51afd7ed558ccdULL;
    value ^= value >> 33U;
    value *= 0xc4ceb9fe1a85ec53ULL;
    value ^= value >> 33U;
    return value;
  }

  [[nodiscard]] static auto mix(uint64_t hi, uint64_t lo) noexcept -> uint64_t
  {
    return avalanche(hi ^ (avalanche(lo) + 0x9e3779b97f4a7c15ULL +
                           (hi << 6U) + (hi >> 2U)));
  }
};

class HttpRequestThrottler
{
  struct TokenBucket {
    double tokens = 0.0;
    std::chrono::steady_clock::time_point last_refill{};
    bool initialized = false;

    bool consume(size_t rate_per_second,
                 size_t burst,
                 std::chrono::steady_clock::time_point now,
                 double cost = 1.0)
    {
      if (rate_per_second == 0) {
        return true;
      }

      const double capacity =
          static_cast<double>(burst == 0 ? rate_per_second : burst);
      if (!initialized) {
        tokens = capacity;
        last_refill = now;
        initialized = true;
      } else {
        const std::chrono::duration<double> elapsed = now - last_refill;
        tokens = std::min(capacity,
                          tokens + elapsed.count() *
                                       static_cast<double>(rate_per_second));
        last_refill = now;
      }

      if (tokens < cost) {
        return false;
      }

      tokens -= cost;
      return true;
    }
  };

  struct IpState {
    TokenBucket http3_new_connections;
    TokenBucket http_rpc_requests;
    TokenBucket webtransport_connects;
    TokenBucket websocket_upgrades;
    std::chrono::steady_clock::time_point last_seen{};
    size_t http3_active_connections = 0;
    size_t websocket_active_sessions = 0;
  };

  struct SessionState {
    TokenBucket request_bucket;
    TokenBucket stream_open_bucket;
    std::chrono::steady_clock::time_point last_seen{};
  };

  static constexpr size_t ip_shard_count_ = 64;
  static constexpr size_t session_shard_count_ = 64;

  static constexpr auto state_ttl_ = std::chrono::minutes(15);

  struct IpShard {
    std::mutex mutex;
    ankerl::unordered_dense::map<IpAddressKey, IpState, IpAddressKeyHash> map;
    uint64_t sweep_counter = 0;
  };

  struct SessionShard {
    std::mutex mutex;
    ankerl::unordered_dense::map<uint64_t, SessionState> map;
    uint64_t sweep_counter = 0;
  };

  std::array<IpShard, ip_shard_count_> ip_shards_{};
  std::array<SessionShard, session_shard_count_> session_shards_{};
  std::atomic<uint64_t> next_session_key_{1};

  static void sweep_locked(IpShard& shard,
                           std::chrono::steady_clock::time_point now)
  {
    for (auto it = shard.map.begin(); it != shard.map.end();) {
      if (it->second.http3_active_connections == 0 &&
          it->second.websocket_active_sessions == 0 &&
          now - it->second.last_seen > state_ttl_) {
        it = shard.map.erase(it);
      } else {
        ++it;
      }
    }
  }

  static void sweep_locked(SessionShard& shard,
                           std::chrono::steady_clock::time_point now)
  {
    for (auto it = shard.map.begin(); it != shard.map.end();) {
      if (now - it->second.last_seen > state_ttl_) {
        it = shard.map.erase(it);
      } else {
        ++it;
      }
    }
  }

  static void maybe_sweep_locked(IpShard& shard,
                                 std::chrono::steady_clock::time_point now)
  {
    if ((++shard.sweep_counter & 0xffU) == 0) {
      sweep_locked(shard, now);
    }
  }

  static void maybe_sweep_locked(SessionShard& shard,
                                 std::chrono::steady_clock::time_point now)
  {
    if ((++shard.sweep_counter & 0xffU) == 0) {
      sweep_locked(shard, now);
    }
  }

  [[nodiscard]] static auto to_storage(const boost::asio::ip::address& address)
      -> std::array<uint8_t, 16>
  {
    std::array<uint8_t, 16> storage{};
    if (address.is_v4()) {
      storage[10] = 0xff;
      storage[11] = 0xff;
      const auto bytes = address.to_v4().to_bytes();
      std::copy(bytes.begin(), bytes.end(), storage.begin() + 12);
      return storage;
    }

    const auto bytes = address.to_v6().to_bytes();
    std::copy(bytes.begin(), bytes.end(), storage.begin());
    return storage;
  }

  [[nodiscard]] static auto hash_ip_key(const IpAddressKey& key) noexcept
      -> uint64_t
  {
    return IpAddressKeyHash{}(key);
  }

  [[nodiscard]] static auto ip_shard_for(const IpAddressKey& key) noexcept
      -> size_t
  {
    return static_cast<size_t>(hash_ip_key(key) % ip_shard_count_);
  }

  [[nodiscard]] static auto session_shard_for(uint64_t session_key) noexcept
      -> size_t
  {
    return static_cast<size_t>(session_key % session_shard_count_);
  }

public:
  [[nodiscard]] static auto make_ip_key(const boost::asio::ip::address& address)
      -> IpAddressKey
  {
    const auto storage = to_storage(address);
    IpAddressKey key;
    std::memcpy(&key.hi, storage.data(), sizeof(key.hi));
    std::memcpy(&key.lo, storage.data() + sizeof(key.hi), sizeof(key.lo));
    return key;
  }

  [[nodiscard]] auto allocate_session_key() noexcept -> uint64_t
  {
    return next_session_key_.fetch_add(1, std::memory_order_relaxed);
  }

  bool allow_http3_new_connection(const boost::asio::ip::address& ip,
                                  size_t max_active_connections,
                                  size_t rate_per_second,
                                  size_t burst)
  {
    const auto key = make_ip_key(ip);
    const auto now = std::chrono::steady_clock::now();
    auto& shard = ip_shards_[ip_shard_for(key)];
    std::lock_guard lock(shard.mutex);
    maybe_sweep_locked(shard, now);

    auto& state = shard.map[key];
    state.last_seen = now;

    if (max_active_connections != 0 &&
        state.http3_active_connections >= max_active_connections) {
      return false;
    }

    return state.http3_new_connections.consume(rate_per_second, burst, now);
  }

  void on_http3_connection_accepted(const boost::asio::ip::address& ip)
  {
    const auto key = make_ip_key(ip);
    const auto now = std::chrono::steady_clock::now();
    auto& shard = ip_shards_[ip_shard_for(key)];
    std::lock_guard lock(shard.mutex);
    auto& state = shard.map[key];
    state.last_seen = now;
    ++state.http3_active_connections;
  }

  void on_http3_connection_closed(const boost::asio::ip::address& ip)
  {
    const auto key = make_ip_key(ip);
    const auto now = std::chrono::steady_clock::now();
    auto& shard = ip_shards_[ip_shard_for(key)];
    std::lock_guard lock(shard.mutex);
    auto it = shard.map.find(key);
    if (it == shard.map.end()) {
      return;
    }

    it->second.last_seen = now;
    if (it->second.http3_active_connections > 0) {
      --it->second.http3_active_connections;
    }
  }

  bool allow_http_rpc_request(const boost::asio::ip::address& ip,
                              size_t rate_per_second,
                              size_t burst)
  {
    const auto key = make_ip_key(ip);
    const auto now = std::chrono::steady_clock::now();
    auto& shard = ip_shards_[ip_shard_for(key)];
    std::lock_guard lock(shard.mutex);
    maybe_sweep_locked(shard, now);

    auto& state = shard.map[key];
    state.last_seen = now;
    return state.http_rpc_requests.consume(rate_per_second, burst, now);
  }

  bool allow_webtransport_connect(const boost::asio::ip::address& ip,
                                  size_t rate_per_second,
                                  size_t burst)
  {
    const auto key = make_ip_key(ip);
    const auto now = std::chrono::steady_clock::now();
    auto& shard = ip_shards_[ip_shard_for(key)];
    std::lock_guard lock(shard.mutex);
    maybe_sweep_locked(shard, now);

    auto& state = shard.map[key];
    state.last_seen = now;
    return state.webtransport_connects.consume(rate_per_second, burst, now);
  }

  bool allow_websocket_upgrade(const boost::asio::ip::address& ip,
                               size_t rate_per_second,
                               size_t burst)
  {
    const auto key = make_ip_key(ip);
    const auto now = std::chrono::steady_clock::now();
    auto& shard = ip_shards_[ip_shard_for(key)];
    std::lock_guard lock(shard.mutex);
    maybe_sweep_locked(shard, now);

    auto& state = shard.map[key];
    state.last_seen = now;
    return state.websocket_upgrades.consume(rate_per_second, burst, now);
  }

  bool try_acquire_websocket_session(const boost::asio::ip::address& ip,
                                     size_t max_active_sessions)
  {
    const auto key = make_ip_key(ip);
    const auto now = std::chrono::steady_clock::now();
    auto& shard = ip_shards_[ip_shard_for(key)];
    std::lock_guard lock(shard.mutex);
    maybe_sweep_locked(shard, now);

    auto& state = shard.map[key];
    state.last_seen = now;

    if (max_active_sessions != 0 &&
        state.websocket_active_sessions >= max_active_sessions) {
      return false;
    }

    ++state.websocket_active_sessions;
    return true;
  }

  void release_session(uint64_t session_key)
  {
    if (session_key == 0) {
      return;
    }

    auto& shard = session_shards_[session_shard_for(session_key)];
    std::lock_guard lock(shard.mutex);
    shard.map.erase(session_key);
  }

  void release_websocket_session(const IpAddressKey& ip, uint64_t session_key)
  {
    const auto now = std::chrono::steady_clock::now();
    auto& shard = ip_shards_[ip_shard_for(ip)];
    {
      std::lock_guard lock(shard.mutex);
      if (auto it = shard.map.find(ip); it != shard.map.end()) {
        it->second.last_seen = now;
        if (it->second.websocket_active_sessions > 0) {
          --it->second.websocket_active_sessions;
        }
      }
    }

    release_session(session_key);
  }

  bool allow_session_request(uint64_t session_key,
                             size_t rate_per_second,
                             size_t burst)
  {
    if (session_key == 0) {
      return true;
    }

    const auto now = std::chrono::steady_clock::now();
    auto& shard = session_shards_[session_shard_for(session_key)];
    std::lock_guard lock(shard.mutex);
    maybe_sweep_locked(shard, now);

    auto& state = shard.map[session_key];
    state.last_seen = now;
    return state.request_bucket.consume(rate_per_second, burst, now);
  }

  bool allow_session_stream_open(uint64_t session_key,
                                 size_t rate_per_second,
                                 size_t burst)
  {
    if (session_key == 0) {
      return true;
    }

    const auto now = std::chrono::steady_clock::now();
    auto& shard = session_shards_[session_shard_for(session_key)];
    std::lock_guard lock(shard.mutex);
    maybe_sweep_locked(shard, now);

    auto& state = shard.map[session_key];
    state.last_seen = now;
    return state.stream_open_bucket.consume(rate_per_second, burst, now);
  }
};

inline HttpRequestThrottler& http_request_throttler()
{
  static HttpRequestThrottler throttler;
  return throttler;
}

} // namespace nprpc::impl