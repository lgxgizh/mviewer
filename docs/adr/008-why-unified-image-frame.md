# ADR-008: Why Unified ImageFrame Object

## Status

Accepted

## Context

Engines currently pass around ImageData + metadata + histogram + state separately. This creates impedance mismatch and duplicated logic.

## Decision

**ImageFrame** is the single object that owns everything about one image: pixels + metadata + histogram + decode/cache state + file info + image ID. All engines communicate exclusively through ImageFrame.

## Rationale

- **Impedance match** — one object, no mismatched state bundles
- **Simpler APIs** — take ImageFrame, not 5+ parameters
- **Centralized state** — decode/cache state in one place
- **Easy caching** — cache ImageFrame, not individual pieces
- **Future GPU** — add `gpuTexture` field without touching consumers

## Consequences

- ✅ Clean, uniform engine APIs
- ✅ State consistency guaranteed by construction
- ❌ A large object — but this reflects the inherent complexity of "an image"

## Related

- RFC-002 (ImageFrame)
- RFC-003 (Cache)
- RFC-005 (Repository)
