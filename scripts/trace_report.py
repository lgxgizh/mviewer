#!/usr/bin/env python3
"""
M13 Phase 5 — trace report.

Parses a Chrome trace-format JSON produced by mviewer_bench --trace <file>
(MViewer's self-contained MV_TRACE_* sink) and prints per-stage duration
percentiles (p50 / p95 / p99) in microseconds and milliseconds.

This is the RFC Phase-5 acceptance: "each pipeline stage has p50/p95/p99 from
a real trace (not inference)."

Usage:
    python trace_report.py <trace.json> [--top N]
    python trace_report.py <trace.json> --name "Decoder::decodeFull"

The trace is Chrome-trace JSON (ph="X" duration events). The Perfetto web UI
(ui.perfetto.dev) also ingests this format directly.
"""
import argparse
import json
import statistics
import sys


def pct(sorted_vals, p):
    if not sorted_vals:
        return 0.0
    if len(sorted_vals) == 1:
        return float(sorted_vals[0])
    k = (len(sorted_vals) - 1) * (p / 100.0)
    f = int(k)
    c = min(f + 1, len(sorted_vals) - 1)
    if f == c:
        return float(sorted_vals[f])
    return sorted_vals[f] + (sorted_vals[c] - sorted_vals[f]) * (k - f)


def main():
    ap = argparse.ArgumentParser(description="MViewer trace report (p50/p95/p99)")
    ap.add_argument("trace", help="Chrome trace JSON from mviewer_bench --trace")
    ap.add_argument("--top", type=int, default=0, help="show only top N stages by p99")
    ap.add_argument("--name", help="restrict to a single event name (substring)")
    args = ap.parse_args()

    try:
        with open(args.trace, "r", encoding="utf-8") as f:
            doc = json.load(f)
    except (OSError, json.JSONDecodeError) as e:
        print(f"ERROR: cannot read trace: {e}", file=sys.stderr)
        return 1

    events = doc.get("traceEvents", [])
    spans = [e for e in events if e.get("ph") == "X" and "dur" in e]

    by_name = {}
    for e in spans:
        nm = e.get("name", "<unnamed>")
        if args.name and args.name not in nm:
            continue
        by_name.setdefault(nm, []).append(e["dur"])

    if not by_name:
        print("No duration events found (was MViewer built with "
              "MVIEWER_ENABLE_PERFETTO=ON, and did --trace capture spans?)")
        return 1

    rows = []
    for nm, durs in by_name.items():
        durs_sorted = sorted(durs)
        rows.append((
            nm, len(durs_sorted),
            pct(durs_sorted, 50), pct(durs_sorted, 95), pct(durs_sorted, 99),
        ))

    rows.sort(key=lambda r: r[4], reverse=True)
    if args.top:
        rows = rows[:args.top]

    print(f"{'stage':36s} {'n':>6s} {'p50(us)':>10s} {'p95(us)':>10s} {'p99(us)':>10s}  p50(ms)  p99(ms)")
    print("-" * 100)
    for nm, n, p50, p95, p99 in rows:
        print(f"{nm:36s} {n:6d} {p50:10.1f} {p95:10.1f} {p99:10.1f}  {p50/1000:7.2f}  {p99/1000:7.2f}")

    print(f"\nTotal duration spans: {len(spans)}  distinct stages: {len(by_name)}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
