#include "compareworkspace_display_state.h"

#include <algorithm>
#include <utility>

namespace mviewer::ui
{

bool CompareDisplayBatchPlan::wantsCheap(int pane) const
{
    for (size_t i = 0; i < panes.size() && i < cheap.size(); ++i)
    {
        if (panes[i] == pane)
            return cheap[i] != 0;
    }
    return false;
}

bool isComparePanePending(ComparePanePhase phase)
{
    return phase != ComparePanePhase::Ready && phase != ComparePanePhase::Failed;
}

void CompareDisplayStateMachine::reset(int paneCount)
{
    m_panes.assign(static_cast<size_t>(std::max(0, paneCount)), ComparePaneDisplayState{});
    m_liveGen = 0;
}

void CompareDisplayStateMachine::adopt(const std::vector<uint8_t> &hasRaster,
                                       const std::vector<uint8_t> &badge)
{
    m_panes.assign(hasRaster.size(), ComparePaneDisplayState{});
    m_liveGen = 0;
    for (size_t i = 0; i < m_panes.size(); ++i)
    {
        ComparePaneDisplayState &state = m_panes[i];
        state.badge = i < badge.size() && badge[i] != 0;
        if (hasRaster[i] == 0)
            state.phase = ComparePanePhase::Empty;
        else
            state.phase = state.badge ? ComparePanePhase::WarmStandIn : ComparePanePhase::Ready;
    }
}

const ComparePaneDisplayState &CompareDisplayStateMachine::pane(int index) const
{
    static const ComparePaneDisplayState kNone{ComparePanePhase::Ready};
    if (index < 0 || index >= paneCount())
        return kNone;
    return m_panes[static_cast<size_t>(index)];
}

ComparePaneDisplayState *CompareDisplayStateMachine::at(int index)
{
    if (index < 0 || index >= paneCount())
        return nullptr;
    return &m_panes[static_cast<size_t>(index)];
}

void CompareDisplayStateMachine::beginPane(int index, bool hasStandIn)
{
    ComparePaneDisplayState *state = at(index);
    if (!state)
        return;
    *state = ComparePaneDisplayState{};
    state->phase = hasStandIn ? ComparePanePhase::WarmStandIn : ComparePanePhase::Empty;
    state->badge = true;
}

void CompareDisplayStateMachine::noteStandIn(int index)
{
    ComparePaneDisplayState *state = at(index);
    if (!state)
        return;
    state->badge = true;
    state->idleLandings = 0;
    if (!isComparePanePending(state->phase) || state->phase == ComparePanePhase::Empty)
    {
        // The warm bitmap replaced whatever was shown: start a fresh epoch
        // (cheap first, then sharp). A live request is for the same source,
        // so it is kept.
        state->phase = ComparePanePhase::WarmStandIn;
        state->cheapTried = false;
    }
}

void CompareDisplayStateMachine::clearBadges()
{
    for (ComparePaneDisplayState &state : m_panes)
        state.badge = false;
}

void CompareDisplayStateMachine::swapPanes(int a, int b)
{
    ComparePaneDisplayState *sa = at(a);
    ComparePaneDisplayState *sb = at(b);
    if (sa && sb && sa != sb)
        std::swap(*sa, *sb);
}

CompareDisplayBatchPlan CompareDisplayStateMachine::plan(const std::vector<int> &dirty,
                                                         const std::vector<uint8_t> &hasRaster,
                                                         bool loadInFlight) const
{
    CompareDisplayBatchPlan out;
    const int count = paneCount();
    const auto raster = [&hasRaster](int i)
    {
        return i >= 0 && i < static_cast<int>(hasRaster.size()) &&
               hasRaster[static_cast<size_t>(i)] != 0;
    };
    const auto add = [this, &out, &raster, count](int i)
    {
        if (i < 0 || i >= count)
            return;
        if (std::find(out.panes.cbegin(), out.panes.cend(), i) != out.panes.cend())
            return;
        const ComparePaneDisplayState &state = m_panes[static_cast<size_t>(i)];
        const bool cheapPhase = state.phase == ComparePanePhase::Empty ||
                                state.phase == ComparePanePhase::WarmStandIn ||
                                state.phase == ComparePanePhase::CheapPending;
        out.panes.push_back(i);
        out.cheap.push_back((!raster(i) || (cheapPhase && !state.cheapTried)) ? 1 : 0);
    };
    for (int i = 0; i < count; ++i)
    {
        const ComparePaneDisplayState &state = m_panes[static_cast<size_t>(i)];
        const bool pending = isComparePanePending(state.phase);
        const bool blank = !raster(i) && state.phase != ComparePanePhase::Failed;
        if (!pending && !blank)
            continue;
        // A soft pair reload in flight still has the previous frames in the
        // engine; repainting them would replace the new pair's stand-in.
        // finishLoad schedules every pane once the frames land.
        if (loadInFlight && state.badge && raster(i))
            continue;
        add(i);
    }
    for (int i : dirty)
        add(i);
    return out;
}

void CompareDisplayStateMachine::commit(const CompareDisplayBatchPlan &plan, uint64_t gen)
{
    m_liveGen = gen;
    for (size_t k = 0; k < plan.panes.size(); ++k)
    {
        ComparePaneDisplayState *state = at(plan.panes[k]);
        if (!state)
            continue;
        const bool cheap = k < plan.cheap.size() && plan.cheap[k] != 0;
        state->requestGen = gen;
        state->requestedCheap = cheap;
        state->phase = cheap ? ComparePanePhase::CheapPending : ComparePanePhase::SharpPending;
    }
}

bool CompareDisplayStateMachine::expects(int index, uint64_t gen) const
{
    if (gen == 0 || gen != m_liveGen || index < 0 || index >= paneCount())
        return false;
    return m_panes[static_cast<size_t>(index)].requestGen == gen;
}

bool CompareDisplayStateMachine::requestedCheap(int index, uint64_t gen) const
{
    return expects(index, gen) && m_panes[static_cast<size_t>(index)].requestedCheap;
}

void CompareDisplayStateMachine::deliver(int index, uint64_t gen, ComparePaneDelivery delivery)
{
    if (!expects(index, gen))
        return;
    ComparePaneDisplayState &state = m_panes[static_cast<size_t>(index)];
    const bool wasCheap = state.requestedCheap;
    state.requestGen = 0;
    state.requestedCheap = false;
    switch (delivery)
    {
    case ComparePaneDelivery::Failed:
        state.phase = ComparePanePhase::Failed;
        state.badge = false;
        state.idleLandings = 0;
        return;
    case ComparePaneDelivery::Accepted:
        state.badge = false;
        state.idleLandings = 0;
        if (wasCheap)
            state.cheapTried = true;
        state.phase = wasCheap ? ComparePanePhase::SharpPending : ComparePanePhase::Ready;
        return;
    case ComparePaneDelivery::Skipped:
        if (wasCheap || state.badge)
        {
            // The cheap raster lost to the stand-in: ask for sharp next. This
            // is the transition whose absence livelocked #202.
            state.cheapTried = state.cheapTried || wasCheap;
            state.phase = ComparePanePhase::SharpPending;
            ++state.idleLandings;
        }
        else
        {
            // A late preview lost to a sharper raster already on screen.
            state.phase = ComparePanePhase::Ready;
        }
        return;
    }
}

void CompareDisplayStateMachine::countIdle(ComparePaneDisplayState &state, int index,
                                           std::vector<int> &exhausted)
{
    if (!isComparePanePending(state.phase) || state.idleLandings < kMaxIdleLandings)
        return;
    state.phase = ComparePanePhase::Failed;
    state.badge = false;
    state.idleLandings = 0;
    exhausted.push_back(index);
}

std::vector<int> CompareDisplayStateMachine::land(uint64_t gen)
{
    std::vector<int> exhausted;
    if (gen == 0 || gen != m_liveGen)
        return exhausted;
    m_liveGen = 0;
    for (int i = 0; i < paneCount(); ++i)
    {
        ComparePaneDisplayState &state = m_panes[static_cast<size_t>(i)];
        if (state.requestGen == gen)
        {
            state.requestGen = 0;
            state.requestedCheap = false;
            ++state.idleLandings;
        }
        countIdle(state, i, exhausted);
    }
    return exhausted;
}

bool CompareDisplayStateMachine::needsRequest(int index) const
{
    if (index < 0 || index >= paneCount())
        return false;
    const ComparePaneDisplayState &state = m_panes[static_cast<size_t>(index)];
    if (!isComparePanePending(state.phase))
        return false;
    return state.requestGen == 0 || state.requestGen != m_liveGen;
}

std::vector<int> CompareDisplayStateMachine::awaitingRequest(bool badgeOnly) const
{
    std::vector<int> panes;
    for (int i = 0; i < paneCount(); ++i)
    {
        if (!needsRequest(i))
            continue;
        if (badgeOnly && !m_panes[static_cast<size_t>(i)].badge)
            continue;
        panes.push_back(i);
    }
    return panes;
}

int mergeLodRefreshPane(bool pending, int current, int incoming)
{
    if (!pending)
        return incoming;
    if (current < 0 || incoming < 0 || current != incoming)
        return -1;
    return current;
}

} // namespace mviewer::ui
