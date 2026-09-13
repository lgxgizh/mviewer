# ADR 016: The Qt boundary in core/domain headers, stated as it is enforced

Date: 2026-09-13
Status: Accepted

## Context

The project rule was stated as "Core: Qt-free headers" / "No Qt types in
`domain/` or `core/` headers" (AGENTS.md) and "no `<QWidget>`/`<QPainter>`/
`<QImage>` in `src/core/**/*.h`" (a comment in `core/render/RenderEngine.h`).
None of those statements was true, and the ways they were false were not
checkable:

- `core/render/RenderEngine.h` names `QPainter` and `QRect` in its **public**
  signature (`executeCommand(QPainter &, const RenderCommand &, const QRect &)`)
  behind forward declarations. The include is kept out, but a caller cannot use
  that method without Qt.
- `core/image/QtConvert.h` and `core/image/QtMetadataSemantics.h` (core headers)
  include `<QImage>` / `<QColorSpace>` / `<QImageIOReader>`. `QtConvert.h`
  claimed it "must never be included by a Qt-free public header" while the UI
  private headers (`mainwindow_p.h`, `compareworkspace_p.h`,
  `thumbnailpanel_p.h`) include it.
- At link level `mviewer_core` links `Qt6::Widgets` and `Qt6::Sql`
  (`src/CMakeLists.txt`), so "core is Qt-free" was never true as a build fact,
  only as a header-text convention.
- A grep for `#include <Q` was the obvious audit, and it is defeated by the
  forward-declaration pattern above: the check passed while the rule did not.

## Decision

State the boundary in the form that is both true and mechanically enforced:

1. **Core / Domain headers must not include Qt.** Qt in those layers' `.cpp`
   bodies is allowed (implementation detail, not part of the layer's contract).
2. **Two named Qt adapters are the exception**, because converting between core
   pixel buffers and Qt types is exactly their job:
   `core/image/QtConvert.h`, `core/image/QtMetadataSemantics.h`.
3. The rule is enforced by the `R5` check in `scripts/architecture_gate.ps1`,
   with the adapter list spelled out in that script, and covered in both
   directions by `scripts/architecture_gate_test.ps1` (a planted Qt include in a
   core header is flagged; the adapters and Qt-in-`.cpp` are not).
4. `RenderEngine.h`'s comment now says what is true: the header avoids the Qt
   *include*, but its command-execution API takes Qt painter types, so it is not
   Qt-free and must not be described as such.
5. `mviewer_core` links Qt Widgets/Sql by design; a genuinely Qt-free core
   library is a build-system change and would need its own ADR (the build system
   is frozen by AGENTS.md).

## Consequences

- The rule is auditable: R5 fails (advisory, like R1-R4) on any new Qt include in
  a core/domain header that is not one of the two adapters, so the drift this ADR
  documents cannot silently return.
- The layering claim in AGENTS.md is edited to match: "no Qt types in `domain/`
  headers; core headers must not include Qt, with two named adapter exceptions".
- `RenderEngine`'s public Qt signature stays as it is: removing it would change
  the rendering API for every caller, which is a larger, separate refactor than
  the honesty fix this ADR is about.
