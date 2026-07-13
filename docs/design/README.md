# Design Documents

## Overview

This directory contains detailed design documents that bridge specifications and implementation.

## Directory Structure

```
docs/design/
  overall.md        ← Big picture architectural overview
  data_flow.md      ← How data flows: File → ImageFrame → Cache → Render → UI
  threading.md      ← Threading model: which thread owns which data
  caching.md        ← Cache design: hierarchy, eviction, integrity
  rendering.md      ← Render pipeline: backend abstraction, command model
  analysis.md       ← Analyzer plugin design + capability framework
  compare.md        ← Compare state machine, ownership model
  plugins.md        ← Future plugin framework
```

## Relationship with Specs

- `docs/spec/` = **contract** (input/output, thread-safety, error behavior)
- `docs/design/` = **rationale** (why this design, alternatives considered, trade-offs)
- `docs/adr/` = **decision** (why we chose X over Y, frozen)

## Audience

- Future contributors (onboarding)
- Hermes Agent (self-directed development)
- Reviewers (quality audits)
