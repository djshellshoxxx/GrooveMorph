#pragma once

namespace groovemorph::core
{
// Stateless synthetic drum loop (kick on every beat, snare on beats 2 and 4, closed hat on
// off-beat eighths) for evaluating the rhythmic effects without external audio. Output is a pure
// function of the musical position, so seeks, loops and tempo changes cannot desynchronise it.
// Real-time safe: noexcept, no allocation, no state.
class DemoSource final
{
public:
    static constexpr float level = 0.35f; // about -9 dBFS peak

    // Overwrites `numSamples` of every channel. beatAtStart: meter beats since the grid origin.
    static void render(float* const* channels, int numChannels, int numSamples, double beatAtStart,
                       double samplesPerBeat, double sampleRate) noexcept;
};
} // namespace groovemorph::core
