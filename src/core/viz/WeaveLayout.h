#pragma once

#include "core/viz/PathResolver.h"
#include "core/viz/PlannedPath.h"

#include <cstddef>
#include <cstdint>

// Warp Weave geometry (docs/visual-feedback/SPEC.md section 7). Pure: frames in, normalised
// thread geometry out. The top row is what is heard, the bottom row is where it was played.
namespace groovemorph::core::viz
{
enum class Effect : std::uint8_t
{
    Live = 0,
    Repeat = 1,
    Reverse = 2,
    Gate = 3,
    HalfTime = 4,
    TapeStop = 5,
};

struct WeaveParams
{
    double viewStartBeat = 0.0; // left edge of the heard (top) row, meter beats
    double spanBeats = 4.0;     // width of the top row
    double leadBeats = 2.0;     // extra width on the left so earlier sources stay on the same scale
    int threads = 160;          // requested thread count (16..256)
    double nowBeat = 0.0;       // ink front: threads right of it are not yet heard
};

struct Thread
{
    float xTop = 0.0f;    // 0..1 over [start - lead, start + span]
    float xBottom = 0.0f; // same scale
    float opacity = 0.0f;
    float width = 1.0f;
    std::uint8_t effectMask = 0; // bit per OperationType that moved this thread
    Effect primary = Effect::Live;
    bool planned = false;    // future (pencil) thread; produced by the planner, not by buildWeave
    bool noData = false;     // source or output moment outside the recorded window
    bool gateClosed = false; // gate was (mostly) closed at this moment
    bool clipped = false;    // source lies left of the drawn range and was clamped to the edge
    bool tentative = false;  // planned thread beyond a queued scene switch, auto-return or similar boundary
};

// Builds one thread per requested slot for the heard part of the view (output beat <= nowBeat).
// Returns the number written (<= min(params.threads, capacity)); allocation-free.
[[nodiscard]] std::size_t buildWeave(const FrameWindow& w, const WeaveParams& p, Thread* out,
                                     std::size_t capacity) noexcept;

// Planned (pencil) threads for the part of the view right of the ink front (output beat > nowBeat), from
// the committed scene via PlannedPath. Threads at or after tentativeFromBeat are flagged tentative.
[[nodiscard]] std::size_t buildPlannedWeave(const PlanContext& ctx, const WeaveParams& p, double tentativeFromBeat,
                                            Thread* out, std::size_t capacity) noexcept;
} // namespace groovemorph::core::viz
