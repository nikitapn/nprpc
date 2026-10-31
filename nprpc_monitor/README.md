# NPRPC Monitor

Native LavaUI console for process-local HTTP/3, WebSocket and WebTransport
telemetry. The UI consumes typed `Monitor.Watch` snapshots over NPRPC QUIC;
there is no HTTP polling. Server production follows stream credits and the
configured sampling interval. The console retains 120 samples per transport.

## Build and run

Requires the sibling `../../HelloWorld` LavaUI checkout and its Linux development
dependencies, Swift 6.3+, and a full NPRPC build with QUIC and HTTP/3 enabled.
The monitor links the full library, including SHM. The compositor and ordinary
Lava apps can continue to use their own SHM-only build.

From the repository root:

```sh
cmake --build .build_relwith_debinfo --target nprpc npidl -j4
export NPRPC_SWIFT_PATH="$PWD/nprpc_swift"
export NPRPC_BUILD_DIR=.build_relwith_debinfo
swift build --package-path nprpc_monitor/app --scratch-path nprpc_monitor/.build -j4
nprpc_monitor/run.sh
```

For a standalone server with HTTPS/HTTP3/WebSocket/WebTransport on 8443 and
QUIC telemetry on 8444:

```sh
swift run --package-path nprpc_monitor MonitorDemo certificate.pem key.pem
```

Paste its printed object reference into the console and click Connect.
Certificates must be appropriate for the advertised `localhost` hostname.
`MonitorProbe '<object-reference>'` prints three snapshots and cancels the
stream, allowing a headless end-to-end check.

For an existing server, telemetry is part of the C++ runtime. No monitor
package dependency or Swift servant is needed on the server.

```cpp
auto* rpc = nprpc::RpcBuilder()
    .with_hostname("localhost")
    .with_quic(8444).ssl("certificate.pem", "key.pem")
    .enable_telemetry(true, 250)
    .allow_anonymous_telemetry() // or .require_client_certificates(...), see below
    .build();
auto reference = rpc->telemetry_reference();
```

```swift
let rpc = try RpcBuilder().withHostname("localhost")
    .withQuic(8444).ssl(certFile: "certificate.pem", keyFile: "key.pem")
    .enableTelemetry(minIntervalMs: 250)
    .allowAnonymousTelemetry() // or .requireClientCertificates(...), see below
    .build()
let reference = rpc.telemetryReference!
```

Telemetry is disabled by default. Enabling it requires a configured QUIC
listener. The minimum sampling interval must be between 250 and 10,000 ms;
Watch requests are clamped to that minimum through 10 seconds. Invalid builder
settings fail before the runtime starts. The reference is empty/nil when the
service is disabled. Enabling it also requires either client certificates or an
explicit `allow_anonymous_telemetry()`, which lets any QUIC peer holding the
reference read the counters. A process serves 32 Watch streams, at most 4 per
session; further Watch calls fail with `ExceptionBadAccess`.

The runtime owns the POA and servant. Samples are generated directly from C++
counters and paced by Asio timers without blocking an I/O thread. Stream credits
bound production when a reader falls behind. Cancellation removes the producer
and wakes its timer. There is no Swift service implementation or C telemetry API.

## Dependencies and runtime ownership

The client API package depends only on NPRPC Swift. The native executable in
`app/` also depends on LavaUI/LavaHost. The C++ runtime has no UI dependencies:

```
NPRPCMonitor app → MonitorAPI → NPRPC Swift → libnprpc (telemetry service)
                → LavaHost → LavaClient → NPRPC Swift
                → LavaUI
```

With `LAVA_CLIENT=1`, the app opens LavaHost first and borrows
`LavaHost.rpcRuntime`. It neither initializes another runtime nor stops the
borrowed one. Windowed mode owns its runtime. The accessor requires the
accompanying changes in the sibling HelloWorld checkout.

`run.sh` selects the full library for the entire process, including compositor
SHM calls and telemetry QUIC calls, and puts it first in `LD_LIBRARY_PATH`.
The separate compositor process can retain its SHM-only build. The app manifest
rejects a SHM-only build early. For a direct SwiftPM invocation, set
`NPRPC_SWIFT_PATH` and `NPRPC_BUILD_DIR` as shown above.

```sh
LAVA_CLIENT=1 nprpc_monitor/run.sh
```

## QUIC authentication

