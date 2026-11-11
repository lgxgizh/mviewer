#!/usr/bin/env python3
"""Nightly benchmark regression check with a usable signal (review P0-3).

The old check compared ONE sample of each millisecond-scale metric with a
hand-pinned point baseline at +/-10% and was red 10 of 11 nights. This script:

* takes the per-metric MEDIAN of N mviewer_bench --results samples;
* compares it with a ROLLING baseline: the per-metric median of the last K
  nightly reports whose quality job was green (falls back to the committed
  benchmark/perf_baseline.json when fewer than --min-history reports exist);
* ignores percentage changes that are tiny in absolute terms: for *_ms metrics
  a change below --min-abs-ms (2 ms) or values below --min-value-ms (20 ms) on
  both sides cannot regress;
* reports an improvement beyond --stale-pct (-50%) as a stale baseline, not a pass;
* in --mode report (default) never fails; --mode enforce exits 1 on a regression.

It writes a results JSON in the mviewer_bench --results format (median values),
so publish-health / benchmark_trend / health_score keep working unchanged.
"""
import argparse
import glob
import json
import os
import statistics
import sys

HIGHER_IS_BETTER = {"cache_hit_rate", "cache_hit_ratio", "thumbnails_per_sec",
                    "baseline_return_ok"}


def key_of(result):
    return f"{result['name']}_{result['metric']}"


def load_results(path):
    with open(path, encoding="utf-8") as f:
        doc = json.load(f)
    return doc


def median_results(sample_docs):
    """Per-metric median across samples; 'passed' is true only if every sample passed."""
    order = []
    values = {}
    passed = {}
    meta = {}
    for doc in sample_docs:
        meta = {k: v for k, v in doc.items() if k != "results"}
        for r in doc.get("results", []):
            k = (r["name"], r["metric"])
            if k not in values:
                order.append(k)
                values[k] = []
                passed[k] = True
            if isinstance(r.get("value"), (int, float)):
                values[k].append(float(r["value"]))
            passed[k] = passed[k] and bool(r.get("passed", True))
    out = []
    for k in order:
        if not values[k]:
            continue
        out.append({"name": k[0], "metric": k[1], "value": statistics.median(values[k]),
                    "passed": passed[k], "samples": len(values[k]),
                    "min": min(values[k]), "max": max(values[k])})
    meta["aggregation"] = f"median of {len(sample_docs)} samples"
    meta["results"] = out
    return meta


def rolling_baseline(history_docs):
    per_key = {}
    for doc in history_docs:
        for r in doc.get("results", []):
            if isinstance(r.get("value"), (int, float)):
                per_key.setdefault(key_of(r), []).append(float(r["value"]))
    return {k: statistics.median(v) for k, v in per_key.items() if v}


def committed_baseline(path):
    with open(path, encoding="utf-8") as f:
        doc = json.load(f)
    out = {}
    for k, v in doc.get("metrics", {}).items():
        try:
            out[k] = float(v)
        except (TypeError, ValueError):
            pass
    return out


def classify(metric, value, base, args):
    """Return (status, delta_pct). status in ok|regression|stale|noise|new."""
    if base is None:
        return "new", None
    if metric == "baseline_return_ok":
        return ("ok" if value >= base else "regression"), None
    if base == 0:
        return "ok", None
    delta = (value - base) / abs(base) * 100.0
    worse = -delta if metric in HIGHER_IS_BETTER else delta
    if metric.endswith("_ms"):
        if abs(value - base) < args.min_abs_ms or max(value, base) < args.min_value_ms:
            return ("noise" if worse > args.threshold_pct else "ok"), delta
    if worse > args.threshold_pct:
        return "regression", delta
    if worse < args.stale_pct:
        return "stale", delta
    return "ok", delta


def compare(current, baseline, args):
    rows = []
    for r in current["results"]:
        status, delta = classify(r["metric"], r["value"], baseline.get(key_of(r)), args)
        rows.append((key_of(r), r["value"], baseline.get(key_of(r)), delta, status))
    return rows


