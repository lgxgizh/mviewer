# ADR 039: bounded classic Session ZIP parsing and immutable CRC

- Status: proposed (B05a review; not implemented until native approval)
- Date: 2026-11-24 (America/Chicago)

## Context and source adjudication

The old reader allocates the complete input before enforcing any payload cap.
Central/local offsets are added in uint32_t before comparison with size_t.
Checked field access returning zero prevents a demonstrated 64-bit heap OOB in
some paths; this is not evidence that hostile layouts are safe. The old parser
also accepts partial results after late errors, ambiguous names and overlapping
local records. The CRC table is initialized behind an unsynchronized bool.

## Decision

Keep classic, single-disk, store-only ZIP. Preserve the existing 128 MiB per-entry
and 256 MiB aggregate payload limits. Bound archive bytes (payload plus metadata)
to 272 MiB before allocation, using seek/tell on the opened stream, then an exact
bounded read and a one-byte growth probe. Shrink/read failure and observed growth
are errors, not partially trusted archives. The input may change after the final
probe, but cannot cause an unbounded read. No filesystem-size/path TOCTOU check is
used as the allocation authority.

Use subtractive size_t span checks before additions, field loads or narrowing.
Validate central size/count/end, bounded EOCD comments, local/central metadata
agreement and non-overlapping local spans. A sorted interval map avoids quadratic
local-overlap checks. Reject encryption, descriptors, split/ZIP64 forms and
non-regular/symlink entry attributes. Any error discards all entries.

Preflight every writer entry before creating/truncating its destination: classic
count/name widths, individual/aggregate/archive budgets and unique portable names.
Set the UTF-8 name flag; accept legacy zero-flag UTF-8 names. Reject malformed
UTF-8, controls, ADS/Windows punctuation, DOS device leaves and trailing dots/spaces.
Allow only session.json or a plain images/ leaf. Compare NFC-normalized Qt simple-fold / expanding-upper / fold keys without
renaming original UTF-8 names. This deliberately conservative collision key also
catches sharp-s/SS and dotless-I/I; it is not a full Unicode CaseFolding.txt
implementation or a promise to accept every filesystem-distinct name pair. QtCore is
already a dependency; no QtGui/widget dependency enters the parser/header.

CRC uses an immutable constexpr IEEE table, with the published 123456789 vector
and first-use concurrent package operations in the core test. Add that exact
QtCore-only suite to required TSan, without adding widget/session UI suites.

## Required evidence

Ordinary Linux/Windows corpus replay plus focused ASan/UBSan unit coverage and
4,000 real libFuzzer iterations in required build-linux. The focused CMake target
instruments the actual production SessionPackage/Utf8Path units, not a parser copy.
Private per-process fuzz temporary IO is capped at 64 KiB/input, two reused files,
one bounded output roundtrip, 1 GiB RSS and five seconds/input. Unit/fuzz logs,
mutated corpus and any crash inputs are uploaded even on gate failure. No local
heavy build/test is claimed by the proposal.

## Explicit remaining limits

Parser rejection of symlink ZIP attributes is not an atomic no-follow extraction
primitive. MainWindow extraction into QTemporaryDir is unchanged: hostile same-user
filesystem replacement races remain a separate P1-13.3 adjudication/hardening item.
The fuzz campaign is a bounded execution result, not proof against every input.
R-01 remains incomplete until HDR and Workspace targets and aggregate CI evidence
also exist. No GL, plugin, timeout-owner, raw backpressure, default-switch or
10-second directory-stutter changes are included. No early release.
