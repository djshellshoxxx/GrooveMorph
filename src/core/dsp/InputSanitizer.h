#pragma once

#include <cmath>

// docs/specs/W3-INPUT-SANITIZE.md: NaN and +-Inf never travel through the plugin. Header only, noexcept,
// allocation-free; finite samples (including denormals) are left bit-identical.
namespace groovemorph::core::dsp
{
// Replaces every non-finite sample with exactly 0.0f. Returns how many samples were replaced.
inline int sanitizeBlock(float* const* channels, int numChannels, int numSamples) noexcept
{
    int replaced = 0;
    if (channels == nullptr || numSamples <= 0)
        return 0;
    for (int c = 0; c < numChannels; ++c)
    {
        float* data = channels[c];
        if (data == nullptr)
            continue;
        for (int i = 0; i < numSamples; ++i)
            if (!std::isfinite(data[i]))
            {
                data[i] = 0.0f;
                ++replaced;
            }
    }
    return replaced;
}
} // namespace groovemorph::core::dsp
