#!/usr/bin/env python3
"""Fail when a finished .ts translation is missing from the compiled .qm.

The normal build does not run lrelease, so the committed .qm can silently fall
behind the .ts (v1.0.172 shipped 19 untranslated zh_CN strings this way).
This gate parses both files and checks that every finished, non-obsolete
message in the .ts is present in the .qm that is embedded in the app.

Usage: check_qm_sync.py --ts translations/x.ts --qm resources/i18n/x.qm [--qm ...]
Regenerate with: lrelease translations/x.ts -qm resources/i18n/x.qm
"""
import argparse
import struct
import sys
import xml.etree.ElementTree as ET

QM_MAGIC = bytes([0x3C, 0xB8, 0x64, 0x18, 0xCA, 0xEF, 0x9C, 0x95,
                  0xCD, 0x21, 0x1C, 0xBF, 0x60, 0xA1, 0xBD, 0xDD])
SECTION_MESSAGES = 0x69

TAG_END = 1
TAG_SOURCE_TEXT16 = 2
TAG_TRANSLATION = 3
TAG_CONTEXT16 = 4
TAG_OBSOLETE1 = 5
TAG_SOURCE_TEXT = 6
TAG_CONTEXT = 7
TAG_COMMENT = 8
TAG_OBSOLETE2 = 9


def qm_messages(path):
    """Return the set of (context, source, comment) keys that carry a translation."""
    with open(path, "rb") as f:
        data = f.read()
    if not data.startswith(QM_MAGIC):
        raise ValueError(f"{path}: not a .qm file")
    pos = len(QM_MAGIC)
    messages = None
    while pos + 5 <= len(data):
        tag = data[pos]
        (length,) = struct.unpack(">I", data[pos + 1:pos + 5])
        pos += 5
        if tag == SECTION_MESSAGES:
            messages = data[pos:pos + length]
        pos += length
    if messages is None:
        raise ValueError(f"{path}: no messages section")

    keys = set()
    pos = 0
    ctx = src = cmt = ""
    has_translation = False
    while pos < len(messages):
        tag = messages[pos]
        pos += 1
        if tag == TAG_END:
            if has_translation:
                keys.add((ctx, src, cmt))
            ctx = src = cmt = ""
            has_translation = False
        elif tag in (TAG_TRANSLATION, TAG_SOURCE_TEXT16, TAG_CONTEXT16,
                     TAG_SOURCE_TEXT, TAG_CONTEXT, TAG_COMMENT):
            (length,) = struct.unpack(">I", messages[pos:pos + 4])
            pos += 4
            if length == 0xFFFFFFFF:
                continue
            blob = messages[pos:pos + length]
            pos += length
            if tag == TAG_TRANSLATION:
                has_translation = True
            elif tag == TAG_SOURCE_TEXT:
                src = blob.decode("utf-8")
            elif tag == TAG_CONTEXT:
                ctx = blob.decode("utf-8")
            elif tag == TAG_COMMENT:
                cmt = blob.decode("utf-8")
        elif tag == TAG_OBSOLETE1:
            pos += 4
        elif tag == TAG_OBSOLETE2:
            pass
        else:
            raise ValueError(f"{path}: unknown message tag {tag} at {pos - 1}")
    return keys


def ts_messages(path):
    """Return the set of finished (context, source, comment) keys in a .ts file."""
    keys = set()
    root = ET.parse(path).getroot()
    for context in root.iter("context"):
        name = context.findtext("name") or ""
        for message in context.iter("message"):
            translation = message.find("translation")
            if translation is None:
                continue
            kind = translation.get("type", "")
            if kind in ("unfinished", "obsolete", "vanished"):
                continue
            numerus = translation.findall("numerusform")
            text = "".join(n.text or "" for n in numerus) if numerus else (translation.text or "")
            if not text:
                continue
            source = message.findtext("source") or ""
            comment = message.findtext("comment") or ""
            keys.add((name, source, comment))
    return keys


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--ts", required=True)
    parser.add_argument("--qm", required=True, action="append")
    args = parser.parse_args()

    expected = ts_messages(args.ts)
    status = 0
    for qm in args.qm:
        present = qm_messages(qm)
        # lrelease may drop the disambiguation comment when (context, source) is unique,
        # so a key without its comment also counts as present.
        missing = sorted(k for k in expected
                         if k not in present and (k[0], k[1], "") not in present)
        if missing:
            status = 1
            print(f"FAIL {qm}: {len(missing)} finished translation(s) from {args.ts} "
                  f"are missing. Re-run lrelease (target update_translations_qm).")
            for ctx, src, cmt in missing[:50]:
                suffix = f" [{cmt}]" if cmt else ""
                print(f"  {ctx}: {src!r}{suffix}")
        else:
            print(f"OK {qm}: all {len(expected)} finished translations present")
    return status


if __name__ == "__main__":
    sys.exit(main())
