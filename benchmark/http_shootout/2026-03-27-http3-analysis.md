# HTTP/3 Analysis - 2026-03-27

## Environment

- Host: AMD Ryzen 7 5800H, 8C/16T, 38.54 GiB RAM
- Kernel: Linux 6.19.6-arch1-1
- Clocksource: `tsc`
- CPU governor: `powersave`
- h2load: `nghttp2/1.68.90`
- Benchmark scope: HTTP/3 only, payloads `1kb`, `64kb`, `1mb`

## Result Summary

- `1kb`: NPRPC is clearly ahead. `242k req/s` vs nginx `164k req/s` and caddy `84k req/s`.
- `64kb`: NPRPC drops behind nginx. `19.0k req/s` vs nginx `22.9k req/s`.
- `1mb`: NPRPC is still behind nginx on bulk transfer. `1138 req/s` vs nginx `1408 req/s`.
- There were no HTTP errors or packet loss in the benchmark output, so this is a pure efficiency gap rather than a correctness issue.
- The TSC switch helped materially. Clock reading is no longer the dominant cost.

## What The Results Mean

- Small-response performance is already very good. The current architecture is competitive, and better than nginx, when request/response overhead dominates.
- The remaining weakness is large-body HTTP/3 throughput. As payload size grows, nginx pulls ahead, which points to per-packet, per-frame, and per-ACK overhead rather than accept path, routing, or connection setup.
- Caddy is substantially slower in this setup, so the meaningful comparison target is nginx.

## Perf Interpretation

The attached `1mb` perf run is consistent with the throughput tables.

- The old clocksource bottleneck is gone. `clock_gettime`, `std::chrono::steady_clock::now()`, and `timestamp_ns()` are now negligible.
- The hottest user-space path is QUIC packet construction and send preparation:
  - `conn_write_pkt`
  - `ngtcp2_conn_write_vmsg`
  - `nghttp3_stream_writev`
  - `conn_writev_stream`
  - `ngtcp2_pkt_encode_stream_frame`
- ACK handling is still significant:
  - `ngtcp2_rtb_recv_ack`
  - `nghttp3_conn_add_ack_offset`
  - related retransmission / range-tree maintenance
- Kernel-side send overhead is still real, but no longer dominant:
  - `clear_page_erms`
  - `_copy_from_iter`
  - `udp_sendmsg` / `__ip_append_data`
- Crypto is present but not the main limiter. OpenSSL AES-GCM work shows up, but the profile is not saying “crypto first”.
- Timer churn exists, but it is no longer an important blocker. `schedule_timer()` and timerfd-related paths are too small to be first-priority targets.
- GRO/GSO looks healthy enough to keep. The h2load output shows non-trivial GRO batching, so the current packet aggregation path is not obviously broken.

## Highest-Value Next Optimizations

### 1. Reduce packet count for large responses

This is the highest-value direction.

- The profile says large transfers are paying heavily for QUIC packet generation and ACK processing.
- Fewer packets means less `conn_write_pkt`, fewer ACK ranges, less frame encoding, and less kernel skb/page work.
- First things to validate:
  - whether `MAX_UDP_PAYLOAD_SIZE = 1350` is leaving throughput on the table for this benchmark environment
  - whether GSO segment size can be safely increased for local benchmarking
  - whether the current write loop emits smaller STREAM payloads than necessary under flow-control limits

For localhost benchmarking specifically, trying a larger payload ceiling is justified because path MTU risk is low.

### 2. Cut ACK-processing overhead indirectly by sending fewer, larger packets

- `ngtcp2_rtb_recv_ack` at roughly `3%` is large enough to matter.
- That is unlikely to be solved well by micro-optimizing the ACK tree code in your codebase, because most of it lives in ngtcp2 internals.
- The more pragmatic win is to reduce how many packets must be acknowledged in the first place.
- If ngtcp2 plus h2load support `ACK_FREQUENCY`, it is worth testing later, but packet-count reduction is still the more general fix.

### 3. Keep the large-body write path busy for longer per wake-up

- The stack still spends noticeable time bouncing through `on_write()`, `write_pkt()`, nghttp3 scheduling, and timer-driven re-entry.
- For large static bodies, the goal should be to drain as much application data as possible per write opportunity before falling back to another readiness cycle.
- Worth checking later:
  - whether `http_read_data_cb` or body-source logic is fragmenting the response into smaller pieces than needed
  - whether the current chunk size policy for large bodies is too conservative
  - whether stream rescheduling causes avoidable queue churn in nghttp3

### 4. Revisit kernel send-path overhead only after packet count is reduced

- `clear_page_erms` and `_copy_from_iter` show that skb/page allocation and copy cost are still visible.
- But the kernel path is now secondary to QUIC packet generation.
- Do not jump straight to `io_uring`, `MSG_ZEROCOPY`, or custom send plumbing yet.
- The right sequence is:
  - first reduce packet volume
  - then reprofile
  - only then decide whether kernel send path is the next real blocker

### 5. Remove environment-imposed frequency limits for future peak runs

- This run used the `powersave` governor on `amd-pstate-epp`.
- That does not invalidate the comparison, but it likely suppresses absolute peak throughput.
- For future “how far can this go?” runs, test again with a performance-oriented governor or EPP setting.

## Lower-Priority Items

- Timestamp helper workarounds are not a priority anymore. They fixed a correctness and benchmarking issue, but they are not where the CPU time is going now.
- Timer scheduling cleanup is not a first-order win at current profile shape.
- Connection lookup and socket steering are not current bottlenecks.
- Crypto tuning is not the first move unless a later profile shows packet/ACK overhead has already been reduced.

## Suggested Next Experiments

1. Benchmark larger QUIC payload / GSO segment sizes on localhost and compare `64kb` and `1mb` throughput.
2. Add one round of instrumentation for bytes-per-packet and packets-per-response on the `1mb` case.
3. Inspect the large-body path around `http_read_data_cb` and response chunking to make sure the stream stays productive per scheduler wake-up.
4. Re-run the same `1mb` perf capture after any packet-count reduction to see whether the bottleneck shifts into kernel send path or crypto.
5. Repeat the run once with a performance governor so the next profile is not frequency-limited by policy.

## Practical Conclusion

- The TSC change was worth it.
- NPRPC is already excellent on small HTTP/3 responses.
- The remaining gap to nginx is now a classic bulk-transfer QUIC efficiency problem: too much work per payload byte in packet generation, ACK handling, and the residual kernel send path.
- The next optimization pass should focus on sending fewer, fuller packets for large bodies rather than chasing timer or clock overhead.