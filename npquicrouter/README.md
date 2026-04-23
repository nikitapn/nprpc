# npquicrouter

A lightweight SNI-based reverse proxy that routes both TLS (TCP) and QUIC (UDP) connections to backend services based on the hostname in the TLS ClientHello — without terminating TLS itself.

## How It Works

```
Client                npquicrouter              Backend(s)
  │──── TCP 443 ────►│ peek TLS ClientHello     │
  │                  │ extract SNI              │
  │                  │──── TCP splice ─────────►│ backend:port
  │                  │                          │
  │──── UDP 443 ────►│ decrypt QUIC Initial     │
  │                  │ extract SNI              │
  │                  │──── UDP forward ────────►│ backend:port
```

**TCP path:** The proxy accepts a TCP connection, reads enough of the first TLS record to extract the SNI from the ClientHello (without decrypting the handshake), then opens a TCP connection to the matched backend and splices data bidirectionally.

**UDP/QUIC path:** The proxy receives UDP datagrams, decrypts QUIC v1 Initial packets using the publicly-known key material derived from the DCID (RFC 9001 §5 — these keys are not secret), reassembles the CRYPTO stream if the ClientHello spans multiple packets, extracts the SNI, and forwards all subsequent datagrams for that client to the matched backend.

Both paths route transparently without terminating TLS or QUIC — the backend performs the actual TLS/QUIC handshake with the client.

## Features