Native QUIC clients now verify server certificates and DNS/IP identity by
default. Public servers use system trust. Private/self-signed deployments must
explicitly supply a trust bundle; there is no implicit development bypass.
The existing HTTP/WSS verification-disable setting does not disable QUIC checks.

Enable mTLS on the server's QUIC listener:

```cpp
auto* rpc = nprpc::RpcBuilder().with_hostname("localhost")
    .with_quic(8444).ssl("server.pem", "server.key")
    .require_client_certificates("trusted-clients.pem")
    .enable_telemetry().build();
```

```swift
let rpc = try RpcBuilder().withHostname("localhost")
    .withQuic(8444).ssl(certFile: "server.pem", keyFile: "server.key")
    .requireClientCertificates(caFile: "trusted-clients.pem")
    .enableTelemetry().build()
```

`trusted-clients.pem` can contain a private CA or explicitly trusted self-signed
client certificates. mTLS is opt-in and applies to the entire native QUIC
listener. Telemetry dispatch also rejects other transports, so knowing its
object ID cannot bypass mTLS through a TCP/WS/HTTP listener. Servants can inspect
`get_context().quic_client_authenticated` for inbound native QUIC client
certificate authentication. Trust bundles restrict trust to their contents. A
trusted CA authorizes the valid client certificates it issues; explicitly trusted
self-signed leaf certificates provide a per-device allowlist. Certificates must be
valid for their role (`serverAuth` / `clientAuth`) and within their validity
period; the server certificate must match the hostname/IP in the reference.
This implementation targets the MsQuic OpenSSL backend used on Linux.

Configure the client before opening a remote QUIC connection:

```cpp
auto* rpc = nprpc::RpcBuilder()
    .quic_client_tls("server-ca.pem", "operator.pem", "operator.key").build();
// For an already-running/borrowed runtime:
rpc->configure_quic_client_tls({"server-ca.pem", "operator.pem", "operator.key"});
```

```swift
let rpc = try RpcBuilder()
    .quicClientTls(caFile: "server-ca.pem", certFile: "operator.pem", keyFile: "operator.key")
    .build()
// For a borrowed LavaHost runtime:
try rpc.configureQuicClientTls(caFile: "server-ca.pem", certFile: "operator.pem", keyFile: "operator.key")
```

An empty client CA path uses system trust. Certificate/key must be supplied
together. Runtime changes are validated before replacing the current settings
and apply only to NEW connections. Established/cached sessions retain their
identity; reconnect/restart to apply a different identity or revoke an active
session. Server certificate reload retains the client-auth requirement and
reloads the configured trust bundle; existing connections remain established.

The native monitor accepts these file paths through its environment, including
when borrowing the LavaHost runtime:

```sh
NPRPC_QUIC_CA_FILE=server-ca.pem \
NPRPC_QUIC_CLIENT_CERT_FILE=operator.pem \
NPRPC_QUIC_CLIENT_KEY_FILE=operator.key \
LAVA_CLIENT=1 nprpc_monitor/run.sh
```

The command-line fixtures accept them explicitly:

```sh
swift run --package-path nprpc_monitor MonitorDemo server.pem server.key trusted-clients.pem
swift run --package-path nprpc_monitor MonitorProbe '<reference>' server-ca.pem operator.pem operator.key
```

The `QuicTLS` CTest fixture generates temporary certificates and checks trusted,
untrusted, expired, and missing identities, hostname mismatch, certificate
purpose, explicitly trusted self-signed clients, live runtime configuration,
retention of mTLS policy after server certificate reload, and denial of
telemetry access through an alternate transport. It requires
Python's `cryptography` package.

## Measurements

* HTTP/3 counts responses when headers are prepared, including cached files
  and 304 responses. Errors are HTTP 5xx. Latency starts at stream creation and
  ends at response preparation; it includes request receipt, not response
  delivery. Bytes are request bodies as received and planned response bodies;
  HEAD responses count no sent bytes.
* WS and WT count FunctionCall and StreamInitialization dispatches. Errors
  are RPC exception/error replies. Latency is synchronous dispatch duration;
  bytes are RPC request/reply frames. Stream data/control frames and object
  reference bookkeeping are excluded. These are not whole-session byte totals.
* CONNECT requests are excluded from HTTP/3 counts so WT sessions do not look
  like ordinary page requests. Counters aggregate the process's sessions;
  WebSocket includes incoming calls on client sessions as well as servers.
