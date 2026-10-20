#pragma once

#include <cstdint>
#include <vector>

namespace mviewer::ui
{

// Per-pane display materialization state for CompareWorkspace (pure value,
// no Qt). Replaces the implicit coupling of RawImageView::softLoading, the
// "undelivered panes" list, the 0 ms cheap->sharp upgrade kick and the stall
// watchdog that #198 / #202 layered on top of each other.
//
// One-way progression per pane, re-entered by a pair switch / warm seed
// (beginPane / noteStandIn) or a view change (dirty pane in plan()):
//
//   Empty | WarmStandIn  -> CheapPending -(accepted | skipped)-> SharpPending
//   SharpPending         -(sharp, non-preview accepted)-> Ready | Failed
//   SharpPending         -(preview accepted | skipped)-> SharpPending
//   Ready | Failed       -(dirty: zoom, pan, adjust, swap)-> SharpPending
//   any                  -(pair switch)-> Empty | WarmStandIn
//
// Invariants (each covered by test_compare_display_state.cpp):
// - A pane that is not Ready / Failed rides along in every new batch, so
//   latest-wins cancellation can never strand it (the #202 lost request).
// - The Loading badge set by a pair switch / warm seed stays until a sharp,
//   non-preview raster lands (or the pane fails): a cheap or preview raster
//   is never mistaken for the target LOD.
// - Cheap is requested at most once per pane epoch. After the cheap pass was
//   delivered *or* skipped as blurrier than the stand-in, the next request is
//   sharp (the #202 cheap -> skip -> cheap livelock cannot be expressed).
// - Every landed batch either moves a requested pane forward or counts an idle
//   landing; kMaxIdleLandings idle landings fail the pane (badge cleared). That
//   is the only remaining backstop and the UI logs it when it fires.
enum class ComparePanePhase : uint8_t
{
    Empty,        // no raster for the current pair yet
    WarmStandIn,  // warm thumb / previous raster under the Loading badge
    CheapPending, // cheap provisional LOD requested, not delivered yet
    SharpPending, // sharp viewport LOD wanted (requested or awaiting a batch)
    Ready,        // sharp raster for the current view is shown
    Failed,       // terminal error for this pair; the caption carries it
};

enum class ComparePaneDelivery : uint8_t
{
    Accepted, // the raster was painted
    Skipped,  // the raster was discarded (blurrier than what is shown)
    Failed,   // the batch reported an error for this pane
};

struct ComparePaneDisplayState
{
    ComparePanePhase phase = ComparePanePhase::Empty;
    bool badge = false;          // "Loading" corner badge (RawImageView::softLoading)
    bool cheapTried = false;     // cheap pass delivered or skipped this epoch
    bool requestedCheap = false; // the live request for this pane is the cheap LOD
    uint64_t requestGen = 0;     // batch generation carrying this pane, 0 = none
    int idleLandings = 0;        // landed batches without progress for this pane
};

struct CompareDisplayBatchPlan
{
    std::vector<int> panes;     // stable order, unique, in range
    std::vector<uint8_t> cheap; // parallel to panes: 1 = cheap provisional LOD

    bool empty() const
    {
        return panes.empty();
    }
    bool wantsCheap(int pane) const;
};

bool isComparePanePending(ComparePanePhase phase);

class CompareDisplayStateMachine
{
  public:
    static constexpr int kMaxIdleLandings = 3;

    // Fresh panes (rebuildCells): every pane Empty, no badge, no live batch.
    void reset(int paneCount);
    // Re-derive state from the views when the pane count changed behind the
    // machine's back. A pane with a raster is Ready, with the badge it is a
    // WarmStandIn, without a raster it is Empty.
    void adopt(const std::vector<uint8_t> &hasRaster, const std::vector<uint8_t> &badge);
    int paneCount() const
    {
        return static_cast<int>(m_panes.size());
    }
    const ComparePaneDisplayState &pane(int index) const;
    bool badge(int index) const
    {
        return pane(index).badge;
    }
    bool isPending(int index) const
    {
        return isComparePanePending(pane(index).phase);
    }
    // Pending and not carried by the live batch: the next batch must ask.
    bool needsRequest(int index) const;
    uint64_t liveGeneration() const
    {
        return m_liveGen;
    }

    // Pair switch: the pane shows a stand-in (or nothing) for a new source.
    // Drops any request for the previous source; the next request is cheap.
    void beginPane(int index, bool hasStandIn);
    // A warm seed was painted over the pane. Keeps a live request (it is for
    // the same source) but makes sure a real raster still follows.
    void noteStandIn(int index);
    // Badge off everywhere (before rebuildCells / terminal reload).
    void clearBadges();
    // Pane swap (A <-> B): the state follows the swapped raster.
    void swapPanes(int a, int b);

    // Batch for `dirty` plus every pending pane. While a soft pair reload is
    // in flight (`loadInFlight`) a badged pane with a stand-in raster is left
    // alone, even when dirty: the engine still holds the previous frames.
    CompareDisplayBatchPlan plan(const std::vector<int> &dirty,
                                 const std::vector<uint8_t> &hasRaster, bool loadInFlight) const;
    // The plan was submitted as generation `gen` (latest-wins replaces any
    // live batch; its panes are carried by phase, not by a side list).
    void commit(const CompareDisplayBatchPlan &plan, uint64_t gen);
    // The live batch was dropped without landing (hide, teardown).
    void abandon()
    {
        m_liveGen = 0;
    }

    // Whether `index` has a live request in batch `gen` at all, and whether
    // it is the cheap one (per cell, not per batch).
    bool expects(int index, uint64_t gen) const;
    bool requestedCheap(int index, uint64_t gen) const;
    // One delivered cell of batch `gen`. Ignored when stale. Only a sharp,
    // non-preview raster settles a pane (Ready, badge off); a cheap LOD or a
    // wheel `preview` leaves it SharpPending under its badge.
    void deliver(int index, uint64_t gen, ComparePaneDelivery delivery, bool preview = false);
    // Batch `gen` finished applying. Requested panes without a delivered cell
    // count an idle landing (not for a `preview` batch: a preview is never an
    // obligation). Returns the panes failed by the idle backstop.
    std::vector<int> land(uint64_t gen, bool preview = false);
    // Pending panes without a live request: the follow-up batch. With
    // `badgeOnly` only panes still under the Loading badge (after a preview
    // batch, while the wheel debounce is still armed, the sharp pass for the
    // other panes waits for it).
    std::vector<int> awaitingRequest(bool badgeOnly = false) const;

  private:
    ComparePaneDisplayState *at(int index);
    void countIdle(ComparePaneDisplayState &state, int index, std::vector<int> &exhausted);

    std::vector<ComparePaneDisplayState> m_panes;
    uint64_t m_liveGen = 0;
};

// Merge the pane targeted by a debounced LOD refresh. -1 means "all panes".
// Two different panes inside one debounce window must not collapse onto the
// last one: with sync zoom every view emits scaleChanged(own index).
int mergeLodRefreshPane(bool pending, int current, int incoming);

} // namespace mviewer::ui
