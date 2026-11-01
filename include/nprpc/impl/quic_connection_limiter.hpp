#pragma once

#include <boost/asio/ip/address.hpp>

#include <array>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <map>
#include <memory>
#include <mutex>

namespace nprpc::impl {

// Caps on a QUIC listener's connections, overall and per client. A client is an
// IPv4 address or an IPv6 /64, which is what one customer usually holds. 0 means
// no limit.
//
// Only addresses proven by a Retry round trip get here, so a spoofing sender
// can't grow the table or use up another address's allowance.
class QuicConnectionLimiter : public std::enable_shared_from_this<QuicConnectionLimiter>
{
public:
  using Clock = std::chrono::steady_clock;

  // One connection, counted for as long as this object lives.
  class Slot
  {
    std::shared_ptr<QuicConnectionLimiter> limiter_;
    std::array<uint8_t, 16> key_;
  public:
    Slot(std::shared_ptr<QuicConnectionLimiter> limiter, const std::array<uint8_t, 16>& key)
      : limiter_(std::move(limiter)), key_(key) {}
    ~Slot() { limiter_->release(key_); }
    Slot(const Slot&) = delete;
    Slot& operator=(const Slot&) = delete;
  };

  QuicConnectionLimiter(size_t max_connections, size_t max_per_client, size_t new_per_client_per_second)
    : max_connections_(max_connections)
    , max_per_client_(max_per_client)
    , new_per_second_(new_per_client_per_second)
  {
  }

  // Null if this connection is over a limit.
  std::unique_ptr<Slot> acquire(const boost::asio::ip::address& address, Clock::time_point now = Clock::now())
  {
    const auto key = client_key(address);
    std::lock_guard lock(mutex_);
    sweep(now);
    if (max_connections_ != 0 && total_ >= max_connections_) return nullptr;

    auto& client = clients_[key];
    if (now - client.window_start >= std::chrono::seconds(1)) {
      client.window_start = now;
      client.new_in_window = 0;
    }
    const bool allowed = (max_per_client_ == 0 || client.active < max_per_client_) &&
                         (new_per_second_ == 0 || client.new_in_window < new_per_second_);
    if (!allowed) return nullptr;

    ++client.active;
    ++client.new_in_window;
    ++total_;
    return std::make_unique<Slot>(shared_from_this(), key);
  }

  size_t tracked_clients()
  {
    std::lock_guard lock(mutex_);
    return clients_.size();
  }

private:
  struct Client {
    size_t active = 0;
    size_t new_in_window = 0;
    Clock::time_point window_start{};
  };

  static std::array<uint8_t, 16> client_key(const boost::asio::ip::address& address)
  {
    std::array<uint8_t, 16> key{};
    if (address.is_v4()) {
      const auto bytes = address.to_v4().to_bytes();
      std::memcpy(key.data() + 12, bytes.data(), bytes.size());
    } else if (const auto v6 = address.to_v6(); v6.is_v4_mapped()) {
      const auto bytes = boost::asio::ip::make_address_v4(boost::asio::ip::v4_mapped, v6).to_bytes();
      std::memcpy(key.data() + 12, bytes.data(), bytes.size());
    } else {
      const auto bytes = v6.to_bytes();
      std::memcpy(key.data(), bytes.data(), 8); // The /64.
      key[8] = 1;                               // Can't collide with an IPv4 key.
    }
    return key;
  }

  void release(const std::array<uint8_t, 16>& key)
  {
    std::lock_guard lock(mutex_);
    --total_;
    if (auto it = clients_.find(key); it != clients_.end() && it->second.active > 0) --it->second.active;
  }

  void sweep(Clock::time_point now)
  {
    if (now - last_sweep_ < std::chrono::seconds(1)) return;
    last_sweep_ = now;
    for (auto it = clients_.begin(); it != clients_.end();) {
      if (it->second.active == 0 && now - it->second.window_start >= std::chrono::seconds(1))
        it = clients_.erase(it);
      else
        ++it;
    }
  }

  const size_t max_connections_;
  const size_t max_per_client_;
  const size_t new_per_second_;
  std::mutex mutex_;
  size_t total_ = 0;
  Clock::time_point last_sweep_{};
  std::map<std::array<uint8_t, 16>, Client> clients_;
};

} // namespace nprpc::impl