* QUIC counts native NPRPC calls, including the monitor's own Watch.
* Handshake failures count inbound connections that ended before the handshake
  finished, whatever the cause: a rejected certificate, or a client that gave
  up. HTTP/3 and QUIC count them per connection, and HTTP/3's are recorded when
  the connection is destroyed, so they can lag by its idle timeout. WebSocket
  counts failed upgrades. WebTransport has no handshake of its own.
* Throttled counts requests, connections and stream opens refused by a rate or
  session limit, on the transport they arrived on.
* HTTP/1.1 reports only TLS handshake failures and throttled RPC requests, as
  process totals, because a TLS failure happens before a connection can become
  a WebSocket.
* Active connections and streams are current values, not cumulative, and cover
  inbound sessions only. An HTTP/3 connection counts once its handshake
  completes. HTTP/3 has no NPRPC streams, so its stream count stays 0. Each
  direction of a bidi stream counts separately.
* Rates are deltas of cumulative counters divided by server monotonic elapsed
  time. First samples establish a baseline. Charts scale independently.

This first version does not yet expose stream throughput/backpressure, latency percentiles, per-method breakdowns,
logs, alerts, or persistent history. It does not claim end-to-end latency.

## Framework exercise

The UI exercises Observation/@Bindable, editable connection state, repeated
connect/disconnect, cross-thread updates through MainQueue, ForEach identity,
and Canvas charts. No changes to LavaUI itself are needed.

The native app still resolves LavaUI's unrelated desktop-app packages (including
KDBXKit); the service package no longer does. LavaUI's NPRPC auto-discovery can
select another clone;
`run.sh` explicitly selects this checkout to prevent that ambiguity.

Regenerate the API after IDL changes:

```sh
.build_relwith_debinfo/npidl/npidl --swift idl/nprpc_monitor.npidl \
  --output-dir nprpc_monitor/Sources/MonitorAPI
```

Counter concurrency test:

```sh
ctest --test-dir .build_relwith_debinfo/test -R telemetry_counters
```

## Verification performed

Built libnprpc with HTTP/3/QUIC and all three Swift executables. Ran the
80,000-update concurrent counter test. Against MonitorDemo, fetched the bundled
page with curl `--http3-only`, sent an invalid-object RPC over WSS and browser
WebTransport, and asserted the resulting per-transport counters through the
QUIC MonitorProbe stream. Each WS/WT error added one completed operation, one
error, 32 received bytes and 16 response bytes. The browser test used the test
certificate's SHA-256 pin, as the existing WebTransport tests do.

Rendered and inspected the native 1120×720 window in disconnected and live
states, exercised Connect/Disconnect, and observed an HTTP/3 traffic burst on
the chart. The demo uses one HTTP/3 worker to avoid privileged reuseport BPF.

Runtime integration rechecked with `LAVA_CLIENT=1`: opened a compositor SHM
surface using the full library, connected to MonitorDemo over QUIC, received
live samples, and rendered HTTP/3 counters. `/proc/<pid>/maps` showed exactly
one libnprpc, from `.build_relwith_debinfo`. The app manifest was also checked
against `.build_lava_shm` and rejected it with the expected diagnostic. The
service-only dependency graph contains NPRPC and swift-mustache, with no LavaUI.

After moving the service to C++, rechecked default-off behavior, rejected
builder settings, and three repeated QUIC subscriptions from the Swift probe.
Each subscription received three samples separated by 250–252 ms and then
cancelled. `stream_pacing` checks that credits bound production, pacing spaces
chunks, and cancellation releases a manager parked on a long sampling timer.
`telemetry_builder` checks disabled and invalid configurations.

```sh
cmake --build .build_relwith_debinfo --target test_stream_pacing test_telemetry_builder -j4
ctest --test-dir .build_relwith_debinfo -R '^(stream_pacing|telemetry_builder)$' --output-on-failure
```

QUIC security verification: 19 real handshake/RPC cases passed in `QuicTLS`,
including DNS/IP matching, expired/untrusted/missing identities, key mismatch,
restricted trust, mTLS policy after reload, and denied TCP access to telemetry.
The Swift MonitorDemo/MonitorProbe also completed mTLS with a temporary
self-signed operator certificate and received three C++ telemetry snapshots.
