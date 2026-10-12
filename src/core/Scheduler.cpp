#include "core/Scheduler.h"

namespace groovemorph::core
{
namespace
{
std::optional<Quantization> quantizationFor(TransitionMode mode) noexcept
{
    switch (mode)
    {
        case TransitionMode::NextBeat:
            return Quantization::Beat;
        case TransitionMode::NextBar:
            return Quantization::Bar;
        case TransitionMode::NextPhrase:
            return Quantization::Phrase;
        case TransitionMode::Immediate:
        case TransitionMode::Manual:
            break;
    }
    return std::nullopt;
}
} // namespace

Scheduler::Scheduler(std::size_t initialVariation) noexcept
    : active(initialVariation < variationCount ? initialVariation : 0)
{
}

bool Scheduler::request(std::size_t variation, TransitionMode mode) noexcept
{
    if (variation >= variationCount)
        return false;
    if (variation == active)
    {
        cancel();
        return true;
    }
    hasPending = true;
    pendingVariation = variation;
    pendingMode = mode;
    manualTriggered = false;
    untilSwitch = mode == TransitionMode::Immediate ? std::optional<std::int64_t>{0}
                                                    : std::nullopt;
    return true;
}

void Scheduler::cancel() noexcept
{
    hasPending = false;
    manualTriggered = false;
    untilSwitch.reset();
}

bool Scheduler::trigger() noexcept
{
    if (!hasPending || pendingMode != TransitionMode::Manual)
        return false;
    manualTriggered = true;
    untilSwitch = 0;
    return true;
}

void Scheduler::reset(std::size_t activeVariation) noexcept
{
    active = activeVariation < variationCount ? activeVariation : 0;
    cancel();
}

std::optional<std::size_t> Scheduler::queuedVariation() const noexcept
{
    return hasPending ? std::optional<std::size_t>{pendingVariation} : std::nullopt;
}

std::optional<TransitionMode> Scheduler::queuedMode() const noexcept
{
    return hasPending ? std::optional<TransitionMode>{pendingMode} : std::nullopt;
}

SwitchEvent Scheduler::process(const ResolvedTiming& timing, int numSamples) noexcept
{
    if (!hasPending)
        return {};

    std::optional<std::int64_t> offset;
    if (pendingMode == TransitionMode::Immediate
        || (pendingMode == TransitionMode::Manual && manualTriggered))
        offset = 0;
    else if (const auto q = quantizationFor(pendingMode); q && timing.isPlaying)
        offset = PhraseClock::samplesUntilBoundary(timing.currentSample, timing.sampleRate,
                                                   timing.grid, *q);

    untilSwitch = offset;
    if (!offset || numSamples <= 0 || *offset >= numSamples)
        return {};

    active = pendingVariation;
    hasPending = false;
    manualTriggered = false;
    untilSwitch.reset();
    return {true, active, *offset};
}
} // namespace groovemorph::core
