#!/usr/bin/env python3
"""Incremental clang-tidy gate: fail only on diagnostics on changed lines.

The "Static analysis (clang-tidy)" CI job analyses every C++ file a push
touches, but clang-tidy reports diagnostics for the WHOLE file. Historical debt
in a touched file (empty catch blocks by design, printf-based test output,
enum-size nits, ...) therefore turned the required gate red for changes that
introduced none of it, which is the same class of problem the incremental
clang-format step already solved.

This filter keeps the gate strict about what a change actually introduces: a
diagnostic counts only when it falls on a line added or modified by the push
(plus every line of a newly added file).

Usage:
    clang_tidy_changed_only.py --base <sha> --head <sha> --log <clang-tidy-output>

Exit codes: 0 = no new diagnostics, 1 = at least one new diagnostic,
2 = usage/repository error.
"""

from __future__ import annotations

import argparse
import re
import subprocess
import sys

# <path>:LINE:COL: <severity>: message [check-name,-warnings-as-errors]
# Searched (not anchored): CI log lines carry a job/step/timestamp preamble.
DIAGNOSTIC = re.compile(
    r"(?P<path>(?:[A-Za-z]:)?(?:\S)*?\.(?:cpp|h|inc)):"
    r"(?P<line>\d+):(?P<col>\d+):\s+(?P<severity>error|warning):\s+"
    r"(?P<message>.*?)(?:\s+\[(?P<checks>[^\]]+)\])?$"
)
HUNK = re.compile(r"^@@ -\d+(?:,\d+)? \+(?P<start>\d+)(?:,(?P<count>\d+))? @@")
FILE_HEADER = re.compile(r"^\+\+\+ b/(?P<path>.+)$")


def changed_lines(base: str, head: str) -> dict[str, list[tuple[int, int]]]:
    """Map repository-relative path -> [(first_line, last_line), ...] changed."""
    diff = subprocess.run(
        [
            "git",
            "diff",
            "--unified=0",
            # Line-ending normalisation (required by the Format gate for CRLF
            # files) would otherwise mark every line of the file as changed and
            # defeat this filter's purpose.
            "--ignore-cr-at-eol",
            base,
            head,
            "--",
            "src/**/*.cpp",
            "src/**/*.h",
        ],
        check=True,
        capture_output=True,
        text=True,
        encoding="utf-8",
        errors="replace",
    ).stdout

    ranges: dict[str, list[tuple[int, int]]] = {}
    current: str | None = None
    for line in diff.splitlines():
        header = FILE_HEADER.match(line)
        if header:
            current = header.group("path")
            ranges.setdefault(current, [])
            continue
        hunk = HUNK.match(line)
        if hunk and current is not None:
            start = int(hunk.group("start"))
            count = int(hunk.group("count") or 1)
            if count > 0:
                ranges[current].append((start, start + count - 1))
    return ranges


def normalise(path: str, prefix: str) -> str:
    """Map an absolute diagnostic path to the repository-relative form."""
    if prefix and path.startswith(prefix):
        path = path[len(prefix) :]
    path = path.lstrip("/")
    # The CI checks out into <workspace>/<repo>/<repo>.
    marker = "mviewer/"
    if path.startswith(marker) and not path.startswith(marker + marker):
        path = path[len(marker) :]
    while path.startswith(marker):
        path = path[len(marker) :]
    return path


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--base", required=True)
    parser.add_argument("--head", required=True)
    parser.add_argument("--log", required=True, help="file holding clang-tidy output")
    parser.add_argument("--prefix", default="", help="path prefix to strip (workspace root)")
    args = parser.parse_args()

    try:
        changed = changed_lines(args.base, args.head)
    except subprocess.CalledProcessError as error:
        print(f"clang-tidy gate: git diff failed: {error}", file=sys.stderr)
        return 2

    if not changed:
        print("clang-tidy gate: no changed C++ files; passing.")
        return 0

    with open(args.log, "r", encoding="utf-8", errors="replace") as handle:
        log_lines = handle.read().splitlines()

    offenders: list[tuple[str, int, str, str]] = []
    for raw in log_lines:
        match = DIAGNOSTIC.search(raw.strip())
        if not match:
            continue
        path = normalise(match.group("path"), args.prefix)
        # Only diagnostics the job promotes to errors matter; plain warnings
        # (readability-*, modernize-*, ...) are advisory.
        if match.group("severity") != "error":
            continue
        line = int(match.group("line"))
        spans = changed.get(path)
        if not spans:
            continue
        if not any(start <= line <= end for start, end in spans):
            continue
        checks = match.group("checks") or "-"
        offenders.append((path, line, match.group("severity"), f"{match.group('message')} [{checks}]"))

    if not offenders:
        print(
            f"clang-tidy gate: no new diagnostics on changed lines "
            f"({len(changed)} file(s) analysed)."
        )
        return 0

    print("clang-tidy gate: diagnostics introduced by this change:", file=sys.stderr)
    for path, line, severity, message in offenders:
        print(f"  {path}:{line}: {severity}: {message}", file=sys.stderr)
    return 1


if __name__ == "__main__":
    sys.exit(main())
