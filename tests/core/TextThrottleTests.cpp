#include "core/viz/TextThrottle.h"

#include <iostream>
#include <string_view>

using groovemorph::core::viz::TextThrottle;

namespace {
int failures = 0;
void expect(std::string_view name, bool condition) {
    if (!condition) { std::cerr << name << ": check failed\n"; ++failures; }
}
} // namespace

int main() {
    // UI-TH-1: first text is written; unchanged text never is, however much time passes.
    {
        TextThrottle t(250);
        expect("TH1 first write", t.apply(0, "a"));
        expect("TH1 unchanged", !t.apply(10, "a") && !t.apply(1000, "a") && !t.apply(100000, "a"));
    }
    // UI-TH-2: a change is held back inside the interval and written after it.
    {
        TextThrottle t(250);
        (void) t.apply(1000, "a");
        expect("TH2 change inside interval is held", !t.apply(1100, "b"));
        expect("TH2 still held", !t.apply(1249, "b"));
        expect("TH2 written at the interval", t.apply(1250, "b"));
        expect("TH2 then unchanged", !t.apply(1260, "b"));
    }
    // UI-TH-3: at 30 Hz refreshes with a text that changes every refresh the write rate is at most 4 Hz.
    {
        TextThrottle t(250);
        int writes = 0;
        for (int frame = 0; frame < 300; ++frame) // 10 s at 30 Hz
            if (t.apply(static_cast<std::int64_t>(frame) * 1000 / 30, std::string("text ") + std::to_string(frame)))
                ++writes;
        expect("TH3 at most 4 Hz", writes <= 4 * 10 + 1 && writes >= 30);
    }
    // Backwards clock never freezes the text; reset starts over.
    {
        TextThrottle t(250);
        (void) t.apply(5000, "a");
        expect("TH3 backwards clock", t.apply(100, "b"));
        t.reset();
        expect("TH3 reset writes again", t.apply(101, "b"));
    }

    if (failures == 0) std::cout << "TextThrottleTests: all checks passed\n";
    return failures == 0 ? 0 : 1;
}
