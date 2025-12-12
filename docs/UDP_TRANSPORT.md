# UDP Transport for Game Networking

NPRPC supports UDP transport for low-latency, high-frequency communication typical in game networking scenarios.

## Overview

UDP transport provides:
- **Fire-and-forget semantics**: Send without waiting for response (~76µs latency)
- **Reliable mode**: ACK-based delivery with automatic retransmission
- **Connection caching**: Reuse connections for high throughput (~39k calls/sec)
- **Message fragmentation**: Automatic handling of messages exceeding MTU

## IDL Syntax

Mark interfaces with `[udp]` to enable UDP transport:

```cpp
namespace game;

[udp]
interface GameUpdates {
  // Fire-and-forget (default for UDP)
  void PlayerPosition(f32 x, f32 y, f32 z);
  void PlayerRotation(f32 yaw, f32 pitch);
  void AnimationState(u32 state_id);
  
  // Reliable delivery with ACK
  [reliable]
  void PlayerDied(u32 killer_id, u32 victim_id);
  
  [reliable]
  void ItemPickup(u32 player_id, u32 item_id);
}
```

## Performance Characteristics

| Metric | Value | Notes |
|--------|-------|-------|
| Fire-and-forget latency | ~76µs | Same machine |
| Reliable call latency | ~150µs | Includes ACK round-trip |
| Throughput | ~39k calls/sec | With connection caching |
| Max payload (no fragmentation) | 1400 bytes | SAFE_MTU_PAYLOAD |
| Max payload (with IP fragmentation) | 64KB | 45 fragments max |

## Server Setup

```cpp
#include <nprpc/nprpc.hpp>

class GameUpdatesImpl : public game::IGameUpdates_Servant {
public:
  void PlayerPosition(float x, float y, float z) override {
    // Handle position update
  }
  
  void PlayerDied(uint32_t killer_id, uint32_t victim_id) override {
    // Handle player death - this was delivered reliably
  }
};

int main() {
  boost::asio::io_context io;
  
  nprpc::Config cfg;
  cfg.udp_port = 50000;  // Listen for UDP on port 50000
  cfg.debug_level = nprpc::LogLevel::Error;
  
  auto rpc = nprpc::RPC::create(io, cfg);
  auto poa = rpc->create_poa(1);
  
  auto servant = std::make_unique<GameUpdatesImpl>();
  auto oid = poa->activate_object(std::move(servant));
  
  io.run();
}
```

## Client Usage

```cpp
#include <nprpc/nprpc.hpp>
#include "game.hpp"  // Generated from IDL

int main() {
  boost::asio::io_context io;
  auto rpc = nprpc::RPC::create(io, {});
  
  // Get object reference via nameserver or construct directly
  nprpc::ObjectRef ref;
  ref.urls.push_back("udp://server:50000");  // UDP endpoint
  ref.poa_id = 1;
  ref.object_id = ...;
  
  auto game = nprpc::ObjectPtr<game::GameUpdates>(ref, rpc.get());
  
  // Fire-and-forget - returns immediately
  game->PlayerPosition(10.0f, 20.0f, 30.0f);
  
  // Reliable - blocks until ACK received
  game->PlayerDied(killer_id, victim_id);
}
```

## Design Decisions

### Why UDP?

For game networking, TCP's head-of-line blocking and retransmission delays are problematic:
- Old position updates become stale while waiting for retransmit
- Connection reset on packet loss disrupts gameplay
- TCP's reliability is overkill for high-frequency updates

UDP allows:
- Sending the latest state without waiting
- Application-level control over which messages need reliability
- Lower latency without connection overhead

### Reliable Mode

The `[reliable]` attribute adds:
1. Message ID tracking
2. ACK packets from receiver
3. Automatic retransmission on timeout (100ms default, 5 retries)

This is simpler than TCP but sufficient for important game events.

### Message Fragmentation
 - Message Fragmentation is not supported in this version
 - Maxinum payload size is limited to 1400 bytes to avoid fragmentation issues. Theoretically, can be increased to MAX_DATAGRAM_SIZE (64KB) but not recommended.

**Note**: For large messages, consider using TCP or the upcoming QUIC transport.

## Limitations

1. **No encryption**: UDP payload is not encrypted (use VPN or application-layer encryption)
2. **No congestion control**: Application must implement rate limiting
3. **Fragment limits**: Very large messages may timeout on reassembly
4. **Connectionless**: No session state between calls

## Future: QUIC Transport

QUIC will provide the best of both worlds:
- Reliable streams for RPC (like TCP)
- Unreliable datagrams for fire-and-forget (like UDP)
- Built-in encryption (TLS 1.3)
- Congestion control
- 0-RTT connection establishment

See TODO.md for QUIC integration roadmap.
