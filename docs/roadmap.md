# MViewer Development Roadmap

## Guiding Principles

1. **Performance before features** — Every phase must meet its performance targets before moving on.
2. **Vertical slices** — Each phase delivers a working, testable product.
3. **No premature optimization** — Build correctly first, then benchmark and optimize.
4. **Maintainability always** — Code quality is non-negotiable at every stage.

---

## Phase 0: Foundation (Weeks 1-3)

**Goal:** Project scaffolding and build infrastructure.

### Deliverables
- CMake build system with Ninja generator support
- Module directory structure
- CI pipeline (GitHub Actions: Windows MSVC, Linux Clang/GCC)
- clang-format and clang-tidy configuration
- Unit test framework integration (Catch2 or Google Test)
- Basic application skeleton (window, event loop, exit)

### Exit Criteria
- Project builds on Windows (MSVC) and Linux (Clang)
- CI passes with zero warnings
- Empty application window opens and closes cleanly

---

## Phase 1: Core Image Loading (Weeks 4-7)

**Goal:** Open and display a single image.

### Deliverables
- Filesystem module (file enumeration, path handling)
- Decoder interface (`IDecoder`)
- JPEG decoder (libjpeg-turbo)
- PNG decoder (libpng or WIC)
- BMP decoder (custom, simple format)
- Basic renderer (D3D11 on Windows)
- Image canvas widget (display, window fit)
- "Open File" dialog

### Exit Criteria
- Open and display JPEG, PNG, BMP images
- Image fits to window correctly
- No memory leaks (verified by AddressSanitizer/Valgrind)
- Startup time < 300ms (cold), < 100ms (warm)

---

## Phase 2: Navigation & Thumbnails (Weeks 8-12)

**Goal:** Browse folders efficiently.

### Deliverables
- Async directory scanning
- Folder open dialog
- Previous/Next navigation
- Thumbnail sidebar panel
- Thumbnail generation pipeline
- Persistent thumbnail cache (disk-backed)
- Preloading (next image in direction)

### Exit Criteria
- Open folder with 10,000 images — thumbnails appear immediately
- Navigation latency < 16ms for preloaded images
- Thumbnail cache persists across sessions
- UI remains responsive during folder scan

---

## Phase 3: GPU Acceleration & Interaction (Weeks 13-17)

**Goal:** Smooth zoom, pan, and fullscreen.

### Deliverables
- GPU-accelerated zoom (mouse wheel, keyboard)
- Smooth pan (click-drag)
- Fit to window / Original size / Stretch
- Fullscreen mode (F11)
- Rotate (90° clockwise/counter-clockwise)
- GPU texture cache with LRU eviction
- Renderer fallback chain (D3D11 → D2D)

### Exit Criteria
- Zoom/pan at 60fps for 50MP images
- Fullscreen toggle < 100ms
- GPU memory usage stays within configured budget
- Smooth interaction on integrated graphics

---

## Phase 4: Format Expansion (Weeks 18-22)

**Goal:** Support all required formats.

### Deliverables
- GIF decoder (with animation playback)
- TIFF decoder (libtiff)
- WebP decoder (libwebp)
- AVIF decoder (libdav1d + libaom)
- HEIF/HEIC decoder (libheif)
- JPEG XL decoder (libjxl)
- Format detection by magic bytes
- Decoder factory with priority chain

### Exit Criteria
- All required formats open and display correctly
- GIF animations play smoothly at correct speed
- Format detection works without file extension
- Decode performance benchmarks pass

---

## Phase 5: Metadata & Slideshow (Weeks 23-26)

**Goal:** Information display and passive viewing.

### Deliverables
- EXIF parser
- IPTC parser
- XMP parser
- Metadata panel (collapsible sidebar)
- Metadata-driven auto-rotation
- Slideshow mode (configurable interval)
- Ken Burns effect (optional, future)

### Exit Criteria
- Metadata displays without blocking image render
- Slideshow transitions smoothly
- Panel updates on navigation
- All metadata parsing is async

---

## Phase 6: Performance Pass (Weeks 27-30)

**Goal:** Optimize, benchmark, and stabilize.

### Deliverables
- Comprehensive benchmark suite
- Profiling infrastructure (Tracy integration)
- Cache tuning (size policies, hit ratio optimization)
- Preloading algorithm refinement
- Memory usage optimization
- Startup time optimization pass
- Performance regression CI gates

### Exit Criteria
- All performance targets met (see `performance.md`)
- Benchmark suite runs in CI
- No regressions vs. Phase 5
- Memory usage within defined budgets

---

## Phase 7: Linux Port (Weeks 31-36)

**Goal:** Full Linux support.

### Deliverables
- OpenGL renderer implementation
- Linux filesystem watcher (inotify)
- Linux thumbnail cache path (XDG_CACHE_HOME)
- GTK/Qt theme integration
- AppImage or Flatpak packaging
- Linux CI pipeline

### Exit Criteria
- Feature parity with Windows version
- Performance targets met on Linux
- Native look on GNOME and KDE
- Package installs and runs on Ubuntu 24.04, Fedora 40

---

## Phase 8: Post-MVP Features (Weeks 37+)

**Goal:** Community-requested enhancements.

### Planned Features
- Dual image comparison (side-by-side)
- Multiple image comparison (grid)
- Histogram overlay
- Batch rename
- Batch format conversion
- Duplicate image detection
- Similar image search (perceptual hash)
- Bookmarks / Favorites
- Tag management

### Constraints
- Each feature must pass performance regression tests
- Features implemented as plugins where possible
- Core browsing performance must not degrade

---

## Release Schedule

| Version | Target | Key Milestone |
|---------|--------|---------------|
| 0.1.0 | End of Phase 5 | MVP — all required features |
| 0.1.1 | End of Phase 6 | Performance-optimized MVP |
| 0.2.0 | End of Phase 7 | Linux release |
| 0.3.0+ | Phase 8 | Post-MVP features (iterative) |

---

## Definition of Done

For any release:
- [ ] All unit tests pass
- [ ] All integration tests pass
- [ ] All benchmarks meet targets
- [ ] No memory leaks (ASan clean)
- [ ] No data races (TSan clean)
- [ ] Code reviewed and approved
- [ ] Documentation updated
- [ ] changelog.md updated
