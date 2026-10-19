#!/usr/bin/env python3
"""i18n gate: fail when CJK (Han) characters appear in C++/CMake sources outside an allowlist.

MViewer's source language is English. Chinese belongs in translations/*.ts
(UI) and resources/help/*.zh_CN.html (help pages), never in src/.

A line is allowed when:
  * it, or the line directly above it, contains the marker ``i18n-allow``;
  * it sits between ``i18n-allow-begin`` and ``i18n-allow-end`` markers;
  * its file matches an entry of ALLOWED_FILES (test fixtures that exercise
    Unicode file names or the zh_CN translation itself).

Every marker should carry an English reason, e.g.
``// i18n-allow: matches zh-CN Windows error text, never displayed``.

Usage: python3 scripts/check_no_chinese.py [--root REPO] [paths...]
Exit code 0 = clean, 1 = violations found.
"""

from __future__ import annotations

import argparse
import fnmatch
import re
import sys
from pathlib import Path

HAN = re.compile(r"[\u3400-\u4dbf\u4e00-\u9fff\uf900-\ufaff\u3000-\u303f\uff00-\uffef]")
SOURCE_SUFFIXES = {".h", ".hpp", ".cpp", ".cc", ".inc", ".ui", ".cmake"}
SOURCE_NAMES = {"CMakeLists.txt"}

# Files that legitimately contain Chinese. Keep this list short and justified.
ALLOWED_FILES = [
    # Tests that verify the zh_CN translation is installed and shown.
    "src/test_i18n*.cpp",
    "src/test_language*.cpp",
]


def is_allowed_file(rel: str) -> bool:
    return any(fnmatch.fnmatch(rel, pattern) for pattern in ALLOWED_FILES)


def scan_file(path: Path, rel: str) -> list[tuple[int, str]]:
    try:
        lines = path.read_text(encoding="utf-8").splitlines()
    except UnicodeDecodeError:
        return [(0, "file is not valid UTF-8")]
    hits: list[tuple[int, str]] = []
    in_block = False
    previous = ""
    for number, line in enumerate(lines, start=1):
        if "i18n-allow-begin" in line:
            in_block = True
        elif "i18n-allow-end" in line:
            in_block = False
        elif HAN.search(line) and not in_block:
            if "i18n-allow" not in line and "i18n-allow" not in previous:
                hits.append((number, line.strip()))
        previous = line
    return hits


def main() -> int:
    # Windows consoles may not be UTF-8; never crash while reporting.
    if hasattr(sys.stdout, "reconfigure"):
        sys.stdout.reconfigure(errors="backslashreplace")
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--root", default=Path(__file__).resolve().parent.parent, type=Path)
    parser.add_argument("paths", nargs="*", default=["src", "CMakeLists.txt", "cmake"])
    args = parser.parse_args()
    root: Path = args.root.resolve()

    violations = 0
    files = 0
    for base in args.paths:
        base_path = (root / base).resolve()
        if not base_path.exists():
            continue
        candidates = [base_path] if base_path.is_file() else sorted(base_path.rglob("*"))
        for path in candidates:
            if not path.is_file() or (
                path.suffix not in SOURCE_SUFFIXES and path.name not in SOURCE_NAMES
            ):
                continue
            rel = path.relative_to(root).as_posix()
            if is_allowed_file(rel):
                continue
            files += 1
            for number, text in scan_file(path, rel):
                violations += 1
                print(f"{rel}:{number}: Chinese text outside tr()/translations: {text}")

    if violations:
        print(
            f"\nFAIL: {violations} line(s) with Chinese characters in {files} scanned file(s).\n"
            "Use English source text with tr()/QT_TR_NOOP and add the Chinese to "
            "translations/mviewer_zh_CN.ts, or mark a legitimate case with an "
            "'i18n-allow: <reason>' comment."
        )
        return 1
    print(f"PASS: no Chinese characters outside the allowlist ({files} files scanned).")
    return 0


if __name__ == "__main__":
    sys.exit(main())
