#include "core/SceneScheduler.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace groovemorph::core
{
namespace
{
std::optional<double> intervalPpq(const PhraseGrid& grid,
                                  Quantization quantization) noexcept
{
    if (grid.meterNumerator < 1 || grid.meterDenominator <= 0
        || grid.phraseBars < 1)
        return std::nullopt;

    double beats = 0.0;
    switch (quantization)
    {
        case Quantization::Beat:
            beats = 1.0;
            break;
        case Quantization::Bar:
            beats = static_cast<double>(grid.meterNumerator);
            break;
        case Quantization::Phrase:
            beats = static_cast<double>(grid.meterNumerator)
                * static_cast<double>(grid.phraseBars);
            break;
    }
    const auto value = beats * 4.0 / static_cast<double>(grid.meterDenominator);
    if (!std::isfinite(value) || value <= 0.0)
        return std::nullopt;
    return value;
}
} // namespace

bool SceneScheduler::request(std::uint32_t sceneId, Quantization quantization) noexcept
{
    if (quantization != Quantization::Beat && quantization != Quantization::Bar
        && quantization != Quantization::Phrase)
        return false;

    pending_ = true;
    immediate_ = false;
    targetResolved_ = false;
    sceneId_ = sceneId;
    quantization_ = quantization;
    targetPpq_ = 0.0;
    return true;
}

void SceneScheduler::requestImmediate(std::uint32_t sceneId) noexcept
{
    pending_ = true;
    immediate_ = true;
    targetResolved_ = false;
    sceneId_ = sceneId;
    targetPpq_ = 0.0;
}

void SceneScheduler::cancel() noexcept
{
    pending_ = false;
    immediate_ = false;
    targetResolved_ = false;
}

std::optional<std::uint32_t> SceneScheduler::pendingSceneId() const noexcept
{
    if (!pending_)
        return std::nullopt;
    return sceneId_;
}

std::optional<double> SceneScheduler::nextBoundaryPpq(
    double currentPpq,
    std::optional<double> barStartPpq,
    const PhraseGrid& grid,
    Quantization quantization) noexcept
{
    const auto interval = intervalPpq(grid, quantization);
    if (!interval || !std::isfinite(currentPpq))
        return std::nullopt;

    double anchor = 0.0;
    if (quantization == Quantization::Phrase)
    {
        if (!std::isfinite(grid.originPpq))
            return std::nullopt;
        anchor = grid.originPpq;
    }
    else
    {
        if (!barStartPpq || !std::isfinite(*barStartPpq))
            return std::nullopt;
        anchor = *barStartPpq;
    }

    constexpr double tolerance = 1.0e-12;
    const auto relative = currentPpq - anchor;
    const auto boundaryNumber = std::ceil(relative / *interval - tolerance);
    const auto target = anchor + boundaryNumber * *interval;
    if (!std::isfinite(target))
        return std::nullopt;
    return target;
}

std::optional<ScheduledSceneEvent> SceneScheduler::process(
    const BlockTiming& timing, std::uint32_t blockFrames) noexcept
{
    if (!pending_ || blockFrames == 0)
        return std::nullopt;

    if (immediate_)
    {
        const ScheduledSceneEvent event{sceneId_, 0, quantization_};
        cancel();
        return event;
    }

    if (!timing.isPlaying || !timing.ppqPosition
        || !std::isfinite(*timing.ppqPosition)
        || !PhraseClock::samplesPerMeterBeat(timing.sampleRate, timing.grid))
        return std::nullopt;

    if (timing.discontinuity)
        targetResolved_ = false;

    if (!targetResolved_)
    {
        const auto target = nextBoundaryPpq(*timing.ppqPosition,
                                            timing.ppqPositionOfLastBarStart,
                                            timing.grid,
                                            quantization_);
        if (!target)
            return std::nullopt;
        targetPpq_ = *target;
        targetResolved_ = true;
    }

    // If a seek or unreported jump moved beyond the saved target, recover at the
    // next boundary instead of firing late at offset zero.
    if (*timing.ppqPosition > targetPpq_ + 1.0e-9)
    {
        const auto target = nextBoundaryPpq(*timing.ppqPosition,
                                            timing.ppqPositionOfLastBarStart,
                                            timing.grid,
                                            quantization_);
        if (!target)
        {
            targetResolved_ = false;
            return std::nullopt;
        }
        targetPpq_ = *target;
    }

    if (!std::isfinite(timing.grid.tempoBpm) || timing.grid.tempoBpm < 20.0
        || timing.grid.tempoBpm > 300.0 || !std::isfinite(timing.sampleRate)
        || timing.sampleRate < 8000.0 || timing.sampleRate > 192000.0)
        return std::nullopt;

    const auto deltaPpq = targetPpq_ - *timing.ppqPosition;
    if (!std::isfinite(deltaPpq) || deltaPpq < -1.0e-9)
        return std::nullopt;

    const auto samplesPerPpq = timing.sampleRate * 60.0 / timing.grid.tempoBpm;
    const auto framesUntil = std::max(0.0, deltaPpq * samplesPerPpq);
    if (!std::isfinite(framesUntil)
        || framesUntil > static_cast<double>(std::numeric_limits<std::uint32_t>::max()))
        return std::nullopt;

    // Never fire early. An event exactly at the block end belongs to next block.
    const auto offset = static_cast<std::uint64_t>(std::ceil(framesUntil - 1.0e-9));
    if (offset >= blockFrames)
        return std::nullopt;

    const ScheduledSceneEvent event{
        sceneId_, static_cast<std::uint32_t>(offset), quantization_};
    cancel();
    return event;
}
} // namespace groovemorph::core
