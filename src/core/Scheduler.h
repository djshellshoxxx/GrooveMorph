#pragma once

#include "core/OperationParams.h"
#include "core/TransportResolver.h"

#include <cstddef>
#include <cstdint>
#include <optional>

namespace groovemorph::core
{
// GM-FR-004 transition timing for variation switches.
enum class TransitionMode
{
    Immediate,  // applies at offset 0 of the next processed block, even when stopped
    NextBeat,   // next meter-beat boundary
    NextBar,    // next bar boundary
    NextPhrase, // next phrase boundary
    Manual      // waits for trigger()
};

struct SwitchEvent
{
    bool switched = false;
    std::size_t variation = 0;     // new active variation when switched
    std::int64_t sampleOffset = 0; // offset inside the block, 0 <= offset < block length
};

// Queues one variation switch and resolves it to a sample offset per audio block.
// Real-time safe (noexcept, no allocation, no locks). Not thread-safe: the owner must
// marshal requests from other threads (e.g. via a lock-free FIFO) onto the audio thread.
//
// Quantized modes (NextBeat/NextBar/NextPhrase) only fire while timing.isPlaying is true.
// While the transport is stopped they stay pending (samplesUntilSwitch() is nullopt) and
// fire at the first boundary after playback resumes. The boundary is recomputed from the
// resolved position every block, so seeks and position jumps are handled. A boundary that
// falls exactly on the block start fires at offset 0.
class Scheduler final
{
public:
    explicit Scheduler(std::size_t initialVariation = 0) noexcept;

    // Queues a switch; replaces any pending request. Returns false (and changes nothing)
    // if variation >= variationCount. Requesting the active variation cancels a pending one.
    bool request(std::size_t variation, TransitionMode mode) noexcept;
    void cancel() noexcept;
    // Fires a pending Manual request at offset 0 of the next block. Returns false if none.
    bool trigger() noexcept;
    // Sets the active variation directly and clears any pending request (state restore).
    void reset(std::size_t activeVariation) noexcept;

    // Call once per block; timing.currentSample is the block's first sample.
    [[nodiscard]] SwitchEvent process(const ResolvedTiming& timing, int numSamples) noexcept;

    [[nodiscard]] std::size_t activeVariation() const noexcept { return active; }
    [[nodiscard]] std::optional<std::size_t> queuedVariation() const noexcept;
    [[nodiscard]] std::optional<TransitionMode> queuedMode() const noexcept;
    // Samples from the start of the last processed block to the scheduled switch.
    // nullopt when nothing is queued, the mode is Manual (untriggered), or the transport
    // is stopped for a quantized mode. Updated by process(); request() resets it to
    // 0 for Immediate and nullopt otherwise until the next process().
    [[nodiscard]] std::optional<std::int64_t> samplesUntilSwitch() const noexcept
    {
        return untilSwitch;
    }

private:
    std::size_t active = 0;
    bool hasPending = false;
    std::size_t pendingVariation = 0;
    TransitionMode pendingMode = TransitionMode::Immediate;
    bool manualTriggered = false;
    std::optional<std::int64_t> untilSwitch;
};
} // namespace groovemorph::core
