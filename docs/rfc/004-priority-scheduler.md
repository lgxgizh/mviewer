# RFC-004: Priority Scheduler

## Status
Accepted

## Priority
P0

## Goal
Evolve TaskScheduler into a priority scheduler with 5 separate task queues.

## Current State
TaskScheduler has 4 queues (Decode/Thumbnail/Analysis/IO) but no priority across them. All tasks compete equally.

## Target Architecture
```
TaskScheduler (singleton)
    ├── IO Queue (highest)
    ├── Decode Queue
    ├── Thumbnail Queue
    ├── Analysis Queue
    └── Background Queue (lowest)

Every task owns:
    priority (enum)
    cancel token (atomic<bool>)
    progress (atomic<int>, 0-100)
    dependency (vector<TaskId>, future)
```

## Requirements

### Input
- Task ID (for cancellation/callback)
- Priority (enum: UI/Decode/Thumbnail/Analysis/Background)
- Work function (lambda)
- Done callback (lambda, on UI thread)

### Output
- Success/failure
- Progress updates

### Priority Rules
1. UI (highest): navigation, zoom, user invoke
2. Decode: current-image decode
3. Thumbnail: visible thumbnails
4. Analysis: histogram, diff, PSNR
5. Background (lowest): preload, prefetch

### Cancel Token
- Tasks check cancel token periodically
- Cancelled tasks don't invoke done callback
- Cancel propagates to dependent tasks

### Thread Safety
- Submit from any thread
- Execute on pool thread
- Callback dispatched to UI thread via event loop

### Performance
- UI tasks: immediate execution (preempt if needed)
- Background tasks: may be throttled
- Never block UI thread

### Error
- Exception in work → err callback
- Timeout → cancel + error

## Consequences
- Smooth 60fps during background scans
- UI never waits for background work
- Higher single-image latency under load (acceptable)

## Related
- ADR-007 (Why priority scheduler)
