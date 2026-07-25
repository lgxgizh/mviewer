# ADR 013 — Plugin SDK Interface Freeze

**Date**: 2026-07-25
**Status**: Accepted
**Context**: P2 Product Polish — Plugin SDK

## Decision

The Plugin SDK ABI v1 is formally **frozen** as of this ADR. No breaking changes
will be introduced to the four plugin interfaces defined in `plugins/`:

| Interface      | Header / Key File               | Stability |
|----------------|----------------------------------|-----------|
| **Decoder**    | `plugins/` third-party decoder   | Frozen    |
| **Analyzer**   | `src/core/analyzer/Analyzer.h`   | Frozen    |
| **Exporter**   | `src/core/export/`               | Frozen    |
| **Importer**   | `src/core/image/ImageLoader.h`   | Frozen    |

New capabilities should be added as **new plugin types** or **optional
extensions** rather than modifying existing interfaces.

## Rationale

Per external review: "建议不要继续改，而是冻结接口". The plugin SDK has
reached a maturity level where further iteration yields diminishing returns.
Stable interfaces enable:

- Third-party plugin authors to depend on a fixed ABI.
- Separate release cadences for core vs. plugins.
- Clear backward-compatibility guarantees for users.

## Consequences

- **Positive**: Plugin ecosystem growth without fear of breakage.
- **Positive**: Core developers focus on product workflows instead of
  infrastructure.
- **Negative**: Any future ABI change requires a new major version (API v2)
  and migration guide.
- **Negative**: Current ABI limitations are permanent until v2.

## Compliance

The CI gate `PluginIntegrityTests` (see `tests/`) already validates that
all bundled plugins conform to the declared interfaces. New plugins must
pass this gate before merge.
