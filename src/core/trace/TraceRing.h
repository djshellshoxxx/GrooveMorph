#pragma once

#include "core/trace/TraceFrame.h"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace groovemorph::core::trace
{
// Single audio-thread writer, single UI reader, overwrite-oldest, fixed capacity.
// Each slot stores the frame as atomic 64-bit words guarded by a per-slot sequence stamp, so the
// protocol has no data race in the C++ memory model. Allocation happens only in the constructor.
class TraceRing final
{
public:
    TraceRing();

    // Tracing costs nothing while inactive: the engine only pushes when active() is true.
    void setActive(bool on) noexcept { activeFlag.store(on, std::memory_order_relaxed); }
    [[nodiscard]] bool active() const noexcept { return activeFlag.load(std::memory_order_relaxed); }

    // Audio thread only (exactly one writer).
    void push(const TraceFrame& frame) noexcept;

    // Reader thread. Copies up to maxFrames of the newest frames, oldest first, into out.
    // Frames overwritten or torn during the copy are skipped. Returns the number written.
    [[nodiscard]] std::size_t copyLatest(TraceFrame* out, std::size_t maxFrames) const noexcept;

    [[nodiscard]] std::uint64_t pushedCount() const noexcept { return written.load(std::memory_order_acquire); }

private:
    static constexpr std::size_t wordCount = sizeof(TraceFrame) / sizeof(std::uint64_t);
    static constexpr std::uint64_t mask = ringCapacity - 1;
    static_assert((ringCapacity & (ringCapacity - 1)) == 0, "capacity must be a power of two");
    static_assert(std::atomic<std::uint64_t>::is_always_lock_free);

    struct Slot
    {
        std::atomic<std::uint64_t> seq{0};
        std::array<std::atomic<std::uint64_t>, wordCount> words{};
    };

    std::vector<Slot> slots;
    std::atomic<std::uint64_t> written{0};
    std::atomic<bool> activeFlag{false};
};

// Index of the first frame belonging to the newest epoch in an ascending frame list (0 when empty).
[[nodiscard]] std::size_t latestEpochStart(const TraceFrame* frames, std::size_t count) noexcept;
} // namespace groovemorph::core::trace
