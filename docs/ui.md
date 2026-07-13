# MViewer UI Specification

## Overview

The MViewer user interface is designed for efficient image browsing with minimal visual clutter. The interface prioritizes keyboard navigation and provides a native desktop experience through Qt 6 Widgets.

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
│  Toolbar (optional, collapsible)                            │
├──────────┬──────────────────────────────────┬───────────────┤
│          │                                  │               │
│Thumbnail │                                  │  Metadata     │
│Sidebar   │         Image Canvas             │  Panel        │
│          │                                  │  (collapsible)│
│          │                                  │               │
│          │                                  │               │
├──────────┴──────────────────────────────────┴───────────────┤
│  Status Bar                                                 │
└─────────────────────────────────────────────────────────────┘
```

### Default Panel Visibility

| Panel | Default | User Toggle |
|-------|---------|-------------|
| Menu Bar | Visible | Always visible |
| Toolbar | Hidden | View → Toolbar |
| Thumbnail Sidebar | Visible | View → Thumbnails (T) |
| Metadata Panel | Hidden | View → Metadata (M) |
| Status Bar | Visible | View → Status Bar |

---

## Menu Bar

### File

| Action | Shortcut | Description |
|--------|----------|-------------|
| Open File... | `Ctrl+O` | Open single image file |
| Open Folder... | `Ctrl+Shift+O` | Open folder for browsing |
| Exit | `Alt+F4` / `Ctrl+Q` | Close application |

### View

| Action | Shortcut | Description |
|--------|----------|-------------|
| Zoom In | `+` / `Ctrl+=` | Increase zoom level |
| Zoom Out | `-` | Decrease zoom level |
| Fit to Window | `F` | Scale image to fit canvas |
| Actual Size | `1` | 100% zoom (1:1 pixel) |
| Stretch to Window | | Fill canvas (ignore aspect) |
| Fullscreen | `F11` | Toggle fullscreen mode |
| Thumbnails | `T` | Toggle thumbnail sidebar |
| Metadata | `M` | Toggle metadata panel |
| Toolbar | | Toggle toolbar |
| Status Bar | | Toggle status bar |

### Navigate

| Action | Shortcut | Description |
|--------|----------|-------------|
| Next Image | `→` / `Space` | Go to next image |
| Previous Image | `←` / `Backspace` | Go to previous image |
| First Image | `Home` | Go to first image in folder |
| Last Image | `End` | Go to last image in folder |
| Go to... | `Ctrl+G` | Open "go to image" dialog |

### Image

| Action | Shortcut | Description |
|--------|----------|-------------|
| Rotate Clockwise | `R` | Rotate 90° clockwise |
| Rotate Counter-CCW | `Shift+R` | Rotate 90° counter-clockwise |
| Flip Horizontal | `H` | Mirror horizontally |
| Flip Vertical | `V` | Mirror vertically |

### Slideshow

| Action | Shortcut | Description |
|--------|----------|-------------|
| Start/Stop | `Space` (when not navigating) | Toggle slideshow |
| Settings... | | Configure interval, order |

### Help

| Action | Shortcut | Description |
|--------|----------|-------------|
| Keyboard Shortcuts | `F1` | Show shortcut reference |
| About | | Show version and credits |

---

## Keyboard Shortcuts (Complete Reference)

### Navigation

| Key | Action |
|-----|--------|
| `→` or `PageDown` | Next image |
| `←` or `PageUp` | Previous image |
| `Home` | First image |
| `End` | Last image |
| `Space` | Next image (or slideshow toggle) |
| `Backspace` | Previous image |

### Zoom

| Key | Action |
|-----|--------|
| `+` or `=` | Zoom in |
| `-` | Zoom out |
| `F` | Fit to window |
| `1` | Actual size (100%) |
| `2` | 200% zoom |
| `5` | 50% zoom |
| `Ctrl+0` | Fit to window (alternate) |

### View

| Key | Action |
|-----|--------|
| `F11` | Toggle fullscreen |
| `T` | Toggle thumbnail sidebar |
| `M` | Toggle metadata panel |
| `Esc` | Exit fullscreen / Close panels |

### Image Manipulation

| Key | Action |
|-----|--------|
| `R` | Rotate 90° clockwise |
| `Shift+R` | Rotate 90° counter-clockwise |
| `H` | Flip horizontal |
| `V` | Flip vertical |

### File

| Key | Action |
|-----|--------|
| `Ctrl+O` | Open file |
| `Ctrl+Shift+O` | Open folder |
| `Ctrl+Q` | Quit |

---

## Image Canvas

### Rendering

- Custom `QWidget` with platform-specific rendering backend
- Direct3D 11 surface on Windows (via `QWindow` or `HWND` interop)
- OpenGL surface on Linux (via `QOpenGLWidget`)
- GPU-accelerated zoom and pan
- Bilinear filtering for smooth scaling

### Interaction

| Input | Action |
|-------|--------|
| Mouse drag | Pan image |
| Mouse wheel | Zoom in/out (cursor-centered) |
| Double-click | Toggle fit-to-window / actual size |
| Middle-click drag | Pan image (alternative) |
| Touch pinch | Zoom (future) |
| Touch drag | Pan (future) |

### Zoom Behavior

- Zoom levels: 1% to 1600%
- Default zoom step: 10% per wheel tick
- Zoom centered on cursor position
- Smooth zoom (no discrete steps perceived)
- Maintain zoom level across image switches (user-configurable)

### Pan Behavior

- Pan only when image larger than canvas
- Momentum/inertia (optional, minimal)
- Boundary clamping (no infinite panning)
- Pan resets on "Fit to Window"

---

## Thumbnail Sidebar

### Layout

- Vertical strip on left side of window
- Width: 150-300px (user-resizable)
- Thumbnails arranged top-to-bottom
- Scrollbar for overflow

### Thumbnail Display

| Property | Value |
|----------|-------|
| Thumbnail size | 128-256px (user-configurable) |
| Aspect ratio | Preserved |
| Border | 2px, highlight on selected |
| Spacing | 4px between thumbnails |
| Label | Optional filename overlay |

### Behavior

- Click to select and display image
- Scroll to navigate through folder
- Selected thumbnail highlighted
- Current image indicator (colored border)
- Drag to resize panel width
- Context menu: Open in new window, Show in folder, File info

### Performance

- Only visible thumbnails rendered (virtual scrolling)
- Thumbnails loaded from cache (L3 → L4 → generate)
- Priority: visible range first, then scroll buffer
- Smooth scrolling at 60fps

---

## Metadata Panel

### Layout

- Vertical strip on right side of window
- Width: 250-400px (user-resizable)
- Sections with collapsible headers

### Sections

| Section | Content |
|---------|---------|
| File Info | Filename, path, size, dimensions, format |
| EXIF | Camera, lens, exposure, ISO, focal length, date |
| IPTC | Title, keywords, copyright, creator |
| XMP | Rating, labels, description |
| Color | Color space, bit depth, ICC profile name |

### Behavior

- Updates asynchronously on image navigation
- No blocking of image display
- Scrollable content
- Copy value on click (optional)
- Section collapse state persisted

---

## Status Bar

### Layout

```
[Image info]          [Zoom level]          [Folder position]
```

### Content

| Position | Content | Example |
|----------|---------|---------|
| Left | Image info | `1920×1080 JPEG 2.3 MB` |
| Center | Zoom level | `100%` |
| Right | Position | `42 / 1000` |

### Behavior

- Updates on navigation and zoom
- No user interaction (display only)
- Hidden in fullscreen mode (or minimal overlay)

---

## Fullscreen Mode

### Behavior

- Toggle with `F11`
- Hide all panels and chrome
- Black background
- Image centered, fit to screen
- Status bar overlay (auto-hide after 2 seconds)
- Exit with `Esc` or `F11`

### Interaction

| Input | Action |
|-------|--------|
| `→` / `←` | Navigate |
| `Esc` | Exit fullscreen |
| Mouse move | Show cursor (auto-hide after 2s) |
| `Space` | Slideshow toggle |

---

## Slideshow Mode

### Behavior

- Toggle with `Space` (when not navigating)
- Fullscreen or windowed (user-configurable)
- Configurable interval (1s - 60s, default 5s)
- Forward or random order
- Loop or stop at end
- Ken Burns effect (optional, future)

### Controls

| Input | Action |
|-------|--------|
| `Space` | Pause/resume |
| `→` | Next (manual advance) |
| `←` | Previous |
| `Esc` | Stop slideshow |
| `+/-` | Adjust interval (while running) |

---

## Theme & Appearance

### System Theme

- Respect system light/dark mode (Windows 11 / Linux GTK)
- Auto-switch on system theme change
- Manual override in settings

### Color Scheme

| Element | Light | Dark |
|---------|-------|------|
| Background | `#FFFFFF` | `#1E1E1E` |
| Canvas | `#808080` (neutral gray) | `#404040` |
| Text | `#000000` | `#FFFFFF` |
| Accent | System accent | System accent |
| Border | `#E0E0E0` | `#404040` |

