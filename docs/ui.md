# MViewer UI Specification

## Status — read this first

This file is the **design spec** for the UI, not a description of the shipped
product. Several sections describe an intended design that was deliberately not
built: a thumbnail *sidebar* layout, a Direct3D 11 backend, momentum panning and
touch input, IPTC/XMP metadata sections with per-section collapse, a Ken Burns
slideshow, system light/dark switching, and a virtualized thumbnail scroll area.

For what the product actually does, use:

- [`USER_GUIDE.md`](USER_GUIDE.md) — the user-facing guide (browse / compare).
- The in-app **F1** shortcut dialog — generated from the shipped keymap.
- [`ROADMAP_PUBLIC.md`](ROADMAP_PUBLIC.md) — what shipped in which version.

Passages that no longer match the product carry an explicit
**[not implemented]** marker. The shortcut tables were corrected to the shipped
keymap in 1.0.30 (they previously listed an intended keymap: `Space` = next
image, `F` = fit to window, `Ctrl+O` = open file — none of which the product
binds).

---

## Overview

The MViewer user interface is designed for efficient image browsing with minimal
visual clutter. The interface prioritizes keyboard navigation and provides a
native desktop experience through Qt 6 Widgets.

---

## Design Principles

1. **Content first** — The image is the focus; UI chrome is minimal
2. **Keyboard-first** — Every action is accessible via keyboard
3. **Native look** — Respect platform conventions and system theme
4. **Responsive** — UI never blocks on I/O or decode operations
5. **Minimal animations** — No decorative transitions or effects

---

## Main Window Layout

```
┌─────────────────────────────────────────────────────────────┐
│  Menu Bar                                                   │
├─────────────────────────────────────────────────────────────┤
│  Gallery toolbar (view mode, sort, filter, search)          │
├──────────────────────────────┬──────────────────────────────┤
│                              │                              │
│      Thumbnail gallery       │   Preview / analysis dock    │
│      (grid · list · detail · │   (metadata panel, analysis  │
│       filmstrip · compact)   │    panel, histogram)         │
│                              │                              │
├──────────────────────────────┴──────────────────────────────┤
│  Status Bar (selection, image info, zoom, position)        │
└─────────────────────────────────────────────────────────────┘
```

The image itself opens in a dedicated **viewer window** (`ImageViewer`), and
Compare opens its own workspace window — neither is a docked canvas. The sketch
above is the main window only.

### Default Panel Visibility

| Panel | Default | Toggle |
| ------- | --------- | ------------- |
| Menu Bar | Visible | Always visible |
| Gallery toolbar | Visible | View menu |
| Thumbnail gallery | Visible | Always visible (the main content) |
| Metadata panel | Hidden | View → Metadata (`Ctrl+I`) |
| Analysis panel | Hidden | `Alt+H` |
| Status Bar | Visible | View menu |

---

## Menu Bar

### File

| Action | Shortcut | Description |
| -------- | ---------- | ------------- |
| Open Folder... | `Ctrl+O` | Open a folder for browsing (`QKeySequence::Open`) |
| Open File... | `Ctrl+Shift+O` | Open a single image file |
| Exit | `Ctrl+Q` / `Alt+F4` | Close application |

> Corrected in 1.0.30: the table previously had `Ctrl+O` = Open File and
> `Ctrl+Shift+O` = Open Folder, the reverse of the shipped bindings
> (`src/mainwindow_ui_layout.cpp`).

### View

| Action | Shortcut | Description |
| -------- | ---------- | ------------- |
| Zoom In | `Ctrl++` / `Ctrl+=` | Increase zoom level |
| Zoom Out | `Ctrl+-` | Decrease zoom level |
| Fullscreen | `F11` (viewer: `F` or `F11`) | Toggle fullscreen mode |
| Analysis panel | `Alt+H` | Toggle the analysis panel (histogram by default) |
| Metadata panel | `Ctrl+I` | Toggle the metadata panel |
| Search | `Ctrl+Shift+F` | Focus the search field |
| Batch | `Ctrl+Shift+B` | Batch operations dialog |
| Batch analyze | `Ctrl+Shift+A` | Batch analysis over the selection |
| History back / forward | `Alt+Left` / `Alt+Right` | Browse history |
| Directory back / forward | `Ctrl+Alt+Left` / `Ctrl+Alt+Right` | Directory navigation |
| Undo / Redo | `Ctrl+Z` / `Ctrl+Y` | Command stack |
| Keyboard shortcuts | `F1` | Shortcut reference |

