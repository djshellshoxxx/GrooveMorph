#pragma once

namespace groovemorph::core
{
// Simple transient detector for audio-triggered actions (F07). A fast and a slow envelope follower
// of the rectified input are compared; an onset is reported when the fast envelope rises above the
// slow one by the sensitivity-dependent ratio, then ignored for a refractory period.
// Real-time safe (noexcept, no allocation). One instance per detected channel.
class OnsetDetector final
{
public:
    void prepare(double sampleRate) noexcept;
    void reset() noexcept;
    // sensitivity 0..1 (1 = most sensitive). Returns the number of onsets found in the block.
    int process(const float* samples, int numSamples, float sensitivity) noexcept;

private:
    double rate = 48000.0;
    float fast = 0.0f, slow = 0.0f;
    int refractory = 0;
    int warmup = 0;
    bool armed = true;
    float fastAttack = 0.0f, fastRelease = 0.0f, slowAttack = 0.0f, slowRelease = 0.0f;
    int refractorySamples = 0;
    int warmupSamples = 0;
};
} // namespace groovemorph::core
