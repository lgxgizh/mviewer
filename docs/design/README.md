# Design Documents

## Overview

This directory contains detailed design documents that bridge specifications and implementation.

## Directory Structure

```
docs/design/
  README.md         ← This index (directory overview)
  data_flow.md      ← How data flows: File → ImageFrame → Cache → Render → UI
  image_pipeline.md ← Full image data flow: Directory → Repository → Decoder → ImageFrame
```

## Relationship with Specs

- `docs/spec/` = **contract** (input/output, thread-safety, error behavior)
- `docs/design/` = **rationale** (why this design, alternatives considered, trade-offs)
- `docs/adr/` = **decision** (why we chose X over Y, frozen)

## Audience

- Future contributors (onboarding)
- Hermes Agent (self-directed development)
- Reviewers (quality audits)
