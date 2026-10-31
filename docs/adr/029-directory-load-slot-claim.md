# ADR 029 — Claimed result slots for the synchronous directory load

## Status

Accepted. Refines the M26/M27 notes on `ImageRepository::loadDirectory`.

## Context

`loadDirectory()` fans one decode task per file out to the scheduler and
waits for them under a defensive budget (`syncLoadBudget()`). When the budget
expired, the caller wrote "timed out" Results into the shared result array
while a decode that cannot be interrupted could still finish and assign its
own Result to the same element. Shared ownership (M27) kept the memory alive,
but the two writers raced on one `Result`, and the caller could copy a Result
while a late worker was still assigning it.

## Decision

- Each file has a slot with an atomic state: 0 = unclaimed, 1 = the winner is
  storing its Result, 2 = the Result is published.
- The worker (including its `catch (...)` path), the scheduler-rejected path
  and the timeout path all go through one compare-exchange claim. Only the
  winner writes the Result; a loser drops its value.
- After the wait, the caller claims every slot that is not yet published as a
  "timed out" failure, then copies the results. A slot still in state 1 is
  only a move in progress, so one shared 25 ms grace covers all slots; the
  call never waits for `load()` itself.
- The claim/collect helpers stay file-local in `ImageRepository_dir.cpp`.

## Consequences

- No data race between a late decode and the caller; results are copied only
  after their slot is published.
- A decode that finishes after the budget is discarded even if it succeeded,
  which matches the existing "timed out" contract.
- The call still returns within budget + grace, independent of directory size.
