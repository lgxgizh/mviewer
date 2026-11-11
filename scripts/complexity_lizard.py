#!/usr/bin/env python3
"""Accurate CCN / function-length report with lizard (review P2-9).

scripts/complexity_gate.ps1 approximates McCabe complexity with a brace-stack
parser and reported "CCN>25: 0" while lizard finds ~41 such functions (for
example MainWindow::eventFilter at 48). This script is the reference count. It
is advisory (exit 0) unless --max-ccn25 / --max-long-functions are given, which
turn the current counts into a ratchet: the number may go down, never up.

Usage: python scripts/complexity_lizard.py [--root .] [--summary FILE]
       [--max-ccn25 N] [--max-long-functions N]
"""
import argparse
import os
import sys

try:
    import lizard
except ImportError:  # pragma: no cover - CI installs it
    print("lizard is not installed (pip install lizard); skipping")
    sys.exit(0)


def production_files(root):
    src = os.path.join(root, "src")
    for dirpath, _dirs, files in os.walk(src):
        rel = os.path.relpath(dirpath, root).replace("\\", "/")
        if "/benchmark" in rel:
            continue
        for name in files:
            if not name.endswith((".cpp", ".h")):
                continue
            if name.startswith("test_") or name.endswith(".inc"):
                continue
            yield os.path.join(dirpath, name)


def main():
    p = argparse.ArgumentParser(description=__doc__,
                                formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--root", default=".")
    p.add_argument("--summary", default="")
    p.add_argument("--max-ccn25", type=int, default=-1)
    p.add_argument("--max-long-functions", type=int, default=-1)
    args = p.parse_args()

    functions = []
    for path in production_files(args.root):
        info = lizard.analyze_file(path)
        rel = os.path.relpath(path, args.root).replace("\\", "/")
        for fn in info.function_list:
            functions.append((fn.cyclomatic_complexity, fn.nloc, fn.long_name, rel, fn.start_line))

    ccn25 = [f for f in functions if f[0] > 25]
    ccn15 = [f for f in functions if f[0] > 15]
    long120 = [f for f in functions if f[1] > 120]
    lines = [
        "## Complexity (lizard, production code)",
        f"- functions: {len(functions)}",
        f"- CCN > 25: **{len(ccn25)}**, CCN > 15: {len(ccn15)}, NLOC > 120: **{len(long120)}**",
        "",
        "| CCN | NLOC | function | location |",
        "|---|---|---|---|",
    ]
    for ccn, nloc, name, rel, line in sorted(functions, reverse=True)[:15]:
        lines.append(f"| {ccn} | {nloc} | `{name[:80]}` | {rel}:{line} |")
    text = "\n".join(lines)
    print(text)
    if args.summary:
        with open(args.summary, "a", encoding="utf-8") as f:
            f.write(text + "\n")

    status = 0
    if 0 <= args.max_ccn25 < len(ccn25):
        print(f"::error:: CCN>25 functions grew to {len(ccn25)} (ratchet {args.max_ccn25})")
        status = 1
    if 0 <= args.max_long_functions < len(long120):
        print(f"::error:: NLOC>120 functions grew to {len(long120)} "
              f"(ratchet {args.max_long_functions})")
        status = 1
    return status


if __name__ == "__main__":
    sys.exit(main())
