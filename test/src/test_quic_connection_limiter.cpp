#ifdef NDEBUG
#undef NDEBUG
#endif
#include <nprpc/impl/quic_connection_limiter.hpp>
#include <cassert>
#include <vector>

using nprpc::impl::QuicConnectionLimiter;
using boost::asio::ip::make_address;
using namespace std::chrono_literals;

int main() {
  const auto t = QuicConnectionLimiter::Clock::now();
  const auto a = make_address("192.0.2.1"), b = make_address("192.0.2.2");
  {
    auto limiter = std::make_shared<QuicConnectionLimiter>(0, 2, 0);
    auto s1 = limiter->acquire(a, t), s2 = limiter->acquire(a, t);
    assert(s1 && s2 && !limiter->acquire(a, t)); // per client
    assert(limiter->acquire(b, t));              // others are unaffected
    s1.reset();
    assert(limiter->acquire(a, t));              // a slot frees on close
    // The same address written as IPv4-mapped is the same client.
    auto limiter2 = std::make_shared<QuicConnectionLimiter>(0, 1, 0);
    auto s3 = limiter2->acquire(a, t);
    assert(s3 && !limiter2->acquire(make_address("::ffff:192.0.2.1"), t));
  }
  {
    auto limiter = std::make_shared<QuicConnectionLimiter>(2, 0, 0);
    auto s1 = limiter->acquire(a, t), s2 = limiter->acquire(b, t);
    assert(s1 && s2 && !limiter->acquire(make_address("192.0.2.3"), t)); // overall
    s2.reset();
    assert(limiter->acquire(make_address("192.0.2.3"), t));
  }
  {
    // Rate: closing connections doesn't refill it, a new second does.
    auto limiter = std::make_shared<QuicConnectionLimiter>(0, 0, 2);
    assert(limiter->acquire(a, t));
    assert(limiter->acquire(a, t));
    assert(!limiter->acquire(a, t));
    assert(limiter->acquire(b, t));
    assert(limiter->acquire(a, t + 1s));
  }
  {
    // One IPv6 /64 is one client, and another /64 isn't.
    auto limiter = std::make_shared<QuicConnectionLimiter>(0, 1, 0);
    auto s1 = limiter->acquire(make_address("2001:db8:0:1::1"), t);
    assert(s1 && !limiter->acquire(make_address("2001:db8:0:1::2"), t));
    assert(limiter->acquire(make_address("2001:db8:0:2::1"), t));
  }
  {
    // Idle clients are forgotten, and ones with connections aren't.
    auto limiter = std::make_shared<QuicConnectionLimiter>(0, 0, 0);
    auto keep = limiter->acquire(a, t);
    limiter->acquire(b, t).reset();
    assert(limiter->tracked_clients() == 2);
    auto other = limiter->acquire(make_address("192.0.2.3"), t + 2s);
    assert(limiter->tracked_clients() == 2); // b went, a stayed, the new one came
    assert(keep);
    // A slot outlives the limiter's owner safely.
    limiter.reset();
  }
}
