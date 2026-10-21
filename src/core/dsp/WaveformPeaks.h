#pragma once

#include <cstdint>
#include <vector>

namespace groovemorph::core
{
// Portable data behind the loop waveform overview (F09). No drawing here.

// Min/max per column. The samples are split into min(columns, numSamples) contiguous, equal-as-possible
// ranges that cover every sample exactly once; channels are combined by taking the extremes. Non-finite
// samples count as 0. Returns the number of columns written (0 for invalid input).
int computePeaks(const float* const* channels, int numChannels, std::int64_t numSamples, int columns,
                 std::vector<float>& minimums, std::vector<float>& maximums);

struct GridLine
{
    double fraction = 0.0; // 0..1 position inside the loop
    bool bar = false;      // a bar line (otherwise a beat line)
};
// Beat and bar lines of a loop that spans `loopBeats` meter beats and starts at timeline beat
// `originBeats` (the loop is read as timeline beat origin + position). Lines sit on whole timeline beats;
// a bar line is a whole timeline beat divisible by beatsPerBar. At most 2048 lines (finer grids are
// thinned to bar lines only). Empty for invalid input.
std::vector<GridLine> computeBeatGrid(double loopBeats, double originBeats, int beatsPerBar);

// Playhead position in 0..1 for a timeline beat. 0 for invalid input.
double loopPlayFraction(double timelineBeat, double originBeats, double loopBeats) noexcept;
} // namespace groovemorph::core
