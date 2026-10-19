# MViewer User Guide

English | [简体中文](USER_GUIDE.zh_CN.md)

A professional visual analysis platform for imaging algorithm engineers (Browse → Compare → Analyze → Report → Export → Workspace).  
In the app: **Help → User Guide** (shown in the current UI language). Full key table: **F1**.

---

## 1. Browse

1. **Ctrl+O** opens an image folder; the thumbnail grid loads asynchronously.
2. **Click** to view large; **Ctrl+click** / **Shift+click** to multi-select. **Ctrl+Shift+A** Cancel selection (deselect).
3. **View zoom**:
   - Scroll to zoom freely; double-click toggles between "Fit to Window" and 100% actual size.
   - Quick presets (View menu): **50%**, **100%**, **200%**, **400%**, **800%** (pixel-grid level).
4. **Geometric transforms** (overwrite the original file; JPEG is re-encoded at quality 95):
   - **Ctrl+R** / **Ctrl+Shift+R**: rotate 90° clockwise / counterclockwise and overwrite the original.
   - **Ctrl+Shift+H** / **Ctrl+Shift+V**: flip horizontally / vertically and overwrite the original.
   - With no image selected the menu items are disabled and the status bar explains why (never a silent no-op).
5. **Channel isolation and analysis**:
   - **Shift+1…6**: original RGB / red (R) / green (G) / blue (B) / luminance (Y) / value (HSV-V).
   - **Shift+C**: copy the pixel color under the cursor (Hex, RGB, Float 0-1 and HSV formats).
6. **Color management (CMS)**:
   - "View → Color Management (CMS)": when on, colors are converted with the display ICC profile; when off, RGB passes straight through (same as FastStone).
7. **Animations and multi-frame containers**:
   - GIF, animated WebP and multi-page TIFF are supported.
   - **Space** plays/pauses, **←** / **→** step one frame.
8. **Workspace and search**:
   - **Tab**: focus browse mode (hide the side panels and view full window).
   - **Ctrl+Shift+F**: global search panel.
   - **Alt+H**: analysis panel (histogram, statistics and quality metrics computed instantly). The panel analyzes the current image only; two-image comparison lives in Compare.
   - When the viewer is full screen, the context-menu "Analyze" leaves full screen first and then opens the panel in the main window.

---

## 2. Compare

1. **Entering Compare**:
   - Select **2–8 images** in the thumbnails and press **P** or **C** (with fewer than 2 the status bar explains why).
   - While browsing a single image, **Space** quick-compares it in place with the next image.
2. **Two-image compare modes** (2 images only):
   - **B** Flicker (blink)
   - **S** Split (side by side)
   - **W** Swipe (wipe)
   - **O** Overlay (transparent overlay)
   - **K** Checkerboard interleaved compare
   - Overlay opacity and checkerboard size sliders appear automatically with their mode.
3. **Difference and alignment**:
   - **H**: difference highlight heatmap.
   - The bottom status strip computes and shows **PSNR / SSIM** live.
   - Click "Align" to register the images with an integer-pixel translation before metrics are computed.
4. **Region of interest (ROI)**:
   - **Right-drag** on an image to draw an ROI.
   - A right-click without dragging opens the pane menu (including "Export Adjusted Image..."); dragging still draws an ROI.
   - The box header shows the reference label (A / B); the HUD shows the region's pixel mean, variance, extremes and histogram live.
   - Multi-level synchronized selection and TSV measurement export are supported.
5. **Peek and layouts**:
   - Hold **1–8** to temporarily preview image N in the pane under the cursor; release to restore. Without a pane under the cursor the focused pane is used, otherwise the first pane. Out-of-range digits only show a hint. Layout presets are **Ctrl+2 / Ctrl+4 / Ctrl+8**. **Ctrl+1** is 100% actual size, **0 / Ctrl+0** fits the window.
6. **Synchronization**:
   - **Z** / **D**: toggle sync zoom / sync pan.
   - "Fit window (F)" on the tool row (next to Analyze and Export Report; shortcut **F** / **Ctrl+0**): with "Uniform Pixel Scale" checked, every pane fits at one shared scale; unchecked, each pane fits its own resolution so images with the same field of view appear at a similar size. Peek, flicker or image swaps keep that on-screen size.
   - **R**: synchronized crosshair.
   - **L**: pixel link markers (compare the RGB and Δ of corresponding pixels across images).
   - **X**: swap the A / B panes.
   - The images to compare are chosen in Browse; Compare no longer steps to a previous / next group.
   - **Esc**: clears the ROI selection first; press Esc again to exit Compare.
7. **Rotate / flip in Compare is a preview**:
   - Side-panel buttons or **Ctrl+R** / **Ctrl+Shift+R** / **Ctrl+Shift+H** / **Ctrl+Shift+V** change only the current pane's display and **do not overwrite the original file**.
   - The target is the pane picked for editing, otherwise the pane under the cursor / the locked reference; without a target the status bar says so instead of silently changing pane 1.