### Navigate

| Action | Shortcut | Description |
| -------- | ---------- | ------------- |
| Next Image | `→` | Go to next image |
| Previous Image | `←` | Go to previous image |
| Open / activate | `Enter` | Open the selection in the viewer |
| First Image | `Home` | Go to first image in folder |
| Last Image | `End` | Go to last image in folder |
| Previous / Next page | `PageUp` / `PageDown` | Page through the gallery |
| Refresh | `F5` | Rescan the current directory |

> `Space` is **not** "next image" in the shipped build: in the browse window it
> starts a quick compare of the current image against the next one
> ([`USER_GUIDE.md`](USER_GUIDE.md)).

### Image

| Action | Shortcut | Description |
| -------- | ---------- | ------------- |
| Rename | `F2` | Rename the selected file |
| Delete (MViewer staging) | `Delete` | Move the selection to the MViewer trash |
| Rotate | `R` (viewer) | Rotate the displayed image |
| Colour label | `0`–`6` | Assign a colour label to the selection |

> The original spec listed `R` / `Shift+R` / `H` / `V` as rotate and flip
> bindings of the main window. Only the viewer binds `R`; flips are done through
> the toolbar and the command stack, which is what makes them undoable.

### Slideshow

| Action | Shortcut | Description |
|--------|----------|-------------|
| Start/Stop | `S` | Toggle slideshow |
| Settings... | | Configure interval, order |

> `Space` toggles a quick compare, not the slideshow.

### Help

| Action | Shortcut | Description |
|--------|----------|-------------|
| Keyboard Shortcuts | `F1` | Show shortcut reference |
| 使用说明 (User guide) | | Open `docs/USER_GUIDE.md` in the browser |
| About | | Show version and credits |

---

## Keyboard Shortcuts (Complete Reference)

> **Source of truth:** the in-app **F1** dialog and
> [`USER_GUIDE.md`](USER_GUIDE.md). The tables below mirror them. The zoom-level
> table that used to live here (`2` = 200%, `5` = 50%, `Ctrl+0` = fit) was never
> bound, and `PageDown` / `PageUp` navigate the gallery, not the image.

### Browse window (MainWindow)

| Key | Action |
| ----- | -------- |
| `←` / `→` | Previous / next image |
| `Home` / `End` | First / last image |
| `PageUp` / `PageDown` | Previous / next page |
| `Enter` | Open the selection in the viewer |
| `Ctrl+O` | Open folder |
| `Ctrl+Shift+O` | Open file |
| `P` / `C` | Open Compare (needs 2–8 selected images) |
| `Space` | Quick compare: current image against the next one |
| `Alt+H` | Analysis panel |
| `I` / `M` | Image-info overlay / metadata overlay |
| `S` | Slideshow |
| `F` | Fullscreen |
| `F1` | Full shortcut reference |
| `F2` | Rename |
| `F5` | Refresh the directory |
| `Delete` | Move to MViewer staging |
| `Ctrl+Shift+B` / `Ctrl+Shift+A` | Batch / batch analyze |
| `Ctrl+Z` / `Ctrl+Y` | Undo / redo |
| `Ctrl+Shift+F` | Search |
| `0`–`6` | Colour label |
| `Esc` | Clear the current state (e.g. leave fullscreen) |

### Viewer window (ImageViewer)

| Key | Action |
| ----- | -------- |
| `←` / `→` | Previous / next image |
| `+` / `-` | Zoom in / out |
| `F` or `F11` | Fullscreen |
| `R` | Rotate |
| `Esc` | Close the viewer |
| `,` / `.` | Previous / next frame (animated / multi-page sources) |
| `Space` | Play / pause animation (animated sources only) |

