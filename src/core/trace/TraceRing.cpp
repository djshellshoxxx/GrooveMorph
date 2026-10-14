#include "core/trace/TraceRing.h"

#include <algorithm>
#include <cstring>

namespace groovemorph::core::trace
{
TraceRing::TraceRing() : slots(ringCapacity) {}

void TraceRing::push(const TraceFrame& frame) noexcept
{
    const std::uint64_t i = written.load(std::memory_order_relaxed);
    Slot& slot = slots[static_cast<std::size_t>(i & mask)];

    std::array<std::uint64_t, wordCount> tmp;
    std::memcpy(tmp.data(), &frame, sizeof(TraceFrame));

    slot.seq.store(2 * i + 1, std::memory_order_relaxed); // odd: write in progress
    std::atomic_thread_fence(std::memory_order_release);
    for (std::size_t k = 0; k < wordCount; ++k)
        slot.words[k].store(tmp[k], std::memory_order_relaxed);
    slot.seq.store(2 * i + 2, std::memory_order_release); // even: stable, belongs to index i
    written.store(i + 1, std::memory_order_release);
}

std::size_t TraceRing::copyLatest(TraceFrame* out, std::size_t maxFrames) const noexcept
{
    if (out == nullptr || maxFrames == 0)
        return 0;
    const std::uint64_t total = written.load(std::memory_order_acquire);
    const std::uint64_t readable = std::min<std::uint64_t>({static_cast<std::uint64_t>(maxFrames), ringCapacity - 1, total});
    std::size_t count = 0;
    for (std::uint64_t i = total - readable; i < total; ++i)
    {
        const Slot& slot = slots[static_cast<std::size_t>(i & mask)];
        const std::uint64_t expected = 2 * i + 2;
        if (slot.seq.load(std::memory_order_acquire) != expected)
            continue; // overwritten by a newer lap or still being written
        std::array<std::uint64_t, wordCount> tmp;
        for (std::size_t k = 0; k < wordCount; ++k)
            tmp[k] = slot.words[k].load(std::memory_order_relaxed);
        std::atomic_thread_fence(std::memory_order_acquire);
        if (slot.seq.load(std::memory_order_relaxed) != expected)
            continue; // torn during the copy
        std::memcpy(&out[count], tmp.data(), sizeof(TraceFrame));
        ++count;
    }
    return count;
}

std::size_t latestEpochStart(const TraceFrame* frames, std::size_t count) noexcept
{
    if (frames == nullptr || count == 0)
        return 0;
    const std::uint32_t epoch = frames[count - 1].epoch;
    std::size_t i = count - 1;
    while (i > 0 && frames[i - 1].epoch == epoch)
        --i;
    return i;
}
} // namespace groovemorph::core::trace
