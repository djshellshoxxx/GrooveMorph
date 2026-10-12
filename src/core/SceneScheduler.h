#pragma once

#include "PhraseClock.h"

#include <cstdint>
#include <optional>

namespace groovemorph::core
{
struct BlockTiming
{
    bool isPlaying = false;
    std::optional<double> ppqPosition;
    std::optional<double> ppqPositionOfLastBarStart;
    double sampleRate = 48000.0;
    PhraseGrid grid{};
    bool discontinuity = false; // Host seek or loop wrap detected by the caller.
};

struct ScheduledSceneEvent
{
    std::uint32_t sceneId = 0;
    std::uint32_t sampleOffset = 0;
    Quantization quantization = Quantization::Beat;
};

// Fixed-state scheduler. All methods are owned by the audio/control thread that
// calls process(); cross-thread UI commands must arrive through a bounded queue.
class SceneScheduler final
{
public:
    // A new request replaces the single pending request. No allocation occurs.
    [[nodiscard]] bool request(std::uint32_t sceneId, Quantization quantization) noexcept;
    void requestImmediate(std::uint32_t sceneId) noexcept;
    void cancel() noexcept;

    [[nodiscard]] bool hasPending() const noexcept { return pending_; }
    [[nodiscard]] std::optional<std::uint32_t> pendingSceneId() const noexcept;

    // Consume a fresh block-start snapshot every block. The scheduler stores a
    // musical PPQ target, never a sample countdown. Tempo changes therefore
    // recalculate the event offset from the same target on the next callback.
    [[nodiscard]] std::optional<ScheduledSceneEvent> process(
        const BlockTiming& timing, std::uint32_t blockFrames) noexcept;

private:
    [[nodiscard]] static std::optional<double> nextBoundaryPpq(
        double currentPpq,
        std::optional<double> barStartPpq,
        const PhraseGrid& grid,
        Quantization quantization) noexcept;

    bool pending_ = false;
    bool immediate_ = false;
    bool targetResolved_ = false;
    std::uint32_t sceneId_ = 0;
    Quantization quantization_ = Quantization::Beat;
    double targetPpq_ = 0.0;
};
} // namespace groovemorph::core