### Compare window (CompareWorkspace)

| Key | Action |
| ----- | -------- |
| `Z` / `D` | Sync zoom / sync drag |
| `Space` (hold) | Blink: show B while held |
| `B` / `S` / `W` / `O` / `K` | Blink / split / swipe / overlay / checkerboard (2 images) |
| `H` | Difference highlight |
| `R` / `L` | Sync crosshair / pixel link |
| `X` | Swap A/B |
| `F` | Fit all panes |
| `PageUp` / `PageDown` | Previous / next pair |
| `Shift+1`…`Shift+5` | Channel: RGB / R / G / B / Y |
| `?` | Shortcut hints |
| `Esc` | Clear the ROI, then exit Compare |

---

## Image Canvas

### Rendering

- Custom `QWidget` (`RawImageView`) painting an `ImageFrame`; the full-size view
  is a separate window (`ImageViewer`).
- Qt's raster paint engine by default. Tiles may be uploaded to GPU textures
  through `GpuTileUploader` when an OpenGL context is current **and** the opt-in
  `MVIEWER_GPU` environment variable is set; the CPU compositor remains the
  verified default.
- **[not implemented]** a Direct3D 11 surface — the original design named D3D11
  on Windows, but no D3D backend exists in `src/`.

### Interaction

| Input | Action |
| ------- | -------- |
| Mouse drag | Pan image |
| Mouse wheel | Zoom in/out (cursor-centered) |
| Double-click | Toggle fit-to-window / actual size |
| Right-drag (Compare) | Draw the ROI |
| Touch pinch / drag | **[not implemented]** |

### Zoom Behavior

- Zoom range follows `Viewport::zoomAt` (0.05×–50×); a value restored from
  settings is clamped to that range.
- Zoom is centered on the cursor position.
- Fit-to-window applies per image until the user zooms.

### Pan Behavior

- Boundary clamping (no infinite panning)
- `Fit` restores the centered view
- **[not implemented]** momentum / inertia

---

## Thumbnail Gallery

### Layout

**[not implemented] as originally drawn** (a 150–300 px vertical strip). The
shipped gallery is `ThumbnailPanel`, the main dock of the window, with grid /
list / detail / filmstrip / compact view modes, a toolbar, and a resizable panel.

### Thumbnail Display

| Property | Value |
| ---------- | ------- |
| Aspect ratio | Preserved |
| Selection | Highlighted; the current image is marked |
| Label | Optional filename overlay in the larger view modes |

### Behavior

- Click to select and show the image; `Ctrl` / `Shift`-click extends the
  selection
- Context menu: open in viewer, compare, rate / label, batch rename / move /
  delete
- Drag to resize the panel

### Performance

- Only the visible range (plus a small neighbour buffer) is requested first —
  `ThumbnailPipeline` priorities are visible → neighbours → rest
- Thumbnails come from `ThumbnailCache` (bounded on-disk PNG keyed by
  path + mtime + size + requested size + schema) and are generated off the UI
  thread
- **[not implemented]** the "virtualized scroll area, 60 fps" claim of the
  original spec

---

## Metadata Panel

### Layout

- Docked panel on the right, resizable, tree of metadata groups
- The on-image overlay (`I` / `M`) shows a compact subset of the same data

### Sections

| Section | Content |
| --------- | --------- |
| File Info | Filename, path, size, dimensions, format |
| EXIF | Camera, lens, exposure, ISO, focal length, date |
| RAW | Embedded-preview information for RAW containers |
| Colour | Colour space, bit depth, ICC profile name |
| **[not implemented]** IPTC / XMP | Title, keywords, copyright, creator |

### Behavior

- Updates asynchronously on image navigation; one shared single-flight service
  (`MetadataPresentationService`) coalesces the overlay, panel and status-bar
  requests into one background read per path
- Never blocks image display
- Delivers an empty snapshot for a deleted, corrupt or unsupported file instead
  of leaving stale content on screen

---

## Status Bar

### Layout

```
[Selection / image info]      [Zoom level]      [Folder position]
```

