#pragma once

#include <cstddef>

// Content and navigation of the N05 first-run guide (no JUCE). The "seen" flag is persisted by the UI.
namespace groovemorph::core::easy
{
inline constexpr std::size_t firstRunStepCount = 4;

struct GuideStep
{
    const char* title;
    const char* body;
};

// Index is clamped to the last step.
[[nodiscard]] const GuideStep& firstRunStep(std::size_t index) noexcept;

class FirstRunProgress final
{
public:
    [[nodiscard]] std::size_t index() const noexcept { return current; }
    [[nodiscard]] bool isFirst() const noexcept { return current == 0; }
    [[nodiscard]] bool isLast() const noexcept { return current + 1 >= firstRunStepCount; }
    [[nodiscard]] bool finished() const noexcept { return done; }
    // Back stops at the first step. Next on the last step finishes the guide.
    void back() noexcept { if (current > 0) --current; }
    void next() noexcept { if (isLast()) done = true; else ++current; }
    void dismiss() noexcept { done = true; }
    void restart() noexcept { current = 0; done = false; }

private:
    std::size_t current = 0;
    bool done = false;
};
} // namespace groovemorph::core::easy
