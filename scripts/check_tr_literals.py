#!/usr/bin/env python3
"""Flag bare English UI string literals that bypass tr()/translate().

Non-test src/**/*.cpp must not pass a string literal that starts with an
ASCII letter straight into these user-visible calls:

  addAction, addItem, setPlaceholderText, setToolTip, setWindowTitle,
  new QLabel, new QPushButton, new QCheckBox,
  QMessageBox::{information,warning,critical,question} title and body.

Only the text argument is checked (the first argument, or the title and body
of a message box). Item data, slot names, objectName, and QSettings keys are
out of scope. Literals inside tr(), translate(), QT_TR_NOOP, QT_TRANSLATE_NOOP,
QStringLiteral, or QLatin1String are allowed. Digits, percentages, and strings
that do not start with a letter ("0", "50%", "—", "{name}", "(note)") are
allowed.

Usage:
  python3 scripts/check_tr_literals.py [--root REPO] [paths...]
  python3 scripts/check_tr_literals.py --self-test

Exit code 0 = clean, 1 = violations or a failing self-test.
"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

WRAPPERS = {
    "tr",
    "translate",
    "QT_TR_NOOP",
    "QT_TRANSLATE_NOOP",
    "QStringLiteral",
    "QLatin1String",
    "QLatin1StringView",
    # Lookup key for a row that is translated inside compareShortcutText.
    "compareShortcutText",
}
# (kind, literal name) where kind "text" checks argument 0 and "box" checks 1 and 2.
CALLS = (
    ("text", "addAction"),
    ("text", "addItem"),
    ("text", "setPlaceholderText"),
    ("text", "setToolTip"),
    ("text", "setWindowTitle"),
    ("qlabel", "QLabel"),
    ("qpush", "QPushButton"),
    ("qcheck", "QCheckBox"),
    ("box", "information"),
    ("box", "warning"),
    ("box", "critical"),
    ("box", "question"),
)


def is_product_cpp(rel: str) -> bool:
    """True for src C++ sources that are part of the product, not a test."""
    path = Path(rel)
    if path.suffix != ".cpp":
        return False
    parts = path.parts
    if not parts or parts[0] != "src":
        return False
    return not path.name.startswith("test_")


def _starts_with_letter(content: str) -> bool:
    return bool(content) and content[0].isascii() and content[0].isalpha()


def _skip_comment(src: str, index: int) -> int | None:
    if src.startswith("//", index):
        end = src.find("\n", index + 2)
        return len(src) if end < 0 else end
    if src.startswith("/*", index):
        end = src.find("*/", index + 2)
        return len(src) if end < 0 else end + 2
    return None


def _parse_string(src: str, index: int) -> tuple[int, str] | None:
    """If index starts a string literal, return the index after it and its body."""
    prefixes = ("u8R", "UR", "uR", "LR", "u8", "u", "U", "L", "R")
    prefix = ""
    for candidate in prefixes:
        if src.startswith(candidate, index):
            prefix = candidate
            break
    cursor = index + len(prefix)
    if cursor >= len(src) or src[cursor] != '"':
        return None
    if prefix.endswith("R"):
        cursor += 1
        delim_at = cursor
        while cursor < len(src) and src[cursor] != "(":
            cursor += 1
        if cursor >= len(src):
            return len(src), ""
        delim = src[delim_at:cursor]
        body_at = cursor + 1
        closer = ")" + delim + '"'
        found = src.find(closer, body_at)
        if found < 0:
            return len(src), src[body_at:]
        return found + len(closer), src[body_at:found]
    cursor += 1
    body_at = cursor
    while cursor < len(src):
        if src[cursor] == "\\":
            cursor += 2
            continue
        if src[cursor] == '"':
            return cursor + 1, src[body_at:cursor]
        cursor += 1
    return len(src), src[body_at:]


def _skip_char(src: str, index: int) -> int | None:
    if index >= len(src) or src[index] != "'":
        return None
    cursor = index + 1
    while cursor < len(src):
        if src[cursor] == "\\":
            cursor += 2
            continue
        if src[cursor] == "'":
            return cursor + 1
        cursor += 1
    return len(src)


def _skip_atomic(src: str, index: int) -> int | None:
    comment = _skip_comment(src, index)
    if comment is not None:
        return comment
    char_end = _skip_char(src, index)
    if char_end is not None:
        return char_end
    parsed = _parse_string(src, index)
    if parsed is not None:
        return parsed[0]
    return None


def _find_close(src: str, open_index: int) -> int:
    depth = 0
    index = open_index
    while index < len(src):
        skipped = _skip_atomic(src, index)
        if skipped is not None:
            index = skipped
            continue
        char = src[index]
        if char == "(":
            depth += 1
        elif char == ")":
            depth -= 1
            if depth == 0:
                return index
        index += 1
    return -1


def _split_args(src: str) -> list[str]:
    args: list[str] = []
    start = 0
    paren = bracket = brace = 0
    index = 0
    while index < len(src):
        skipped = _skip_atomic(src, index)
        if skipped is not None:
            index = skipped
            continue
        char = src[index]
        if char == "(":
            paren += 1
        elif char == ")":
            paren = max(0, paren - 1)
        elif char == "[":
            bracket += 1
        elif char == "]":
            bracket = max(0, bracket - 1)
        elif char == "{":
            brace += 1
        elif char == "}":
            brace = max(0, brace - 1)
        elif char == "," and paren == bracket == brace == 0:
            args.append(src[start:index])
            start = index + 1
        index += 1
    tail = src[start:]
    if tail.strip():
        args.append(tail)
    return args


def _read_ident(src: str, index: int) -> tuple[str, int] | None:
    if index >= len(src) or not (
        src[index].isascii() and (src[index].isalpha() or src[index] == "_")
    ):
        return None
    cursor = index + 1
    while cursor < len(src) and (src[cursor].isalnum() or src[cursor] == "_"):
        cursor += 1
    return src[index:cursor], cursor


def _bare_literals(expr: str) -> list[str]:
    found: list[str] = []
    index = 0
    while index < len(expr):
        comment = _skip_comment(expr, index)
        if comment is not None:
            index = comment
            continue
        char_end = _skip_char(expr, index)
        if char_end is not None:
            index = char_end
            continue
        parsed = _parse_string(expr, index)
        if parsed is not None:
            end, content = parsed
            if _starts_with_letter(content):
                found.append(content)
            index = end
            continue
        ident = _read_ident(expr, index)
        if ident is not None:
            name, cursor = ident
            probe = cursor
            while probe < len(expr) and expr[probe].isspace():
                probe += 1
            if name in WRAPPERS and probe < len(expr) and expr[probe] == "(":
                close = _find_close(expr, probe)
                index = len(expr) if close < 0 else close + 1
                continue
            index = cursor
            continue
        index += 1
    return found


def _match_call(src: str, index: int) -> tuple[str, int] | None:
    """Return (kind, index of '(') when a watched call starts at index."""
    if index > 0 and (src[index - 1].isalnum() or src[index - 1] == "_"):
        return None
    for kind, name in CALLS:
        if not src.startswith(name, index):
            continue
        cursor = index + len(name)
        if cursor < len(src) and (src[cursor].isalnum() or src[cursor] == "_"):
            continue
        if kind == "box":
            # QMessageBox::warning( — require the qualifier before the name.
            head = src[:index].rstrip()
            if not head.endswith("QMessageBox::"):
                continue
        elif kind in {"qlabel", "qpush", "qcheck"}:
            head = src[:index].rstrip()
            if not head.endswith("new"):
                continue
            new_at = len(head) - 3
            if new_at > 0 and (head[new_at - 1].isalnum() or head[new_at - 1] == "_"):
                continue
            if src[len(head) : index].strip():
                continue
        while cursor < len(src) and src[cursor].isspace():
            cursor += 1
        if cursor < len(src) and src[cursor] == "(":
            return kind, cursor
    return None


def _literal_line(src: str, open_paren: int, arg: str, literal: str) -> int:
    rel = arg.find(literal)
    if rel < 0:
        return src.count("\n", 0, open_paren) + 1
    quote = arg.rfind('"', 0, rel)
    offset = quote if quote >= 0 else rel
    return src.count("\n", 0, open_paren) + 1 + arg.count("\n", 0, offset)


def scan_source(src: str) -> list[tuple[int, str, str]]:
    """Return (line, call, literal) for each bare UI string in src."""
    hits: list[tuple[int, str, str]] = []
    index = 0
    while index < len(src):
        skipped = _skip_atomic(src, index)
        if skipped is not None:
            index = skipped
            continue
        matched = _match_call(src, index)
        if matched is None:
            index += 1
            continue
        kind, open_paren = matched
        close = _find_close(src, open_paren)
        if close < 0:
            index = open_paren + 1
            continue
        args = _split_args(src[open_paren + 1 : close])
        chosen = args[1:3] if kind == "box" else args[:1]
        call = src[index:open_paren].strip()
        for arg in chosen:
            for literal in _bare_literals(arg):
                hits.append((_literal_line(src, open_paren, arg, literal), call, literal))
        index = open_paren + 1
    return hits


def scan_file(path: Path, rel: str) -> list[tuple[int, str, str]]:
    try:
        text = path.read_text(encoding="utf-8")
    except UnicodeDecodeError:
        return [(0, rel, "file is not valid UTF-8")]
    return scan_source(text)


BAD_SAMPLE = r"""
void bad(QMenu &menu, QWidget *parent, bool playing) {
    menu.addAction("Rename");
    menu.addAction(playing ? "Pause" : "Play");
    menu.addAction("Open", this, "onOpen");
    menu.addItem("File Name");
    edit.setPlaceholderText("Camera");
    edit.setToolTip("Filter by file type");
    setWindowTitle("Batch Processing");
    auto *label = new QLabel("Retry count:");
    auto *button = new QPushButton("Start");
    auto *check = new QCheckBox("Metadata");
    QMessageBox::warning(parent, "Batch Processing", "Please add files first.");
    QMessageBox::information(parent,
        "Title",
        "Body text");
}
"""

GOOD_SAMPLE = r"""
void good(QMenu &menu, QWidget *parent, bool playing, const QString &title) {
    menu.addAction(tr("Rename"));
    menu.addAction(playing ? tr("Pause") : tr("Play"));
    menu.addAction(QCoreApplication::translate("ImageViewer", "Save As..."));
    menu.addAction(tr("Open"), this, "onOpen");
    menu.addItem(tr("JPG"), "jpg,jpeg");
    menu.addItem(QStringLiteral("png"));
    menu.addItem(QT_TR_NOOP("File Name"));
    edit.setPlaceholderText(tr("Camera"));
    edit.setPlaceholderText("{name}_{seq:3}");
    edit.setPlaceholderText("(Leave empty = original directory)");
    edit.setPlaceholderText("© 2025");
    edit.setToolTip(tr("Filter by file type"));
    setWindowTitle(tr("Batch Processing"));
    setWindowTitle(QStringLiteral("MViewer"));
    auto *dash = new QLabel("—", parent);
    auto *zero = new QLabel("0", parent);
    auto *pct = new QLabel("50%", parent);
    auto *arrow = new QLabel(">", parent);
    auto *button = new QPushButton(tr("Start"));
    auto *check = new QCheckBox(tr("Metadata"));
    auto *sync = new QCheckBox(mviewer::cw::compareShortcutText("Z"), parent);
    QMessageBox::warning(parent, tr("Batch Processing"), tr("Please add files first."));
    QMessageBox::warning(parent, title, message);
    button->setObjectName("sortFieldLabel");
    settings.setValue("batch/overwriteExisting", false);
    // menu.addAction("Rename");
    const char *sample = "addAction(\"Rename\")";
}
"""

BAD_LITERALS = {
    "Rename",
    "Pause",
    "Play",
    "Open",
    "File Name",
    "Camera",
    "Filter by file type",
    "Batch Processing",
    "Retry count:",
    "Start",
    "Metadata",
    "Please add files first.",
    "Title",
    "Body text",
}


def run_self_test() -> int:
    failures: list[str] = []
    bad_hits = {literal for _, _, literal in scan_source(BAD_SAMPLE)}
    missing = BAD_LITERALS - bad_hits
    extra = bad_hits - BAD_LITERALS
    if missing:
        failures.append(f"self-test missed bad literals: {sorted(missing)}")
    if extra:
        failures.append(f"self-test flagged unexpected bad literals: {sorted(extra)}")
    if "onOpen" in bad_hits:
        failures.append("self-test flagged addAction slot name onOpen")
    good_hits = scan_source(GOOD_SAMPLE)
    if good_hits:
        failures.append(
            "self-test flagged translated or non-UI literals: "
            + ", ".join(repr(literal) for _, _, literal in good_hits)
        )
    expectations = {
        "src/test_language.cpp": False,
        "src/core/test_metadata.cpp": False,
        "src/mainwindow.cpp": True,
        "src/mainwindow.h": False,
        "docs/USER_GUIDE.md": False,
    }
    for rel, expected in expectations.items():
        if is_product_cpp(rel) != expected:
            failures.append(f"path filter {rel}: expected {expected}")
    if failures:
        print("FAIL: check_tr_literals.py --self-test")
        for failure in failures:
            print(f"  {failure}")
        return 1
    print(
        f"PASS: self-test caught {len(BAD_LITERALS)} bad literals "
        "and accepted the translated fixtures."
    )
    return 0


def main() -> int:
    if hasattr(sys.stdout, "reconfigure"):
        sys.stdout.reconfigure(errors="backslashreplace")
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--root", default=Path(__file__).resolve().parent.parent, type=Path)
    parser.add_argument(
        "--self-test",
        action="store_true",
        help="run built-in positive and negative samples before scanning",
    )
    parser.add_argument("paths", nargs="*", default=["src"])
    args = parser.parse_args()

    status = 0
    if args.self_test:
        status = run_self_test()

    root: Path = args.root.resolve()
    violations = 0
    files = 0
    for base in args.paths:
        base_path = (root / base).resolve()
        if not base_path.exists():
            continue
        candidates = [base_path] if base_path.is_file() else sorted(base_path.rglob("*.cpp"))
        for path in candidates:
            if not path.is_file():
                continue
            rel = path.relative_to(root).as_posix()
            if not is_product_cpp(rel):
                continue
            files += 1
            for line, call, literal in scan_file(path, rel):
                violations += 1
                shown = literal.replace("\n", "\\n")
                print(f"{rel}:{line}: bare UI string in {call}(...): {shown!r}")

    if violations:
        print(
            f"\nFAIL: {violations} bare UI string literal(s) in {files} product source file(s).\n"
            "Wrap user-visible text in tr(), QT_TR_NOOP, or "
            "QCoreApplication::translate, and add the Chinese to "
            "translations/mviewer_zh_CN.ts."
        )
        status = 1
    elif status == 0:
        print(f"PASS: no bare UI string literals ({files} product cpp files scanned).")
    return status


if __name__ == "__main__":
    sys.exit(main())