### Content

| Position | Content | Example |
| ---------- | --------- | --------- |
| Left | Image info | `1920×1080 JPEG 2.3 MB` |
| Center | Zoom level | `100%` |
| Right | Position | `42 / 1000` |

### Behavior

- Updates on navigation and zoom
- Reports why an action is unavailable (e.g. "Compare needs 2–8 images")
- No user interaction (display only)

---

## Fullscreen Mode

### Behavior

- Toggle with `F11` (viewer: `F` or `F11`)
- Hide all panels and chrome
- Black background
- Image centered, fit to screen
- Exit with `Esc` or `F11`

### Interaction

| Input | Action |
| ------- | -------- |
| `←` / `→` | Navigate |
| `Esc` | Exit fullscreen |

---

## Slideshow Mode

### Behavior

- Toggle with `S` in the browse window
- Configurable interval and order
- Loop or stop at the end
- **[not implemented]** Ken Burns effect

### Controls

| Input | Action |
| ------- | -------- |
| `S` | Start / stop |
| `←` / `→` | Previous / next image |
| `Esc` | Leave fullscreen / clear state |

---

## Theme & Appearance

### System Theme

- Qt's native style with the platform palette; the Compare workspace uses a dark
  chrome palette
- **[not implemented]** automatic light/dark switching on system theme change,
  and a manual override in settings

### Fonts

- System default font
- Monospace for metadata values
- Compare captions and the filename overlay use a larger bold type (1.0.29)

---

## Settings Persistence

### Storage

| Platform | Location |
|----------|----------|
| Windows | `%APPDATA%\MViewer\` (`QSettings` + JSON state files under the app config location) |
| Linux | `~/.config/MViewer/` |

### Persisted Settings

- Window size and position
- Panel visibility states
- Panel sizes
- Thumbnail size
- Viewer zoom / pan for the last image (clamped when restored)
- Cache size limits
- Slideshow interval
- Last opened directory, recent folders, favourites
- Workspace and compare sessions (plus a crash-recovery snapshot)

---

## Accessibility

### Keyboard Navigation

- Every gallery and viewer action has a keyboard path; the full table is on `F1`
- Focus indicators come from the platform style
- **[not implemented]** a documented Tab order across the docks

### Screen Reader

- Keyboard shortcut help (`F1`) and the user guide are the documented surfaces
- **[not implemented]** image descriptions / panel state announcements

---

## Qt Widget Hierarchy

```
MainWindow (QMainWindow)
├── QMenuBar
├── ThumbnailPanel            (gallery: view modes, toolbar, model/view)
├── PreviewPanel              (selected-image preview + histogram)
├── MetadataPanel             (docked metadata tree)
├── AnalysisPanel             (analyzer results, histograms, ROI stats)
└── QStatusBar

ImageViewer (separate window, QWidget)
└── RawImageView              (tile-based paint surface, optional GPU upload)

CompareWorkspace (separate window, QDialog)
├── RawImageView × N          (2–8 panes)
├── ROI / measurement HUD + table
└── Inspector + metrics strip
```

### Custom Widgets

| Widget | Parent | Purpose |
| -------- | -------- | --------- |
| `RawImageView` | viewer / Compare pane | Image display through the Qt paint pipeline (tiles, overlay, ROI) |
| `ThumbnailPanel` | MainWindow | Gallery with view modes, filtering, sorting and selection |
| `PreviewPanel` | MainWindow | Selected-image preview and histogram |
| `MetadataPanel` | MainWindow | Metadata tree for the current image |
| `AnalysisPanel` | MainWindow | Analyzer selection, results, histogram and ROI statistics |
| `CompareWorkspace` | (window) | Multi-pane compare: sync, modes, ROI, diff, metrics, export |

---

## M45 UI convergence notes

Long-running file operations show cancellable progress and complete through a
generation-guarded UI handoff. Ctrl+V accepts an image immediately while PNG
encoding runs in the I/O scheduler. Delete actions are labeled **移到 MViewer
回收站** to distinguish the current per-user staging semantics from Windows'
native Recycle Bin.
