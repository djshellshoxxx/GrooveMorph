#pragma once

#include "core/TransportResolver.h"

#include <cstdint>
#include <string>

namespace groovemorph::core::easy
{
// Everything the plain-language timing line needs; filled from the processor's status getters.
struct EasyTimingInput
{
    bool prepared = false;
    bool valid = false;            // processor has usable timing
    TimingSource source = TimingSource::Manual;
    bool playing = false;
    bool internalClock = false;    // the plugin's own transport decides play/stop (Standalone, Sample Clock)
    std::uint32_t missingFields = 0;
};

// One short sentence, no jargon. Examples: "Waiting for host transport", "Using sample clock".
[[nodiscard]] std::string describeEasyTiming(const EasyTimingInput& input);
} // namespace groovemorph::core::easy
