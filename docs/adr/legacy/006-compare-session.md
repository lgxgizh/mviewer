# ADR 006: CompareSession as engine-owned state

## Status

Accepted

## Context

Multi-image comparison carries shared sync transform, per-cell transforms,
blink index, and layout. This state must not live in the UI widget, or it
becomes impossible to test or script.

## Decision

`domain::CompareSession` is a plain value struct holding all comparison
state. `CompareEngine` owns the live state and exposes it read-only via
`CompareEngine::session()`; the UI (`compareworkspace`) only reads and drives
the engine through its public methods.

## Consequences

- Comparison logic is independent of `QWidget` and headless-testable.
- UI cannot corrupt engine state directly.
