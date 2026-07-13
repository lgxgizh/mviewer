# RFC-009: UI Lightweight Components

## Status
Draft → Accepted

## Priority
P1

## Goal
CompareWorkspace and all UI widgets remain lightweight. No business logic inside widgets.

## Components
```
CompareWorkspace
    ├── Toolbar (buttons, no logic)
    ├── ViewGrid (render cells)
    ├── Overlay (diff highlight)
    ├── PixelInspector (read-only display)
    ├── StatusBar (info)
    └── SelectionOverlay (ROI box)
```

## Rule
Widgets only display state. Business logic goes to domain/core.

## Widget responsibilities
- Render current state (pixmaps, images, overlays)
- Emit signals on user input (mouse, keyboard)
- Receive updates via setters/slots

## NOT widget responsibilities
- Compute statistics → AnalysisEngine
- Manage cache → CacheManager
- Decode images → ImageRepository
- Compare images → CompareEngine

## Consequences
- Pure presentation
- Easy UI refactoring
- Signals/slots keep coupling loose

## Related
- RFC-010 (Domain First)
- ADR-010 (Why UI widgets lightweight)
