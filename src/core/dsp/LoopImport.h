#pragma once

namespace groovemorph::core
{
// How an imported/captured loop is mapped onto the beat grid. A loop playing at the plugin tempo T runs at
// T / nativeBpm times its original speed (pitch follows tempo), so the loop's native tempo decides
// everything: nativeBpm = beats * 60 / seconds.
struct LoopMapping
{
    double beats = 0.0;       // meter beats the whole file spans
    double nativeBpm = 0.0;   // tempo at which it plays at its original speed
    double bars = 0.0;        // beats / beatsPerBar (may be fractional for loops with a tail)
    bool fromBpm = false;     // beats derived from a given tempo (true) or from a bar count (false)
    bool valid = false;
};

// Exactly one source of truth is used, in this order:
//   nativeBpm > 0 -> beats = seconds * nativeBpm / 60 (the loop may be a fractional number of bars: a tail or
//                    pre-roll is kept and the loop simply repeats every `beats` beats);
//   bars > 0      -> beats = bars * beatsPerBar;
//   otherwise     -> automatic: the bar count (1, 2, 4, 8 or 16) whose implied tempo is within 60..200 BPM and
//                    closest to currentBpm; when none is in range the overall closest.
[[nodiscard]] LoopMapping mapLoop(double seconds, int beatsPerBar, double currentBpm, int bars, double nativeBpm) noexcept;
} // namespace groovemorph::core
