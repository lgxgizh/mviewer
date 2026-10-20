// Compare display materialization state machine (pure value component).
// Table-driven transitions plus the two #202 regressions (cheap -> skip ->
// cheap livelock, latest-wins lost request) and a seeded interleaving soak
// that asserts every pane leaves "Loading". The fixture mirrors the user
// report: two 4096x3072 JPEG panes with warm thumbs, soft pair switches and
// per-pane scaleChanged refreshes at high zoom.
#include "compareworkspace_display_state.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <random>
#include <vector>

namespace
{

using mviewer::ui::CompareDisplayBatchPlan;
using mviewer::ui::CompareDisplayStateMachine;
using mviewer::ui::ComparePaneDelivery;
using mviewer::ui::ComparePanePhase;

int g_fails = 0;

void expect(const char *name, bool ok)
{
    std::printf("%s: %s\n", ok ? "PASS" : "FAIL", name);
    if (!ok)
        ++g_fails;
}

bool contains(const std::vector<int> &v, int x)
{
    return std::find(v.begin(), v.end(), x) != v.end();
}

const char *phaseName(ComparePanePhase p)
{
    switch (p)
    {
    case ComparePanePhase::Empty:
        return "Empty";
    case ComparePanePhase::WarmStandIn:
        return "WarmStandIn";
    case ComparePanePhase::CheapPending:
        return "CheapPending";
    case ComparePanePhase::SharpPending:
        return "SharpPending";
    case ComparePanePhase::Ready:
        return "Ready";
    case ComparePanePhase::Failed:
        return "Failed";
    }
    return "?";
}

// Starting points reachable through the public API only.
enum class Start
{
    FreshEmpty,  // rebuildCells: blank pane, no badge
    PairEmpty,   // pair switch without a warm thumb
    WarmStandIn, // pair switch over a warm 4096x3072 thumb
    CheapShown,  // cheap 640x480 LOD accepted, sharp still owed
    Ready,       // sharp raster on screen
    Failed,      // terminal error
};

uint64_t g_gen = 0;

void submitAndDeliver(CompareDisplayStateMachine &m, ComparePaneDelivery d, bool raster = true)
{
    const CompareDisplayBatchPlan plan = m.plan({0}, {static_cast<uint8_t>(raster ? 1 : 0)}, false);
    m.commit(plan, ++g_gen);
    m.deliver(0, g_gen, d);
    m.land(g_gen);
}

CompareDisplayStateMachine make(Start s)
{
    CompareDisplayStateMachine m;
    m.reset(1);
    switch (s)
    {
    case Start::FreshEmpty:
        break;
    case Start::PairEmpty:
        m.beginPane(0, false);
        break;
    case Start::WarmStandIn:
        m.beginPane(0, true);
        break;
    case Start::CheapShown:
        m.beginPane(0, true);
        submitAndDeliver(m, ComparePaneDelivery::Accepted);
        break;
    case Start::Ready:
        m.beginPane(0, true);
        submitAndDeliver(m, ComparePaneDelivery::Accepted);
        submitAndDeliver(m, ComparePaneDelivery::Accepted);
        break;
    case Start::Failed:
        m.beginPane(0, true);
        submitAndDeliver(m, ComparePaneDelivery::Failed);
        break;
    }
    return m;
}

struct Row
{
    const char *name;
    Start start;
    bool raster;           // pane shows a raster when the batch is planned
    bool dirty;            // caller asks for the pane (zoom/pan/adjust)
    bool expectPlanned;    // pane rides in the batch
    bool expectCheap;      // ... as the cheap provisional LOD
    ComparePaneDelivery d; // what the batch delivers for the pane
    ComparePanePhase to;   // phase after delivery + landing
    bool badge;            // Loading badge after landing
    bool awaiting;         // a follow-up batch must ask for the pane
};

void runTable()
{
    using D = ComparePaneDelivery;
    using P = ComparePanePhase;
    const Row rows[] = {
        {"fresh blank pane -> cheap -> sharp owed", Start::FreshEmpty, false, false, true, true,
         D::Accepted, P::SharpPending, false, true},
        {"pair switch, no thumb -> cheap keeps badge", Start::PairEmpty, false, false, true, true,
         D::Accepted, P::SharpPending, true, true},
        {"warm stand-in -> cheap accepted keeps badge", Start::WarmStandIn, true, false, true, true,
         D::Accepted, P::SharpPending, true, true},
        {"warm stand-in -> cheap skipped keeps badge, owes sharp", Start::WarmStandIn, true, false,
         true, true, D::Skipped, P::SharpPending, true, true},
        {"warm stand-in -> cheap failed is terminal", Start::WarmStandIn, true, false, true, true,
         D::Failed, P::Failed, false, false},
        {"cheap shown -> sharp accepted is Ready", Start::CheapShown, true, false, true, false,
         D::Accepted, P::Ready, false, false},
        {"cheap shown -> sharp failed", Start::CheapShown, true, false, true, false, D::Failed,
         P::Failed, false, false},
        {"ready -> zoom (dirty) -> sharp accepted", Start::Ready, true, true, true, false,
         D::Accepted, P::Ready, false, false},
        {"ready -> late sharp skipped stays Ready", Start::Ready, true, true, true, false,
         D::Skipped, P::Ready, false, false},
        {"failed -> zoom (dirty) retries sharp", Start::Failed, true, true, true, false,
         D::Accepted, P::Ready, false, false},
    };
    for (const Row &row : rows)
    {
        CompareDisplayStateMachine m = make(row.start);
        const std::vector<int> dirty = row.dirty ? std::vector<int>{0} : std::vector<int>{};
        const CompareDisplayBatchPlan plan =
            m.plan(dirty, {static_cast<uint8_t>(row.raster ? 1 : 0)}, false);
        const bool planned = contains(plan.panes, 0);
        bool ok = planned == row.expectPlanned && plan.wantsCheap(0) == row.expectCheap;
        if (planned)
        {
            m.commit(plan, ++g_gen);
            m.deliver(0, g_gen, row.d);
            m.land(g_gen);
        }
        const auto &st = m.pane(0);
        ok = ok && st.phase == row.to && st.badge == row.badge &&
             contains(m.awaitingRequest(), 0) == row.awaiting;
        if (!ok)
            std::printf("  got planned=%d cheap=%d phase=%s badge=%d\n", planned ? 1 : 0,
                        plan.wantsCheap(0) ? 1 : 0, phaseName(st.phase), st.badge ? 1 : 0);
        expect(row.name, ok);
    }
    // Settled panes are not re-requested without a reason.
    for (const Start s : {Start::Ready, Start::Failed})
    {
        CompareDisplayStateMachine m = make(s);
        expect(s == Start::Ready ? "ready pane without dirty is not planned"
                                 : "failed pane without dirty is not planned",
               m.plan({}, {1}, false).empty());
    }
}

// #202 livelock: two 4096x3072 panes over warm thumbs. The cheap 640x480 LOD
// is blurrier than the thumb shown 1:1, so both cheap cells are skipped. The
// follow-up must ask for sharp, never cheap again.
void livelock()
{
    CompareDisplayStateMachine m;
    m.reset(2);
    m.beginPane(0, true);
    m.beginPane(1, true);
    const std::vector<uint8_t> raster{1, 1};
    auto plan = m.plan({0, 1}, raster, false);
    expect("livelock: first batch is cheap for both", plan.wantsCheap(0) && plan.wantsCheap(1));
    m.commit(plan, 10);
    m.deliver(0, 10, ComparePaneDelivery::Skipped);
    m.deliver(1, 10, ComparePaneDelivery::Skipped);
    m.land(10);
    expect("livelock: both still badged and awaiting sharp",
           m.badge(0) && m.badge(1) && m.awaitingRequest().size() == 2);
    plan = m.plan({}, raster, false);
    expect("livelock: follow-up asks sharp for both",
           plan.panes.size() == 2 && !plan.wantsCheap(0) && !plan.wantsCheap(1));
    m.commit(plan, 11);
    m.deliver(0, 11, ComparePaneDelivery::Accepted);
    m.deliver(1, 11, ComparePaneDelivery::Accepted);
    m.land(11);
    expect("livelock: both Ready without badge", m.pane(0).phase == ComparePanePhase::Ready &&
                                                     m.pane(1).phase == ComparePanePhase::Ready &&
                                                     !m.badge(0) && !m.badge(1) &&
                                                     m.awaitingRequest().empty());
}

// #202 lost request: an all-pane batch is in flight, then a single-pane
// request (fast preview / per-pane scaleChanged) cancels it latest-wins.
void lostRequest()
{
    CompareDisplayStateMachine m;
    m.reset(2);
    m.beginPane(0, true);
    m.beginPane(1, true);
    const std::vector<uint8_t> raster{1, 1};
    m.commit(m.plan({0, 1}, raster, false), 20);
    auto plan = m.plan({0}, raster, false);
    expect("lost request: pane 1 rides along with a pane-0 request",
           plan.panes.size() == 2 && contains(plan.panes, 1) && plan.wantsCheap(1));
    m.commit(plan, 21);
    m.deliver(1, 20, ComparePaneDelivery::Accepted); // cancelled batch: stale
    expect("lost request: delivery of the cancelled batch is ignored",
           m.badge(1) && m.pane(1).phase == ComparePanePhase::CheapPending);
    expect("lost request: landing the cancelled batch is a no-op", m.land(20).empty());
    m.deliver(0, 21, ComparePaneDelivery::Accepted);
    m.deliver(1, 21, ComparePaneDelivery::Accepted);
    m.land(21);
    expect("lost request: both owe the sharp pass", m.awaitingRequest().size() == 2);

    // Lost upgrade: pane 0 got its cheap pass; before the 0 ms follow-up runs
    // pane 1's scaleChanged schedules a batch. Pane 0's sharp pass rides along.
    plan = m.plan({1}, raster, false);
    expect("lost upgrade: cheap-shown pane 0 rides along as sharp",
           contains(plan.panes, 0) && !plan.wantsCheap(0) && !plan.wantsCheap(1));
    m.commit(plan, 22);
    expect("lost upgrade: nothing awaits while the batch is live",
           m.awaitingRequest().empty() && !m.needsRequest(0));
    m.abandon(); // hide drops the batch without landing
    expect("abandoned batch: panes await a request again", m.awaitingRequest().size() == 2);
}

void loadInFlight()
{
    CompareDisplayStateMachine m;
    m.reset(4);
    m.beginPane(0, true);  // warm thumb of the new pair
    m.beginPane(1, false); // blank
    m.beginPane(2, true);
    m.adopt({1, 0, 1, 1}, {1, 1, 1, 0}); // pane 3: settled, not part of the reload
    const auto plan = m.plan({2, 3}, {1, 0, 1, 1}, true);
    expect("load in flight: badged stand-in left alone", !contains(plan.panes, 0));
    expect("load in flight: blank pane still requested", contains(plan.panes, 1));
    expect("load in flight: dirty badged stand-in is not repainted from the previous frames",
           !contains(plan.panes, 2));
    expect("load in flight: dirty settled pane still requested", contains(plan.panes, 3));
    const auto after = m.plan({}, {1, 0, 1, 1}, false);
    expect("load landed: every pending pane requested", after.panes.size() == 3);
}

void reentry()
{
    CompareDisplayStateMachine m;
    m.reset(2);
    m.beginPane(0, true);
    m.beginPane(1, true);
    m.commit(m.plan({}, {1, 1}, false), 30);
    // Pair switch while the batch is in flight: its rasters are for the
    // previous source.
    m.beginPane(0, true);
    expect("pair switch: pane no longer expects the in-flight batch",
           !m.expects(0, 30) && m.expects(1, 30));
    m.deliver(0, 30, ComparePaneDelivery::Accepted);
    expect("pair switch: stale delivery keeps the badge", m.badge(0));
    m.deliver(1, 30, ComparePaneDelivery::Accepted);
    expect("pair switch: landing counts no idle for the re-entered pane",
           m.land(30).empty() && m.pane(0).idleLandings == 0);
    expect("pair switch: re-entered pane asks cheap again",
           m.plan({}, {1, 1}, false).wantsCheap(0));

    // Warm seed over a Ready pane starts a fresh epoch; over a live request
    // it keeps that request (same source).
    CompareDisplayStateMachine r = make(Start::Ready);
    r.noteStandIn(0);
    expect("warm seed over Ready: badged stand-in, cheap first",
           r.pane(0).phase == ComparePanePhase::WarmStandIn && r.badge(0) &&
               r.plan({}, {1}, false).wantsCheap(0));
    CompareDisplayStateMachine live;
    live.reset(1);
    live.beginPane(0, false);
    live.commit(live.plan({}, {0}, false), 31);
    live.noteStandIn(0);
    expect("warm seed over a live request keeps it", live.expects(0, 31));
}

void idleBackstop()
{
    CompareDisplayStateMachine m;
    m.reset(1);
    m.beginPane(0, true);
    std::vector<int> exhausted;
    for (int i = 0; i < CompareDisplayStateMachine::kMaxIdleLandings; ++i)
    {
        expect("backstop: not fired early", exhausted.empty());
        const uint64_t gen = 40 + static_cast<uint64_t>(i);
        m.commit(m.plan({}, {1}, false), gen);
        exhausted = m.land(gen); // no cell delivered for the pane
    }
    expect("backstop: fires after kMaxIdleLandings idle landings",
           exhausted.size() == 1 && exhausted[0] == 0);
    expect("backstop: pane failed and badge cleared", m.pane(0).phase == ComparePanePhase::Failed &&
                                                          !m.badge(0) &&
                                                          m.awaitingRequest().empty());
    CompareDisplayStateMachine p = make(Start::WarmStandIn);
    p.commit(p.plan({}, {1}, false), 50);
    p.deliver(0, 50, ComparePaneDelivery::Skipped);
    p.land(50);
    p.commit(p.plan({}, {1}, false), 51);
    p.deliver(0, 51, ComparePaneDelivery::Accepted);
    expect("backstop: progress resets the idle count",
           p.land(51).empty() && p.pane(0).idleLandings == 0);
}

void previewAndSwap()
{
    CompareDisplayStateMachine m;
    m.reset(2);
    m.beginPane(0, true);
    m.beginPane(1, true);
    m.commit(m.plan({}, {1, 1}, false), 60);
    m.deliver(0, 60, ComparePaneDelivery::Accepted); // cheap shown, still badged
    m.deliver(1, 60, ComparePaneDelivery::Accepted);
    m.land(60);
    m.commit(m.plan({}, {1, 1}, false), 61);
    m.deliver(0, 61, ComparePaneDelivery::Accepted); // sharp: Ready, badge off
    m.land(61);                                      // pane 1 got no cell
    expect("preview follow-up: badgeOnly keeps only badged panes",
           m.awaitingRequest(true).size() == 1 && m.awaitingRequest(true)[0] == 1 &&
               m.awaitingRequest(false).size() == 1);
    m.swapPanes(0, 1);
    expect("swap: state follows the raster", m.badge(0) && !m.badge(1));
    CompareDisplayStateMachine a;
    a.adopt({1, 1, 0}, {0, 1, 0});
    expect("adopt: raster->Ready, badge->stand-in, blank->Empty",
           a.pane(0).phase == ComparePanePhase::Ready &&
               a.pane(1).phase == ComparePanePhase::WarmStandIn &&
               a.pane(2).phase == ComparePanePhase::Empty);
    expect("out of range pane reads as settled", !a.isPending(7) && !a.badge(-1));
}

// The intermittent compare_stuck_loading_stress_tests failure (iteration 6,
// variant 2): two 4096x3072 panes at ~5.4x zoom, a soft pair reload with a
// warm thumb on pane 0 only, wheel events on both panes while the reload is
// in flight. Pane 0 was left on the cheap 2048x1536 full frame with no
// Loading badge: the cheap delivery cleared the badge, a wheel preview batch
// carrying pane 0 then settled it Ready (skipped as blurrier, no badge), and
// the pane-1 debounce never asked for pane 0 again.
void previewDuringReload()
{
    CompareDisplayStateMachine m;
    m.reset(2);
    m.beginPane(0, true);  // warm 512x384 thumb
    m.beginPane(1, false); // cold
    // Wheel on pane 0 while the reload is in flight (fast preview, dirty).
    auto plan = m.plan({0}, {1, 0}, true);
    expect("reload: wheel preview does not repaint pane 0 from the previous pair",
           !contains(plan.panes, 0) && contains(plan.panes, 1));
    m.commit(plan, 70);
    m.deliver(1, 70, ComparePaneDelivery::Accepted, true);
    expect("reload: preview landing never fails or settles a pane",
           m.land(70, true).empty() && m.pane(1).phase == ComparePanePhase::SharpPending);
    // finishLoad: every pane. Pane 0 cheap (2048x1536 full frame).
    plan = m.plan({0, 1}, {1, 1}, false);
    expect("reload landed: pane 0 cheap, pane 1 sharp", plan.wantsCheap(0) && !plan.wantsCheap(1));
    m.commit(plan, 71);
    m.deliver(0, 71, ComparePaneDelivery::Accepted);
    m.deliver(1, 71, ComparePaneDelivery::Accepted);
    m.land(71);
    expect("cheap 2048x1536 frame keeps Loading and owes the sharp crop",
           m.badge(0) && m.pane(0).phase == ComparePanePhase::SharpPending &&
               contains(m.awaitingRequest(true), 0));
    // Before the follow-up runs, a wheel on pane 1 submits a preview batch
    // (latest-wins); pending pane 0 rides along as a preview cell.
    plan = m.plan({1}, {1, 1}, false);
    expect("wheel preview on pane 1 carries pane 0 sharp",
           contains(plan.panes, 0) && !plan.wantsCheap(0));
    m.commit(plan, 72);
    m.deliver(0, 72, ComparePaneDelivery::Skipped, true);
    m.deliver(1, 72, ComparePaneDelivery::Accepted, true);
    m.land(72, true);
    expect("preview skipped on pane 0: still SharpPending under Loading",
           m.badge(0) && m.pane(0).phase == ComparePanePhase::SharpPending);
    expect("preview accepted on pane 1: not Ready, sharp still owed",
           m.pane(1).phase == ComparePanePhase::SharpPending && !m.badge(1));
    expect("preview follow-up asks for pane 0 even while the debounce is armed",
           contains(m.awaitingRequest(true), 0));
    // A wheel burst: many preview batches, each skipped for pane 0.
    for (uint64_t g = 73; g < 73 + 2 * CompareDisplayStateMachine::kMaxIdleLandings; ++g)
    {
        m.commit(m.plan({1}, {1, 1}, false), g);
        m.deliver(0, g, ComparePaneDelivery::Skipped, true);
        m.deliver(1, g, ComparePaneDelivery::Accepted, true);
        expect("wheel burst: previews never trip the idle backstop", m.land(g, true).empty());
    }
    expect("wheel burst: pane 0 still owed with its badge",
           m.badge(0) && m.pane(0).phase == ComparePanePhase::SharpPending &&
               m.awaitingRequest().size() == 2);
    // Debounce / follow-up: a sharp, non-preview batch settles both.
    plan = m.plan({}, {1, 1}, false);
    expect("follow-up carries both panes sharp",
           plan.panes.size() == 2 && !plan.wantsCheap(0) && !plan.wantsCheap(1));
    m.commit(plan, 90);
    m.deliver(0, 90, ComparePaneDelivery::Accepted);
    m.deliver(1, 90, ComparePaneDelivery::Accepted);
    m.land(90);
    expect("sharp crop lands: both Ready, no Loading badge",
           m.pane(0).phase == ComparePanePhase::Ready &&
               m.pane(1).phase == ComparePanePhase::Ready && !m.badge(0) && !m.badge(1) &&
               m.awaitingRequest().empty());
}

// Seeded interleaving soak: pair switches, per-pane zoom refreshes and
// latest-wins cancellations in random order. After the events stop, the
// follow-up loop (what CompareWorkspace runs on every landing) must settle
// every pane within a bounded number of batches with no Loading badge.
void soak()
{
    std::mt19937 rng(202);
    int stuck = 0;
    for (int round = 0; round < 500; ++round)
    {
        CompareDisplayStateMachine m;
        m.reset(2);
        std::vector<uint8_t> raster{0, 0};
        uint64_t gen = 0;
        uint64_t inFlight = 0;
        CompareDisplayBatchPlan live;
        const auto deliverLive = [&]()
        {
            for (size_t k = 0; k < live.panes.size(); ++k)
            {
                const int pane = live.panes[k];
                // A cheap 640 LOD over a sharper stand-in is skipped half the time.
                const bool skip = live.cheap[k] != 0 && raster[static_cast<size_t>(pane)] != 0 &&
                                  (rng() % 2) == 0;
                m.deliver(pane, inFlight,
                          skip ? ComparePaneDelivery::Skipped : ComparePaneDelivery::Accepted);
                raster[static_cast<size_t>(pane)] = 1;
            }
            m.land(inFlight);
            inFlight = 0;
        };
        const int events = 3 + static_cast<int>(rng() % 12);
        for (int e = 0; e < events; ++e)
        {
            switch (rng() % 4)
            {
            case 0: // soft pair switch of one pane (warm thumb or blank)
            {
                const int pane = static_cast<int>(rng() % 2);
                const bool thumb = (rng() % 2) == 0;
                if (thumb)
                    raster[static_cast<size_t>(pane)] = 1;
                m.beginPane(pane, raster[static_cast<size_t>(pane)] != 0);
                break;
            }
            case 1: // per-pane scaleChanged LOD refresh (cancels the live batch)
            case 2:
            {
                const int pane = static_cast<int>(rng() % 2);
                live = m.plan({pane}, raster, false);
                if (!live.empty())
                {
                    inFlight = ++gen;
                    m.commit(live, inFlight);
                }
                break;
            }
            default: // the live batch lands
                if (inFlight != 0)
                    deliverLive();
                break;
            }
        }
        if (inFlight != 0)
            deliverLive();
        int batches = 0;
        while (!m.awaitingRequest().empty() && batches < 8)
        {
            live = m.plan({}, raster, false);
            inFlight = ++gen;
            m.commit(live, inFlight);
            deliverLive();
            ++batches;
        }
        for (int p = 0; p < 2; ++p)
        {
            if (m.badge(p) || m.pane(p).phase != ComparePanePhase::Ready)
                ++stuck;
        }
    }
    std::printf("soak: %d stuck pane(s) in 500 rounds\n", stuck);
    expect("soak: every pane settles Ready without a Loading badge", stuck == 0);
}

} // namespace

int main()
{
    runTable();
    livelock();
    lostRequest();
    loadInFlight();
    reentry();
    idleBackstop();
    previewAndSwap();
    previewDuringReload();
    soak();
    expect("debounce merges two panes into all panes",
           mviewer::ui::mergeLodRefreshPane(true, 0, 1) == -1);
    std::printf("%d failure(s)\n", g_fails);
    return g_fails == 0 ? 0 : 1;
}