8. **Per-pane adjustments and export**:
   - The Adjust panel sets brightness, contrast, gamma, white balance and USM sharpening (amount 0%–500%, radius 0.1–20 px, threshold 0–255, optional luminance only) for the current pane. Off by default. Preview, difference and histogram use the same adjustments and **do not overwrite the original file**. "Reset Adjustments" also turns USM off.
   - "Export Adjusted Image..." (pane context menu or Adjust panel) writes the current pane at full resolution with brightness/contrast/gamma, white balance, USM, rotate/flip and crop applied to a new PNG or TIFF. The default name is `<original>_adjusted.png`, saved next to the source or in the last export folder.

---

## 3. Batch and export

1. **Batch format conversion and rename**:
   - Press **Ctrl+Shift+B** to open the batch processing dialog.
   - Supports format conversion (PNG, JPEG, WebP, TIFF and more), batch resize, color space conversion and flexible rename templates.
2. **Batch analysis export**:
   - Press **Ctrl+Alt+A** (or gallery context menu Batch Analysis Export).
   - Runs the active Analyzer pipeline over the selected images and writes CSV / TSV in one step.
3. **Analysis report**:
   - "File → Export Report...": generates a standalone HTML report with previews, difference heatmaps, ROI statistics and PSNR/SSIM charts.
4. **Image metadata inventory in Export Images**:
   - The "Image metadata inventory CSV / JSON / HTML" modes in "File → Export Images..." write each image's file name, size and channel means; this is not the analysis report above.
   - With these modes or "Copy to clipboard", format, quality, resize, watermark, crop and rename are hidden. Inventories still need an output folder; the clipboard writes no file and needs none.

---

## 4. Workspace and project persistence

1. **Save and open a workspace** (`.mvws`):
   - Stores the open folder, the selected compare set, ROI coordinates and analyzer state.
   - Restores the working context quickly across sessions or machines.
2. **Project files** (`.mvproj`):
   - Store the full set of dataset roots, several workspaces and custom analysis pipeline configuration.
3. **Crash recovery**:
   - If the app closes unexpectedly, the next start detects the open recovery marker and offers to restore the last session.

---

## 5. Language

- **Tools → Preferences... → UI language**: Follow system (default), English or 简体中文.
- Restart MViewer after changing the language so every window uses it.

---

## 6. Shortcut reference

### Compare window

| Key | Action |
|---|---|
| **Z** / **D** | Toggle sync zoom / sync pan |
| **Space** (hold) | Show the other side under the cursor; release to restore |
| **B** / **S** / **W** / **O** / **K** | Flicker / Split / Swipe / Overlay / Checkerboard |
| **H** | Difference highlight and heatmap |
| **R** / **L** | Sync crosshair / pixel link |
| **X** | Swap the A/B pane images |
| **F** | Fit all images to the window |
| **1…8** | Hold to preview image N (pane under the cursor; otherwise the focused pane or pane 1). Out-of-range digits only show a hint |
| **Ctrl+1** | 100% actual size |
| **0 / Ctrl+0** | Fit to window (same as F) |
| **Ctrl+2 / Ctrl+4 / Ctrl+8** | 2 / 4 / 8 pane layouts |
| **]** / **[** | Diff threshold +5 / -5 |
| **.** / **,** | Overlay opacity +5% / -5% |
| **Shift+1…6** | Single channel: RGB / R / G / B / Y / HSV-V |
| **Ctrl+R** / **Ctrl+Shift+R** | Preview-rotate the current pane (file not written) |
| **Ctrl+Shift+H** / **Ctrl+Shift+V** | Preview-flip the current pane (file not written) |
| **Esc** | Clear the ROI selection or exit Compare |
| **?** | Bottom hint strip |

### Browse window

| Key | Action |
|---|---|
| **Ctrl+O** | Open folder |
| **Ctrl+Shift+O** | Open a single file |
| **P** / **C** | Compare the 2–8 selected images |
| **Space** | Quick-compare the current image with the next (or play/pause an animation) |
| **Alt+H** | Toggle the analysis panel (single image; in full-screen viewer, "Analyze" leaves full screen first) |
| **Ctrl+Shift+F** | Toggle the global search panel |
| **Ctrl+I** / **I** | Toggle the EXIF metadata overlay |
| **Ctrl+R** / **Ctrl+Shift+R** | Rotate 90° clockwise / counterclockwise (overwrites the original; preview only in Compare) |
| **Ctrl+Shift+H** / **Ctrl+Shift+V** | Flip horizontally / vertically (overwrites the original; preview only in Compare) |
| **Shift+C** | Copy the pixel color at the cursor |
| **Tab** | Toggle focus browse mode |
| **S** | Start the slideshow |
| **Ctrl+Shift+B** | Open the batch processing dialog |
| **Ctrl+Shift+A** | Cancel selection (deselect) |
| **Ctrl+Alt+A** | Batch Analysis Export |
| **←** / **→** | Previous / next image |
| **F1** | Full keyboard shortcut table |