def render(rows, source, args):
    lines = ["=== REGRESSION CHECK (median vs rolling baseline) ===",
             f"baseline: {source}; threshold +{args.threshold_pct:.0f}%; "
             f"ms floors: |diff| >= {args.min_abs_ms} ms and value >= {args.min_value_ms} ms; "
             f"stale below {args.stale_pct:.0f}%; mode: {args.mode}",
             "| metric | median | baseline | delta | status |", "|---|---|---|---|---|"]
    for key, value, base, delta, status in rows:
        b = "—" if base is None else f"{base:.4g}"
        d = "—" if delta is None else f"{delta:+.1f}%"
        lines.append(f"| {key} | {value:.4g} | {b} | {d} | {status} |")
    return "\n".join(lines)


def self_test():
    class A:
        threshold_pct = 10.0
        min_abs_ms = 2.0
        min_value_ms = 20.0
        stale_pct = -50.0
    a = A()
    assert classify("x_ms", 14.4, 8.8, a)[0] == "noise"          # +63% but < 20 ms
    assert classify("x_ms", 400.0, 300.0, a)[0] == "regression"  # +33%, big
    assert classify("x_ms", 301.0, 300.0, a)[0] == "ok"
    assert classify("x_ms", 100.0, 300.0, a)[0] == "stale"
    assert classify("thumbnails_per_sec", 30.0, 40.0, a)[0] == "regression"
    assert classify("thumbnails_per_sec", 50.0, 40.0, a)[0] == "ok"
    assert classify("baseline_return_ok", 0.0, 1.0, a)[0] == "regression"
    assert classify("x_ms", 1.0, None, a)[0] == "new"
    med = median_results([
        {"results": [{"name": "B2", "metric": "first_thumbnail_ms", "value": v, "passed": True}]}
        for v in (9.0, 30.0, 10.0)])
    assert med["results"][0]["value"] == 10.0
    assert rolling_baseline([{"results": [{"name": "B", "metric": "m", "value": v}]}
                             for v in (1.0, 3.0, 2.0)]) == {"B_m": 2.0}
    print("perf_regression self-test: PASS")
    return 0


def main():
    p = argparse.ArgumentParser(description=__doc__,
                                formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--samples", nargs="*", default=[], help="mviewer_bench --results files")
    p.add_argument("--history-dir", default="", help="dir with previous green benchmark_report.json")
    p.add_argument("--baseline", default="benchmark/perf_baseline.json")
    p.add_argument("--min-history", type=int, default=3)
    p.add_argument("--threshold-pct", type=float, default=10.0)
    p.add_argument("--min-abs-ms", type=float, default=2.0)
    p.add_argument("--min-value-ms", type=float, default=20.0)
    p.add_argument("--stale-pct", type=float, default=-50.0)
    p.add_argument("--mode", choices=["report", "enforce"], default="report")
    p.add_argument("--out", default="", help="write median results JSON here")
    p.add_argument("--summary", default="", help="append a markdown table here")
    p.add_argument("--self-test", action="store_true")
    args = p.parse_args()
    if args.self_test:
        return self_test()
    if not args.samples:
        print("::error:: no benchmark samples given")
        return 1

    current = median_results([load_results(s) for s in args.samples])
    if args.out:
        with open(args.out, "w", encoding="utf-8") as f:
            json.dump(current, f, indent=2)

    history = []
    if args.history_dir:
        for path in sorted(glob.glob(os.path.join(args.history_dir, "**", "benchmark_report.json"),
                                     recursive=True)):
            try:
                history.append(load_results(path))
            except (OSError, ValueError):
                pass
    if len(history) >= args.min_history:
        baseline = rolling_baseline(history)
        source = f"rolling median of {len(history)} green nightly report(s)"
    else:
        baseline = committed_baseline(args.baseline)
        source = f"{args.baseline} (only {len(history)} green nightly report(s) available)"

    rows = compare(current, baseline, args)
    text = render(rows, source, args)
    print(text)
    if args.summary:
        with open(args.summary, "a", encoding="utf-8") as f:
            f.write(text + "\n")
    regressions = [r for r in rows if r[4] == "regression"]
    stale = [r for r in rows if r[4] == "stale"]
    for key, *_ in stale:
        print(f"::warning:: {key}: large improvement vs baseline (stale baseline?)")
    for key, value, base, delta, _ in regressions:
        level = "error" if args.mode == "enforce" else "warning"
        print(f"::{level}:: {key}: {value:.4g} vs {base:.4g} ({delta:+.1f}%)" if delta is not None
              else f"::{level}:: {key}: {value} vs {base}")
    if regressions and args.mode == "enforce":
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
