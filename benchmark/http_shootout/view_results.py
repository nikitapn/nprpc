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
    name = path.name.lower()
    if "1kb" in name:
        return 1024
    if "64kb" in name:
        return 64 * 1024
    if "1mb" in name:
        return 1024 * 1024
    return None


def parse_oha(path: Path) -> dict[str, str]:
    data = json.loads(path.read_text())
    summary = data.get("summary", {})
    latency = data.get("latencyPercentiles", {})
    status = data.get("statusCodeDistribution", {})
    errors = data.get("errorDistribution", {})
    total_requests = sum(int(v) for v in status.values()) + sum(int(v) for v in errors.values())

    return {
        "tool": "oha",
        "name": path.name,
        "rps": fmt_float(summary.get("requestsPerSec")),
        "avg_ms": fmt_seconds_as_ms(summary.get("average")),
        "p50_ms": fmt_seconds_as_ms(latency.get("p50")),
        "p95_ms": fmt_seconds_as_ms(latency.get("p95")),
        "p99_ms": fmt_seconds_as_ms(latency.get("p99")),
        "success": fmt_float(summary.get("successRate", 0.0) * 100.0),
        "requests": str(total_requests),
    }


def parse_h2load(path: Path) -> dict[str, str]:
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

    latency_match = re.search(
        r"time for request:\s+([0-9.]+)ms \[mean\],\s+([0-9.]+)ms \[sd\]",
        text,
    )
    status_match = re.search(r"status codes:\s+(\d+) 2xx,\s+(\d+) 3xx,\s+(\d+) 4xx,\s+(\d+) 5xx", text)
    data_match = re.search(r"traffic:\s+.+?,\s+.+?,\s+([^\s]+)\s*\((\d+)\) data", text)

    avg_ms = float(latency_match.group(1)) if latency_match else None
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
        "rps": fmt_float(req_per_sec),
        "avg_ms": fmt_float(avg_ms, 3),
        "p50_ms": "-",
        "p95_ms": "-",
        "p99_ms": "-",
        "success": fmt_float(success_pct),
        "requests": str(total) if total else "-",
        "note": note,
    }


def print_table(rows: list[dict[str, str]]) -> None:
    if not rows:
        print("No result files found.")
        return

    headers = ["file", "tool", "req", "rps", "avg_ms", "p50_ms", "p95_ms", "p99_ms", "success_%", "note"]
    body = [
        [
            row["name"],
            row["tool"],
            row["requests"],
            row["rps"],
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


def main() -> int:
    results_dir = Path(sys.argv[1]) if len(sys.argv) > 1 else Path("benchmark/http_shootout/results")
    if not results_dir.exists():
        print(f"Results directory not found: {results_dir}", file=sys.stderr)
        return 1

    rows: list[dict[str, str]] = []
    for path in sorted(results_dir.glob("*.oha.json")):
        rows.append(parse_oha(path))
    for path in sorted(results_dir.glob("*.h2load.txt")):
        rows.append(parse_h2load(path))

    print_table(rows)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())