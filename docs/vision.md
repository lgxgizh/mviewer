# MViewer Project Specification

## Project Overview

**MViewer** is an open-source, high-performance desktop image viewer designed for users who browse large image collections.

The project is inspired by FastStone Image Viewer, IrfanView, and ImageGlass, but is built with a modern C++ architecture, excellent maintainability, and outstanding performance.

The primary goal is to provide an image viewing experience that feels **instant**, **smooth**, and **responsive**, even when browsing folders containing tens of thousands of images.

This project is **not** an image editor. Image editing features are intentionally minimal.

---

# Project Goals

The project prioritizes:

- Extremely fast startup
- Instant image switching
- Smooth zooming and panning
- Fast folder scanning
- Efficient thumbnail generation
- Excellent memory efficiency
- Native desktop experience
- Clean modular architecture
- Long-term maintainability

Performance always has higher priority than adding more features.

---

# Target Platforms

## Phase 1

- Windows 11 (Primary Development Platform)

## Phase 2

- Linux

The architecture should remain portable whenever practical, but Windows performance is the highest priority.

macOS is currently out of scope.

---

# Technology Stack

## Language

- C++20

Do **not** introduce:

- Rust
- C#
- Java
- Flutter
- Electron
- Web technologies

---

## GUI

- Qt 6 Widgets

Requirements:

- Native desktop look and feel
- Lightweight UI
- Keyboard-first workflow
- Avoid QML / Qt Quick unless there is a compelling reason

---

## Rendering

Windows:

- Direct3D 11
- Direct2D

Linux:

- OpenGL

Rendering should be abstracted behind a renderer interface so additional backends can be added later.

---

## Image Libraries

Preferred libraries:

- libjpeg-turbo
- libvips
- Windows Imaging Component (WIC)

Keep dependencies minimal.

Only introduce new libraries when they provide significant value.

---

## Build System

- CMake
- Ninja

Compilers:

Windows

- MSVC

Linux

- Clang
- GCC

---

# Supported Image Formats

Required:

- JPEG
- PNG
- BMP
- GIF (including animation playback)
- TIFF
- WebP
- AVIF
- HEIF / HEIC
- JPEG XL

Optional (Future):

- SVG
- ICO

RAW image formats are intentionally **out of scope**.

---

# Project Architecture

The project should use a modular architecture.

Suggested layout:

mviewer/

    app/

    ui/

    renderer/

    image/

        decoder/

        cache/

        thumbnail/

        metadata/

    filesystem/

    scheduler/

    benchmark/

    tests/

Each module should have clear responsibilities.

Avoid tightly coupled code.

Avoid giant classes.

---

# Performance Requirements

Performance is the most important design goal.

## Startup

Cold start:

- <300 ms

Warm start:

- <100 ms

---

## Folder Loading

Opening a folder containing 10,000 images should begin displaying thumbnails immediately.

Directory scanning must be asynchronous.

The UI should remain responsive during scanning.

---

## Image Switching

Switching to the next or previous image should feel instantaneous.

Whenever possible, neighboring images should already be decoded before the user requests them.

Target:

- Perceived latency <16 ms

---

## Zoom & Pan

Zooming and dragging should remain smooth for large images.

GPU acceleration should be used whenever beneficial.

Avoid unnecessary memory copies.

---

## Thumbnail Cache

Requirements:

- Persistent cache
- Background generation
- Incremental updates
- Automatic invalidation when files change

---

## Memory Cache

Provide multiple cache layers:

- Thumbnail cache
- Decoded image cache
- GPU texture cache

Use LRU eviction.

Memory usage should remain predictable.

---

# Metadata

Support reading:

- EXIF
- IPTC
- XMP

Metadata loading must be asynchronous.

Displaying metadata must never block image rendering.

---

# Threading Model

Use a reusable thread pool.

Separate workers for:

- Directory scanning
- Image decoding
- Thumbnail generation
- Metadata parsing
- Image preloading

The UI thread must never perform expensive operations.

---

# UI Philosophy

The UI should be:

- Simple
- Native
- Responsive
- Minimal
- Keyboard friendly

Avoid unnecessary animations.

Keep the interface focused on image browsing.

---

# MVP Features

Version 0.1 should include:

- Open image
- Open folder
- Previous / Next image
- Zoom
- Pan
- Rotate
- Fit to window
- Original size
- Fullscreen
- Thumbnail sidebar
- Metadata panel
- Slideshow

---

# Future Features

After the MVP is stable:

- Dual image comparison
- Multiple image comparison
- Histogram
- Batch rename
- Batch format conversion
- Duplicate image detection
- Similar image search
- Bookmark/Favorites
- Tag management

These features should never compromise browsing performance.

---

# Code Style

Prefer:

- Modern C++20
- RAII
- Smart pointers
- STL containers
- Move semantics
- const correctness

Avoid:

- Global state
- Singleton abuse
- Macros
- Giant utility classes
- Premature optimization

Code should remain readable and maintainable.

---

# Testing

Provide:

- Unit tests
- Integration tests
- Performance benchmarks

Benchmark targets include:

- Startup time
- Folder loading speed
- Image decoding latency
- Thumbnail generation speed
- Cache hit ratio
- Memory usage
- Image switching latency

Performance regressions should be detectable through benchmarks.

---

# AI Assistant Expectations

When generating code:

- Prefer maintainability over cleverness.
- Keep dependencies minimal.
- Make architectural decisions explicit.
- Write modular and testable code.
- Avoid unnecessary abstractions.
- Avoid overengineering.
- Prefer measurable performance improvements.
- Explain important design decisions.
- Keep pull requests and commits small and focused.

Whenever multiple implementations are possible, choose the simplest solution that satisfies the performance requirements.

The long-term goal is to build the fastest and most maintainable open-source desktop image viewer, with code quality comparable to mature open-source C++ projects.
