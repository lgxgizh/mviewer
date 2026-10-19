# Image Viewing and Analysis Software — Requirements Specification

## Project Background

Image algorithm engineers need to quickly compare multiple images (for example, before-and-after results and algorithm output comparisons) and measure luminance, RGB channels, and pixel statistics for specific regions. This project aims to provide a visual analysis platform **centered on Compare → Analyze → Export**.

---

## 1. Project Overview

Develop a visual analysis platform on Windows for image algorithm engineers. It supports efficient browsing of images in a directory, side-by-side multi-image comparison, luminance histogram display, pixel/ROI analysis, and export of reports and results.

**Technology stack**: C++ / Qt 6 Widgets

---

## 2. Interface Layout

The overall layout is divided into three areas:

```
┌──────────┬───────────────────────────────────┐
│          │                                   │
│ Dir tree │       Main image viewport         │
│          │                                   │
│          │                                   │
├──────────┤                                   │
│Thumbnail │                                   │
│   list   │                                   │
└──────────┴───────────────────────────────────┘
```

- **Upper left**: directory tree for browsing the file system
- **Lower left**: thumbnail list showing images in the current directory
- **Right**: main image viewport showing the currently selected image or the comparison view

---

## 3. Core Features

### 3.1 Directory Browsing

- The directory tree supports expand and collapse
- After a directory is clicked, the thumbnail list shows all images in that directory
- Directory scale: ≤ 1000 images per directory
- Sort order: file name by default; sorting by modification date, file size, and resolution is supported (switch by clicking the column header)

### 3.2 Thumbnail List

- Shows all images in the current directory only (the list refreshes when the directory changes; it is not shown only after an item is selected)
- Arranged in the image order of the directory
- When a thumbnail is clicked, the main viewport displays the corresponding image

### 3.3 Image Viewing

- Supported formats: JPEG, BMP, PNG, TIFF, WebP, GIF, and RAW formats currently integrated
- Default display: fit to window
- Zoom: mouse wheel
- Pan: hold the left mouse button and drag
- Response time: the complete view is shown in < 100 ms after a click

### 3.4 Multi-Image Comparison

#### How to enter

- Select 2–8 images in the thumbnail list
- Click the "Compare" button to enter comparison mode

#### Layout rules (not user-changeable)

| Image count | Layout |
| --------- | ------ |
| 2 | Left-right side by side |
| 3 | Left-center-right side by side |
| 4 | 2×2 grid |
| 5–8 | 2×4 grid |

#### Synchronized control

The UI provides a checkbox that controls the synchronization state:

- **Zoom sync**:
  - Checked: all compared images zoom together
  - Unchecked: only the image currently under the mouse zooms
- **Pan sync**:
  - Checked: all compared images move together
  - Unchecked: only the image currently under the mouse moves

### 3.5 Histogram

- Display the luminance histogram only
- Shown as a semi-transparent overlay in a corner of the image
- In comparison mode, every image displays a histogram

### 3.6 Region Selection Statistics

- Selection shape: rectangle
- Statistics: mean luminance and RGB means (R mean, G mean, B mean)
- Presentation: a statistics panel displayed in a fixed corner of the UI
- In comparison mode: compute and display statistics for the same region on every compared image

---

## 4. Performance Requirements

### 4.1 Image Dimensions

- Common size: 4096 × 3072
- Maximum size: 16384 × 12288 (4× the common size)

### 4.2 Preloading

- The images immediately before and after the current image must be preloaded
- Ensures fast display when switching images

### 4.3 Memory Cache

- Cache about 5 decoded images in memory
- Managed with an LRU policy

### 4.4 Disk Cache

- Persist thumbnails to disk
- Opening the same directory again is near-instant

---

## 5. File Operations

| Operation | Description |
| ----- | ------ |
| Delete | Move the item into the **MViewer Recycle Bin**, with recoverable Undo; this is not the Windows system Recycle Bin |
| Rename | Open a dialog to change the file name |
| Copy | Background, cancellable, progress-reporting atomic copy |
| Move | Same-volume move and a safe cross-volume fallback; undoable |
| Drag-and-drop open | Files and directories can be dragged into the main window |

---

## 6. Other Requirements

| Item | Description |
| ----- | ------ |
| UI language | Chinese |
| Multiple monitors | Supports spanning monitors |
| GIF | Supported |
| Screenshot save | Supported through the Copy Image / Save As / Export workflow |
| Statistics export | CSV, JSON, HTML, and report export are supported |

---

## 7. Technical Constraints

- Platform: Windows
- Language: C++
- GUI: Qt 6 Widgets
- Build: CMake + Ninja
- Compiler: MSVC (Windows)
### M45 qualification requirements

- CommandStack callbacks must be safe for immediate state queries and must not
  execute under the internal history mutex.
- File copy/move/delete operations must be cancellable, progress-reporting,
  lifecycle-guarded, and retain recoverable state on partial failure.
- Clipboard image paste must encode PNG off the UI thread and discard stale
  generations safely.
- Product copy must call the current delete destination **MViewer Recycle Bin**;
  native Windows Recycle Bin behavior requires a separate qualified adapter.
