# ADR-001: Why Qt Widgets (not QML)

## Status
Accepted

## Context
The GUI framework choice affects the entire application architecture. Two main Qt options exist: Widgets and QML/Qt Quick.

## Decision
Use **Qt 6 Widgets** for the entire UI layer.

## Rationale
- **Stable, mature API** — Widgets has 20+ years of production use; QML's API churns more frequently
- **Pixel-perfect control** — image viewers require precise control over rendering, zoom, pan, and custom painting; Widgets gives direct QPainter access
- **Performance** — Widgets avoids QML's scene graph overhead for a primarily 2D image-viewing workload
- **Keyboard-first workflow** — Widgets has robust built-in keyboard navigation and shortcut handling
- **Team familiarity** — Widgets is universally known; QML requires specialized declarative skills
- **Interop with rendering** — easier to integrate with Direct2D/OpenGL/Vulkan surfaces via QWidget native window handles

## Consequences
- ✅ Predictable, stable API surface
- ✅ Direct QPainter access for custom rendering
- ✅ Better keyboard accessibility out of the box
- ❌ Less declarative UI (more imperative code)
- ❌ Styling requires stylesheets or custom painting (not QML's inline bindings)

## Related
- RFC-008 (Renderer backend abstraction)
- RFC-009 (UI lightweight components)
