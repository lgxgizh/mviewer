# ADR 028 — Reuse the frame-sequence probe and bound its cost

Date: 2026-10-31
Status: Accepted

## Context

Opening a multi-frame file probed it three times: `ImageRepository::load()`
probed to learn the frame count, `loadFrame()` probed again, and
`FrameSequenceReader::decode()` probed a third time. The duration probe also
decoded every frame of a short animation at native size, and only one
animation reader was cached, so the two panes of a compare kept abandoning
each other's reader.

## Decision

- `ImageRepository::load()` passes the sequence it already probed through
  `loadFrameWithSequence()` to `decodeKnownSequence()` (internal,
  `FrameSequenceDetail.h`). The public `FrameSequenceReader` contract is
  unchanged.
- The duration probe (`FrameSequenceProbe.cpp`) first sums delays with
  `jumpToNextImage()` and no pixels, then with a 1x1 scaled read, and only
  then with the historical native read. It honours cooperative cancel.
- Up to four animation readers are cached (least recently used is dropped),
  so playback plus the other compare pane do not thrash one slot.

## Consequences

Open cost for a multi-frame file is one probe. Repository callers that do not
have a sequence still use `loadFrame()`, which probes once.