- **Dual-transport** — handles TCP (TLS) and UDP (QUIC) on the same port (typically 443)
- **SNI-based routing** — per-hostname routing table with a configurable default fallback
- **HTTP → HTTPS redirect** — optional plaintext HTTP listener that returns `301` to `https://`
- **QUIC multi-worker** — multiple UDP worker threads with `SO_REUSEPORT` for parallel packet processing
- **eBPF QUIC dispatcher** — optional `SO_ATTACH_REUSEPORT_EBPF` program that pins Initial packets by DCID hash and short-header packets by the worker ID embedded in the server's SCID; ensures all packets of a connection stay on the same worker
- **SHM fast path** — when co-deployed with NPRPC HTTP/3 backends on the same machine, packets are exchanged over per-backend shared-memory lock-free ring buffers instead of loopback UDP sockets, preserving GSO batch metadata across the double-hop
- **Zero external config format** — single JSON file parsed with [glaze](https://github.com/stephenberry/glaze)
- **No TLS library dependency** — SNI extraction is pure byte parsing; QUIC Initial decryption uses OpenSSL EVP directly (no ngtcp2)
- **Graceful shutdown** — `SIGINT`/`SIGTERM` drain all io_contexts and join worker threads cleanly

## Configuration

All settings live in a single JSON file passed as the sole command-line argument.

```json
{
  "listen_address":        "0.0.0.0",
  "listen_tcp_port":       443,
  "listen_udp_port":       443,
  "http_redirect_port":    80,
  "default_tcp_backend":   "127.0.0.1:8443",
  "default_udp_backend":   "127.0.0.1:4433",
  "udp_session_timeout_sec": 120,
  "shm_egress_channel":    "",
  "num_workers":           1,
  "routes": [
    {
      "sni":         "app.example.com",
      "tcp_backend": "127.0.0.1:8443",
      "udp_backend": "127.0.0.1:4433",
      "shm_ingress_channel": "app_quic"
    },
    {
      "sni":         "blog.example.com",
      "tcp_backend": "127.0.0.1:9443",
      "udp_backend": "127.0.0.1:4434"
    }
  ]
}
```

| Field | Type | Default | Description |
|---|---|---|---|
| `listen_address` | string | `"0.0.0.0"` | Address to bind both TCP and UDP listeners |
| `listen_tcp_port` | uint16 | `443` | TCP (TLS) listen port |
| `listen_udp_port` | uint16 | `443` | UDP (QUIC) listen port |
| `http_redirect_port` | uint16 | `0` | If non-zero, listens on this port and returns `301 → https://` for all requests. Set to `80` for HTTP→HTTPS redirect. |
| `default_tcp_backend` | string | `""` | Fallback TCP backend (`host:port`) when no route matches the SNI. If empty, unmatched connections are dropped. |
| `default_udp_backend` | string | `""` | Fallback UDP backend (`host:port`). Same rules as `default_tcp_backend`. |
| `udp_session_timeout_sec` | int | `120` | Idle timeout for UDP sessions. Sessions idle longer than this are garbage-collected. |
| `shm_egress_channel` | string | `""` | Shared-memory channel name for the egress (server→client) ring. See [SHM Fast Path](#shm-fast-path). Empty disables SHM egress. |
| `num_workers` | int | `1` | Number of UDP worker threads. Values > 1 require `SO_REUSEPORT` support (Linux 3.9+). With eBPF enabled, each worker gets its own socket and the BPF program routes packets deterministically. |
| `routes` | array | `[]` | Ordered list of SNI → backend mappings. Each entry has `sni`, and optionally `tcp_backend`, `udp_backend`, and `shm_ingress_channel`. |

Routes are matched by exact string comparison against the SNI. The first match wins. If no route matches, the `default_*_backend` is used; if that is also unset, the connection/datagram is dropped.

Route fields:

| Field | Description |
|---|---|
| `sni` | Hostname to match (exact string). |
| `tcp_backend` | TCP backend (`host:port`). Optional — omit if the route is UDP-only. |
| `udp_backend` | UDP/QUIC backend (`host:port`). Optional — omit if the route is TCP-only. |
| `shm_ingress_channel` | SHM ingress channel name for this UDP backend (see [SHM Fast Path](#shm-fast-path)). Empty or absent = UDP mode for this route. |

## Usage

```
npquicrouter [--debug] <config.json>
```

The optional `--debug` / `-d` flag enables verbose per-packet logging to stderr, including QUIC DCID values, CRYPTO stream offsets, and hex dumps of assembled ClientHello data.

## Multi-Worker UDP (SO_REUSEPORT)

Setting `num_workers > 1` creates that many UDP sockets bound to the same address/port with `SO_REUSEPORT`. Each socket runs its own `io_context` in a dedicated thread, allowing parallel packet processing across CPU cores.

### eBPF Routing (Optional)

When built with libbpf, clang, and xxd available, npquicrouter compiles and loads the `quic_reuseport_router.bpf.c` program into the kernel. This BPF program attaches to the `SO_REUSEPORT` group and implements QUIC-aware worker selection:

- **Long-header (Initial/Handshake) packets** — hashes the first 4 bytes of the DCID to select a worker, ensuring all fragments of a single connection's ClientHello land on the same worker.
- **Short-header (1-RTT) packets** — reads `data[1]` of the packet, which contains the worker ID embedded by the backend into the server's SCID (`cid->data[0]` in the HTTP/3 server). This guarantees post-handshake packets return to the same worker that handled the handshake.

Without the eBPF program, the kernel distributes packets using its own `SO_REUSEPORT` hash, which may spread packets from the same connection across multiple workers. The router handles this gracefully but with reduced efficiency.

The BPF program requires `cap_bpf` (or `CAP_SYS_ADMIN` on older kernels):

```bash
sudo setcap cap_net_bind_service,cap_bpf+ep /usr/local/bin/npquicrouter
```

At shutdown, npquicrouter prints per-worker packet distribution and drop counts read from the BPF PERCPU_ARRAY maps:

```
[BPF] Router packet distribution:
  worker[0]: 12840
  worker[1]: 13102
  worker[2]: 12998
  worker[3]: 13060
  total routed: 52000
  dropped:      0
```

## SHM Fast Path

When npquicrouter and one or more NPRPC HTTP/3 backends run on the same machine, configure SHM channels to replace loopback UDP sockets with shared-memory lock-free ring buffers.

Two ring types are used:

| Ring | Direction | SHM name | Configured by |
|---|---|---|---|
| c2s (ingress) | npquicrouter → backend | `/nprpc_<route.shm_ingress_channel>_c2s` | `shm_ingress_channel` per route |
| s2c (egress) | backend → npquicrouter | `/nprpc_<shm_egress_channel>_s2c` | `shm_egress_channel` at top level |

Each UDP route that names a `shm_ingress_channel` gets its own independent ingress ring, so multiple backends on the same machine are all supported simultaneously. Routes without a `shm_ingress_channel` fall back to the normal per-session UDP socket path.

The egress ring is shared across all backends: backends write packets tagged with the real client `sockaddr`, so npquicrouter's egress reader can call `sendmsg()` directly without any lookup.

All rings are created by npquicrouter worker 0 at startup (stale rings from a previous run are removed first). With multiple UDP workers, workers 1..N-1 open the existing ingress rings as additional MPSC producers (multiple worker threads → one backend consumer per ring). Only worker 0 drains the egress ring.

Egress frames carry a `gso_segment_size` field. When set, the egress reader issues `sendmsg()` with a `UDP_SEGMENT` cmsg, preserving the GSO batch assembled by the backend across the double-hop and avoiding per-packet syscall overhead.

Default ring sizes: 32 MiB each (configurable via `kShmIngressRingSize` / `kShmEgressRingSize` in `quic_shm_channel.hpp`).

### Example: two backends, both using SHM

```json
{
  "shm_egress_channel": "quic_edge",
  "routes": [
    { "sni": "app.example.com", "udp_backend": "127.0.0.1:4433", "shm_ingress_channel": "app_quic" },
    { "sni": "api.example.com", "udp_backend": "127.0.0.1:4434", "shm_ingress_channel": "api_quic" }
  ]
}
```

This creates three rings:
- `/nprpc_app_quic_c2s` — ingress for the `app` backend
- `/nprpc_api_quic_c2s` — ingress for the `api` backend
- `/nprpc_quic_edge_s2c` — shared egress from both backends back to clients

## Building

### In-Tree (nprpc dev build)

npquicrouter is included in the standard nprpc build when the parent `CMakeLists.txt` adds it via `add_subdirectory`. No extra flags needed — it picks up in-tree sources automatically.

```bash
cmake -S . -B .build_release -DCMAKE_BUILD_TYPE=Release
cmake --build .build_release --target npquicrouter -j$(nproc)
```

The eBPF feature is detected automatically. It requires `clang`, `xxd`, and `libbpf` (dev headers):

```bash
# Debian/Ubuntu
sudo apt install clang xxd libbpf-dev
```

### Standalone / VPS Deployment (Docker)

The `build-vps.sh` script builds a static-ish `npquicrouter` binary inside a Debian 12 container and extracts it to `npquicrouter/npquicrouter-linux-amd64`:

```bash
# Build only
./npquicrouter/build-vps.sh

# Build and deploy to a remote host via SSH
./npquicrouter/build-vps.sh deploy@my-vps.example.com
```

The deploy path:
1. Copies the binary to `/usr/local/bin/npquicrouter`
2. Installs a systemd unit at `/etc/systemd/system/npquicrouter.service`
3. Applies `cap_net_bind_service,cap_bpf+ep` file capabilities so the process can bind port 443 and load BPF programs without running as root

You can also build the Docker image manually from the repo root:

```bash
docker build -f npquicrouter/Dockerfile.debian12 -t npquicrouter:debian12 .
```

The Dockerfile uses a two-stage build: the `builder` stage compiles with GCC + Ninja + Boost 1.89 headers; the `runtime` stage produces a minimal Debian 12 slim image containing only the binary and `libssl3`.

> **Note:** The standalone build does not include eBPF support (libbpf is not present in the Dockerfile). For eBPF multi-worker routing on a VPS, build in-tree on the target system or extend the Dockerfile to install `libbpf-dev` and `clang`.

## Systemd Service

The `build-vps.sh` deploy path installs the following unit:

```ini
[Unit]
Description=NPRPC QUIC/TLS Router
After=network.target

[Service]
User=www-data
ExecStart=/usr/local/bin/npquicrouter /etc/npquicrouter/config.json
Restart=on-failure
RestartSec=5

[Install]
WantedBy=multi-user.target
```

Create `/etc/npquicrouter/config.json` with your routing config, then:

```bash
sudo systemctl daemon-reload
sudo systemctl enable --now npquicrouter
```

After each binary update, re-apply capabilities (they are cleared on file replacement):

```bash
sudo setcap cap_net_bind_service,cap_bpf+ep /usr/local/bin/npquicrouter
sudo systemctl restart npquicrouter
```

## Dependencies

| Dependency | Required | Notes |
|---|---|---|
| Boost.Asio | Yes | Header-only; any modern version |
| OpenSSL | Yes | QUIC Initial key derivation (HKDF) and AES-128 header protection removal |
| libbpf | Optional | eBPF multi-worker routing; detected via pkg-config |
| clang | Optional | Compiles `quic_reuseport_router.bpf.c` at build time |
| xxd | Optional | Embeds the BPF object as a C header |
| [glaze](https://github.com/stephenberry/glaze) | Yes | JSON config parsing; included as a git submodule under `third_party/glaze` |

## Source Layout

```
npquicrouter/
├── src/
│   ├── main.cpp                      — Config, routing tables, TCP/UDP routers, main()
│   ├── quic_initial.hpp              — QUIC v1 Initial packet decryption (RFC 9001 §5)
│   ├── sni_parser.hpp                — TLS ClientHello SNI extraction (TCP + QUIC paths)
│   ├── quic_shm_channel.hpp          — SHM ring channel (ShmIngressWriter / ShmEgressReader)
│   └── quic_reuseport_router.bpf.c   — eBPF SO_REUSEPORT dispatcher
├── CMakeLists.txt                    — Build system; handles in-tree and standalone builds
├── Dockerfile.debian12               — Multi-stage Docker build for VPS deployment
└── build-vps.sh                      — Build + optional SSH deploy script
```
