#pragma once

#include "compareworkspace_display_planner.h"

#include <cstdint>
#include <vector>

namespace mviewer::ui
{

// Discrete display pyramid: coarse → fine (≈ 1/4, 1/2, 1× of the desired plan).
// Pure planning — no QWidget / decoder state.
std::vector<CompareDisplayPlan> planCompareDisplayPyramid(const CompareDisplayPlanningInput &input);

// Index of the coarsest pyramid level that still satisfies `desired`
// (or -1 if none). `have` entries may be invalid / empty slots.
int selectReadyPyramidLevel(const std::vector<CompareDisplayPlan> &have,
                            const CompareDisplayPlan &desired);

// Next finer level index after `current`, or -1 if already finest / empty.
int nextFinerPyramidLevel(const std::vector<CompareDisplayPlan> &pyramid, int current);

// Latest-wins delivery guard for display batches.
inline bool shouldAcceptDisplayDelivery(uint64_t deliveryGen, uint64_t currentGen,
                                        bool deliveryProvisional, bool alreadyHaveFull)
{
    if (deliveryGen != currentGen)
        return false;
    // A provisional must not overwrite a newer full already painted for this gen.
    if (deliveryProvisional && alreadyHaveFull)
        return false;
    return true;
}

// Prefer Decode pool for visible / blank / soft-loading climbs; Analysis otherwise.
inline bool preferDecodePriorityForDisplay(bool provisional, bool blankOrSoftLoading,
                                           bool visibleClimb)
{
    return provisional || blankOrSoftLoading || visibleClimb;
}

} // namespace mviewer::ui