### Fonts

- System default font (Segoe UI on Windows, system font on Linux)
- Monospace for metadata values
- Minimum size: 11pt

---

## Settings Persistence

### Storage

| Platform | Location |
|----------|----------|
| Windows | `%APPDATA%\MViewer\settings.json` |
| Linux | `~/.config/mviewer/settings.json` |

### Persisted Settings

- Window size and position
- Panel visibility states
- Panel sizes (sidebar width, metadata width)
- Thumbnail size
- Zoom behavior (maintain across images)
- Cache size limits
- Slideshow interval
- Theme preference
- Last opened directory

---

## Accessibility

### Keyboard Navigation

- All actions accessible via keyboard
- Tab order: Menu → Sidebar → Canvas → Metadata
- Focus indicators visible
- No mouse-only actions

### Screen Reader

- Image descriptions via alt text (future)
- Panel state announcements (future)
- Keyboard shortcut help (F1)

### High Contrast

- Respect system high contrast mode
- Focus indicators visible in all themes
- Minimum contrast ratio: 4.5:1 (WCAG AA)

---

## Qt Widget Hierarchy

```
QMainWindow
├── QMenuBar
├── QToolBar (optional)
├── QSplitter (horizontal)
│   ├── ThumbnailScrollArea
│   │   └── ThumbnailListWidget (custom)
│   ├── ImageCanvas (custom QWidget)
│   │   └── [D3D11/GL surface]
│   └── MetadataPanel (custom QWidget)
│       └── QScrollArea
│           └── QTreeWidget / QFormLayout
└── QStatusBar
```

### Custom Widgets

| Widget | Parent | Purpose |
|--------|--------|---------|
| `ImageCanvas` | QWidget | Image display with GPU rendering |
| `ThumbnailListWidget` | QWidget | Virtualized thumbnail grid |
| `ThumbnailScrollArea` | QScrollArea | Scrollable thumbnail container |
| `MetadataPanel` | QWidget | Collapsible metadata display |
