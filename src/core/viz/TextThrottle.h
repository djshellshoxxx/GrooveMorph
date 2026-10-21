#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace groovemorph::core::viz
{
// Rate limit for text that is rewritten from a fast refresh (accessibility descriptions of the Visual
// views). apply() is true when the text should be written now: it differs from the last text that was
// written AND at least minIntervalMs have passed since that write (the first text is always written).
// Callers keep refreshing, so the newest text is written by a later call. Message thread only.
class TextThrottle final
{
public:
    explicit TextThrottle(std::int64_t minIntervalMs = 250) noexcept : interval(minIntervalMs) {}

    [[nodiscard]] bool apply(std::int64_t nowMs, std::string_view text)
    {
        if (written && text == last)
            return false;
        // A clock that moved backwards counts as elapsed, so the text can never get stuck.
        if (written && nowMs >= lastMs && nowMs - lastMs < interval)
            return false;
        last.assign(text);
        lastMs = nowMs;
        written = true;
        return true;
    }
    void reset() noexcept
    {
        written = false;
        last.clear();
    }

private:
    std::int64_t interval;
    std::int64_t lastMs = 0;
    bool written = false;
    std::string last;
};
} // namespace groovemorph::core::viz
