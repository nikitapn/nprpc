#!/usr/bin/env python3

from __future__ import annotations

import json
import re
import sys
from pathlib import Path


def fmt_seconds_as_ms(value: float | None) -> str:
    if value is None:
        return "-"
    return f"{value * 1000:.3f}"


def fmt_float(value: float | None, digits: int = 2) -> str:
    if value is None:
        return "-"
    return f"{value:.{digits}f}"


def infer_expected_payload_bytes(path: Path) -> int | None:
    """Parse payload size from filenames like 1kb, 64kb, 256kb, 1mb, etc."""
    m = re.search(r"(\d+)(kb|mb)", path.name.lower())
    if not m:
        return None
    n, unit = int(m.group(1)), m.group(2)
    return n * 1024 if unit == "kb" else n * 1024 * 1024


def _dur_to_ms(val: str, unit: str) -> float:
    """Convert h2load duration string to milliseconds."""
    return float(val) / 1000.0 if unit == "us" else float(val)


def _infer_protocol(name: str) -> str:
    """Return 'http1', 'http3', etc. as found in the filename, or 'unknown'."""
    m = re.search(r"http(\d+)", name.lower())
    return f"http{m.group(1)}" if m else "unknown"


def parse_oha(path: Path) -> dict[str, str | float | None]:
    data = json.loads(path.read_text())
    summary = data.get("summary", {})
    latency = data.get("latencyPercentiles", {})
    status = data.get("statusCodeDistribution", {})
    errors = data.get("errorDistribution", {})
    total_requests = sum(int(v) for v in status.values()) + sum(int(v) for v in errors.values())
    payload_bytes = infer_expected_payload_bytes(path)

    rps_raw: float | None = summary.get("requestsPerSec")
    return {
        "tool": "oha",
        "name": path.name,
        "payload_bytes": payload_bytes or 0,
        "protocol": _infer_protocol(path.name),
        "rps_raw": rps_raw,
        "rps": fmt_float(rps_raw),
        "avg_ms": fmt_seconds_as_ms(summary.get("average")),
        "p50_ms": fmt_seconds_as_ms(latency.get("p50")),
        "p95_ms": fmt_seconds_as_ms(latency.get("p95")),
        "p99_ms": fmt_seconds_as_ms(latency.get("p99")),
        "success": fmt_float(summary.get("successRate", 0.0) * 100.0),
        "requests": str(total_requests),
    }


def parse_h2load(path: Path) -> dict[str, str | float | None]:
    text = path.read_text()

    req_per_sec = None
    for pattern in [
        r"requests:\s+\d+ total,\s+\d+ started,\s+\d+ done,\s+\d+ succeeded,\s+\d+ failed,\s+([0-9.]+) req/s",
        r"([0-9.]+) req/s",
    ]:
        match = re.search(pattern, text)
        if match:
            req_per_sec = float(match.group(1))
            break

    # The request row format: min max median p95 p99 mean sd +/-sd
    # Values may be in ms or us.
    _DUR = r"([\d.]+)(ms|us)"
    request_row = re.search(
        r"request\s*:" + (r"\s+" + _DUR) * 6,
        text,
    )

    avg_ms: float | None = None
    p50_ms: float | None = None
    p95_ms: float | None = None
    p99_ms: float | None = None
    if request_row:
        # groups (1,2)=min (3,4)=max (5,6)=median (7,8)=p95 (9,10)=p99 (11,12)=mean
        p50_ms = _dur_to_ms(request_row.group(5), request_row.group(6))
        p95_ms = _dur_to_ms(request_row.group(7), request_row.group(8))
        p99_ms = _dur_to_ms(request_row.group(9), request_row.group(10))
        avg_ms = _dur_to_ms(request_row.group(11), request_row.group(12))
    else:
        latency_match = re.search(
            r"time for request:\s+([0-9.]+)ms \[mean\],\s+([0-9.]+)ms \[sd\]",
            text,
        )
        if latency_match:
            avg_ms = float(latency_match.group(1))

    status_match = re.search(r"status codes:\s+(\d+) 2xx,\s+(\d+) 3xx,\s+(\d+) 4xx,\s+(\d+) 5xx", text)
    data_match = re.search(r"traffic:\s+.+?,\s+.+?,\s+([^\s]+)\s*\((\d+)\) data", text)

    ok = int(status_match.group(1)) if status_match else 0
    redirects = int(status_match.group(2)) if status_match else 0
    client_err = int(status_match.group(3)) if status_match else 0
    server_err = int(status_match.group(4)) if status_match else 0
    total = ok + redirects + client_err + server_err
    success_pct = (ok / total * 100.0) if total else None
    transferred_data = int(data_match.group(2)) if data_match else None
    expected_payload = infer_expected_payload_bytes(path)

    note = ""
    if transferred_data is not None and expected_payload is not None and total:
        expected_total = expected_payload * total
        if transferred_data == 0:
            note = "invalid: 0B body"
        elif transferred_data < expected_total // 2:
            ratio = transferred_data / expected_total
            note = f"suspicious: {ratio:.1%} body"

    return {
        "tool": "h2load",
        "name": path.name,
        "payload_bytes": expected_payload or 0,
        "protocol": _infer_protocol(path.name),
        "rps_raw": req_per_sec,
        "rps": fmt_float(req_per_sec),
        "avg_ms": fmt_float(avg_ms, 3),
        "p50_ms": fmt_float(p50_ms, 3),
        "p95_ms": fmt_float(p95_ms, 3),
        "p99_ms": fmt_float(p99_ms, 3),
        "success": fmt_float(success_pct),
        "requests": str(total) if total else "-",
        "note": note,
    }


