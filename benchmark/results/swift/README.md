# Swift↔Swift NPRPC Benchmark Baselines

Results from `nprpc-swift-benchmark` (SwiftPM executable in `nprpc_swift/`).

Same IDL as the C++ suite: `benchmark/idl/nprpc_benchmark.npidl`.

## Record a baseline

Preferred (from repo root):

```bash
just run-swift-benchmarks-baseline
# optional: just run-swift-benchmarks-baseline -- --min-time 2
```

This regenerates Swift stubs, builds `libnprpc`, runs SHM benches, and writes:

- `baseline_<UTC-stamp>.json` / `.csv` (immutable snapshot)
- `baseline.json` / `baseline.csv` (latest, for quick diffs)

Manual run without writing files:

```bash
just run-swift-benchmarks
just run-swift-benchmarks -- --filter EmptyCall --min-time 2
```

Or from `nprpc_swift/` with `NPRPC_BUILD_DIR` set:

```bash
cd nprpc_swift
swift run -c release nprpc-swift-benchmark -- \
  --transport shm \
  --min-time 1.0 \
  --json ../benchmark/results/swift/baseline.json \
  --csv  ../benchmark/results/swift/baseline.csv
```

## Compare with C++

```bash
# C++ (Google Benchmark) — prefer SharedMemory filter first
./.build_relwith_debinfo/benchmark/nprpc_benchmarks \
  --benchmark_filter='Latency.*0' \
  --benchmark_format=json \
  --benchmark_out=benchmark/results/cpp_latency_shm.json
```

**Fairness notes**

- Swift client is always **async** (`send_receive_async`); C++ latency cases are mostly **sync**.
- Prefer **SharedMemory** when comparing bridge/marshal overhead (no kernel TCP stack).
- Record `commit`, date, and build flags in the JSON `meta` object.

## After an optimization

Re-run with a new name, e.g. `baseline_after_tx_zerocopy.json`, and diff `ns_per_op` / `ops_per_sec` on the same machine.
