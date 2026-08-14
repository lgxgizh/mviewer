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
delivery. Foreground decode, generation guards, cancellable requests and
preload promotion remain authoritative. A ready gallery thumbnail may be
presented provisionally, but only a FullImage `ImageFrame` feeds analysis,
ROI, histogram and Pixel Inspector.

The Viewer paint path only queries Ready canonical tiles and submits Missing
tiles to the asynchronous tile manager. Tile scaling and display ICC
materialization run on DecodePool; QPixmap, texture upload and QWidget access
remain on the UI/context thread. Tile identity is image + coordinate + LOD +
render-resolution policy, never transient continuous zoom.

## Presentation contract

Browse double-click sets the Viewer fullscreen window state before showing the
top-level widget. A warm thumbnail is usable before FullImage delivery; the
full frame and visible canonical tiles then replace it without a blank frame.
A post-layout event-loop pass re-applies max Fit so the first usable frame uses
final fullscreen geometry. Viewer double-click retains the
Fit ↔ 100% gesture. `Esc` closes the Viewer directly; `F`/`F11` remain explicit
fullscreen toggles.

## Directory switch contract

The directory-change handler clears the sequence and SelectionModel before the
worker result arrives. A late scan is discarded by the existing directory
generation guard and cannot overwrite a newer directory. The UI handler does
not synchronously enumerate or sort image files.
