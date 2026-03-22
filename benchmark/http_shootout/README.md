# HTTP Server Shootout

This harness compares NPRPC's built-in HTTP stack against well-established
servers under the same static-file workload.

Current comparison set:

- NPRPC
- nginx
- Caddy

Protocols covered:

- HTTP/1.1 over HTTPS via `oha`
- HTTP/3 via `h2load`

Why this exists:

- The in-tree Google Benchmark suite measures NPRPC RPC overhead well.
- It does not answer "how does our HTTP server compare to production web
  servers for static file serving under load?"
- This shootout gives a reproducible transport/server baseline using the same
  files and roughly the same TLS setup.

## Requirements

- `oha`
- `h2load`, or build the vendored copy with:

```bash
./benchmark/http_shootout/build_h2load.sh
```

- Built certs in [certs/out/localhost.crt](../../certs/out/localhost.crt) and
  [certs/out/localhost.key](../../certs/out/localhost.key)
- `nginx` and `caddy` are preferred when installed locally
- Docker is only needed as a fallback when the system binaries are not present

## Run

From the repository root:

```bash
./benchmark/http_shootout/build_h2load.sh
./benchmark/http_shootout/run_server_shootout.sh run
./benchmark/http_shootout/run_server_shootout.sh view
```

Results are written under:

[benchmark/http_shootout/results](../http_shootout/results)

## What it measures

Shared static assets are generated once and mounted into every server:

- `1kb.bin`
- `64kb.bin`
- `1mb.bin`
- `index.html`

The binary fixtures are deterministic pseudo-random bytes, not repeated
characters, so large-file comparisons are not accidentally dominated by
compression-friendly input.

The default suite currently runs:

- HTTP/1.1 HTTPS on `1kb.bin`
- HTTP/1.1 HTTPS on `1mb.bin`
- HTTP/3 on `1kb.bin`
- HTTP/3 on `1mb.bin`

If the active host nginx binary is built with `--with-http_v3_module`, nginx is
included in the HTTP/3 cases too. Docker fallback mode does not currently
assume nginx QUIC support.

The outputs are intentionally left in the native tool formats:

- `oha` JSON files
- `h2load` text summaries

That keeps the harness simple and avoids baking in a fragile parser.

For a quick terminal summary of the generated result files, run:

```bash
./benchmark/http_shootout/run_server_shootout.sh view
```

By default, the runner looks for `h2load` at:

- [third_party/nghttp2/build-h2load/src/h2load](../../third_party/nghttp2/build-h2load/src/h2load)

You can override that with `H2LOAD_BIN=/path/to/h2load`.

By default, the harness prefers host-installed `nginx` and `caddy` binaries for
fairer comparisons. If either binary is missing, it falls back to Docker for
that server only.

You can force the execution mode with:

```bash
NGINX_MODE=system|docker
CADDY_MODE=system|docker
```

The vendored `h2load` in this repo uses HTTP/3 via:

```bash
--alpn-list=h3
```

## Caveats

- nginx is tuned more aggressively than before and now runs directly from the
  system binary when available.
- nginx HTTP/3 is only exercised when the active nginx binary exposes
  `http_v3_module`.
- Caddy is used for both HTTP/1.1 and HTTP/3 because it supports HTTP/3 out of
  the box and now also runs directly from the system binary when available.
- NPRPC uses the benchmark server with `NPRPC_HTTP_ROOT_DIR` pointed at the
  shared asset directory and `NPRPC_BENCH_ENABLE_HTTP3=1`.
- This is a transport/server benchmark, not an RPC framework benchmark.

## Recommended interpretation

Track at least:

- throughput
- p50 latency
- p95 latency
- p99 latency
- error rate
- CPU utilization

For serious regression tracking, run each case multiple times on an otherwise
idle machine and pin CPU frequency/governor if possible.