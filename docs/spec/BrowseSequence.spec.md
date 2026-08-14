# Browse Sequence Specification (M37)

## Ownership

`ThumbnailPanel` is the only component that enumerates the active directory.
Its worker applies the active type filter, sort field/direction, filename and
metadata filters, then emits `sequenceChanged(directory, paths)` after the
visible model is rebuilt.

`ImageListModel` stores that final visible order. It is the Browse Sequence
SSOT for:

- gallery order and current selection;
- Left/Right, Home/End and PageUp/PageDown navigation;
- Viewer `[current / total]` and previous/next preload;
- slideshow and Compare folder-pool seeding.

## Viewer boundary

`ImageViewer` accepts the ordered paths and the current path. It must not call
`QDir::entryInfoList()` or infer neighbors from the filesystem during decode
delivery. Foreground decode, generation guards, cancellable requests,
preload promotion, TileCache and display materialization remain unchanged.

## Presentation contract

Browse double-click sets the Viewer fullscreen window state before showing the
top-level widget. A post-layout event-loop pass re-applies Fit so the first
usable frame uses final fullscreen geometry. Viewer double-click retains the
Fit ↔ 100% gesture. `Esc` closes the Viewer directly; `F`/`F11` remain explicit
fullscreen toggles.

## Directory switch contract

The directory-change handler clears the sequence and SelectionModel before the
worker result arrives. A late scan is discarded by the existing directory
generation guard and cannot overwrite a newer directory. The UI handler does
not synchronously enumerate or sort image files.