def annotate_vs_nprpc(rows: list[dict]) -> None:
    """Set 'vs_nprpc' on each non-nprpc row, comparing RPS to the nprpc baseline of the same protocol."""
    nprpc_rps: dict[str, float] = {}
    for row in rows:
        if "nprpc" in row["name"].lower() and row["rps_raw"] is not None:
            nprpc_rps[row["protocol"]] = row["rps_raw"]

    for row in rows:
        if "nprpc" in row["name"].lower():
            row["vs_nprpc"] = "baseline"
            continue
        baseline = nprpc_rps.get(row["protocol"])
        if baseline is None or row["rps_raw"] is None or baseline == 0:
            row["vs_nprpc"] = "-"
            continue
        ratio = row["rps_raw"] / baseline
        pct = (ratio - 1.0) * 100.0
        sign = "+" if pct >= 0 else ""
        row["vs_nprpc"] = f"{sign}{pct:.0f}%"


def print_table(rows: list[dict]) -> None:
    if not rows:
        print("No result files found.")
        return

    headers = ["file", "tool", "req", "rps", "vs_nprpc", "avg_ms", "p50_ms", "p95_ms", "p99_ms", "success_%", "note"]
    body = [
        [
            row["name"],
            row["tool"],
            row["requests"],
            row["rps"],
            row.get("vs_nprpc", "-"),
            row["avg_ms"],
            row["p50_ms"],
            row["p95_ms"],
            row["p99_ms"],
            row["success"],
            row.get("note", ""),
        ]
        for row in rows
    ]

    widths = [len(header) for header in headers]
    for row in body:
        for index, value in enumerate(row):
            widths[index] = max(widths[index], len(value))

    def render(values: list[str]) -> str:
        return "  ".join(value.ljust(widths[index]) for index, value in enumerate(values))

    print(render(headers))
    print(render(["-" * width for width in widths]))
    for row in body:
        print(render(row))


def _row_sort_key(row: dict) -> tuple:
    """Sort by payload size, then nprpc first, then lexicographic name."""
    nprpc_first = 0 if "nprpc" in row["name"].lower() else 1
    return (row["payload_bytes"], nprpc_first, row["name"])


def main() -> int:
    results_dir = Path(sys.argv[1]) if len(sys.argv) > 1 else Path("benchmark/http_shootout/results")
    if not results_dir.exists():
        print(f"Results directory not found: {results_dir}", file=sys.stderr)
        return 1

    rows: list[dict] = []
    for path in sorted(results_dir.glob("*.oha.json")):
        rows.append(parse_oha(path))
    for path in sorted(results_dir.glob("*.h2load.txt")):
        rows.append(parse_h2load(path))

    rows.sort(key=_row_sort_key)

    # Group into a separate table per payload size.
    seen_sizes: list[int] = []
    by_size: dict[int, list[dict]] = {}
    for row in rows:
        size = row["payload_bytes"]
        if size not in by_size:
            seen_sizes.append(size)
            by_size[size] = []
        by_size[size].append(row)

    def size_label(size: int) -> str:
        if size >= 1024 * 1024:
            return f"{size // (1024 * 1024)}MB"
        if size >= 1024:
            return f"{size // 1024}KB"
        return f"{size}B" if size else "unknown"

    for i, size in enumerate(seen_sizes):
        if i:
            print()
        print(f"=== {size_label(size)} ===")
        annotate_vs_nprpc(by_size[size])
        print_table(by_size[size])

    return 0


if __name__ == "__main__":
    raise SystemExit(main())