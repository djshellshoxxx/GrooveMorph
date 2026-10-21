// N05 acceptance tests E2-E4 (docs/specs/N05-EASY-MODE.md): layout, plain-language timing, guide steps.
#include "core/easy/EasyLayout.h"
#include "core/easy/EasyText.h"
#include "core/easy/FirstRunSteps.h"

#include <iostream>
#include <string>
#include <string_view>
#include <vector>

using namespace groovemorph::core::easy;
using groovemorph::core::TimingSource;

namespace
{
int failures = 0;
void expect(std::string_view name, bool ok)
{
    if (!ok)
    {
        std::cerr << name << ": check failed\n";
        ++failures;
    }
}

bool inside(const IntRect& r, int w, int h)
{
    return r.w > 0 && r.h > 0 && r.x >= 0 && r.y >= 0 && r.right() <= w && r.bottom() <= h;
}

bool disjoint(const IntRect& a, const IntRect& b)
{
    return a.right() <= b.x || b.right() <= a.x || a.bottom() <= b.y || b.bottom() <= a.y;
}

void checkLayout(int w, int h)
{
    const std::string tag = std::to_string(w) + "x" + std::to_string(h);
    const auto layout = computeEasyLayout(w, h);
    std::vector<IntRect> all{layout.title, layout.showGuide, layout.openFull, layout.timing,
                             layout.surprise, layout.undo, layout.panic, layout.message};
    for (const auto& r : layout.scenes) all.push_back(r);
    for (const auto& r : layout.macroLabels) all.push_back(r);
    for (const auto& r : layout.macroSliders) all.push_back(r);
    bool fits = true, separate = true;
    for (std::size_t i = 0; i < all.size(); ++i)
    {
        fits = fits && inside(all[i], w, h);
        for (std::size_t j = i + 1; j < all.size(); ++j)
            separate = separate && disjoint(all[i], all[j]);
    }
    expect(tag + ": everything inside the bounds", fits);
    expect(tag + ": nothing overlaps", separate);
    bool big = layout.surprise.h >= EasyLayout::minButtonHeight && layout.undo.h >= EasyLayout::minButtonHeight
               && layout.panic.h >= EasyLayout::minButtonHeight && layout.openFull.h >= EasyLayout::minButtonHeight - 8
               && layout.showGuide.h >= EasyLayout::minButtonHeight - 8;
    for (const auto& r : layout.scenes) big = big && r.h >= EasyLayout::minButtonHeight && r.w >= EasyLayout::minButtonHeight;
    expect(tag + ": buttons are large enough", big);
    expect(tag + ": scene buttons dominate", layout.scenes[0].h >= layout.undo.h);
}
} // namespace

int main()
{
    // N05-E2: the editor's minimum, default and maximum sizes, plus a small display fit.
    checkLayout(1230, 700);
    checkLayout(1290, 740);
    checkLayout(1900, 1100);
    checkLayout(640, 400);
    {
        const auto a = computeEasyLayout(1230, 700);
        const auto b = computeEasyLayout(1230, 700);
        expect("layout is deterministic", a.scenes[2].x == b.scenes[2].x && a.panic.w == b.panic.w);
        const auto c = computeEasyLayout(1900, 1100);
        expect("layout grows with the window", c.scenes[0].w > a.scenes[0].w && c.scenes[0].h > a.scenes[0].h);
        const auto tiny = computeEasyLayout(0, 0);
        expect("degenerate size does not crash", tiny.scenes[0].w >= 0);
    }

    // N05-E3: plain-language timing.
    {
        EasyTimingInput in;
        expect("not prepared", describeEasyTiming(in) == "Audio is not running yet");
        in.prepared = true;
        expect("no timing", describeEasyTiming(in).rfind("No usable timing", 0) == 0);
        in.valid = true;
        in.source = TimingSource::Host;
        in.playing = false;
        expect("host stopped", describeEasyTiming(in) == "Waiting for host transport");
        in.playing = true;
        expect("host playing", describeEasyTiming(in) == "Following the host transport");
        in.source = TimingSource::Mixed;
        expect("mixed playing", describeEasyTiming(in).rfind("Following the host transport", 0) == 0);
        in.source = TimingSource::Manual;
        expect("manual playing", describeEasyTiming(in).rfind("Using the manual tempo", 0) == 0);
        in.source = TimingSource::FreeRun;
        expect("free run", describeEasyTiming(in) == "Using sample clock");
        in.source = TimingSource::Host;
        in.internalClock = true;
        expect("internal clock", describeEasyTiming(in) == "Using sample clock");
        in.playing = false;
        expect("internal clock stopped", describeEasyTiming(in).rfind("Using sample clock", 0) == 0);
        bool plain = true;
        for (const auto source : {TimingSource::Host, TimingSource::Mixed, TimingSource::Manual, TimingSource::FreeRun})
            for (const bool playing : {false, true})
                for (const bool internal : {false, true})
                {
                    EasyTimingInput x{true, true, source, playing, internal, 0};
                    const auto text = describeEasyTiming(x);
                    plain = plain && !text.empty() && text.find("PPQ") == std::string::npos;
                }
        expect("every combination has plain text", plain);
    }

    // N05-E4: guide steps and navigation.
    {
        expect("exactly four steps", firstRunStepCount == 4);
        expect("step order", std::string(firstRunStep(0).title).find("transport") != std::string::npos
                                 && std::string(firstRunStep(1).title).find("scene") != std::string::npos
                                 && std::string(firstRunStep(2).title).find("Surprise me") != std::string::npos
                                 && std::string(firstRunStep(3).title).find("full view") != std::string::npos);
        bool text = true;
        for (std::size_t i = 0; i < firstRunStepCount; ++i)
        {
            const std::string body = firstRunStep(i).body;
            text = text && !body.empty() && body.find("\xE2\x80\x94") == std::string::npos; // no em dash
        }
        expect("steps have text without em dashes", text);
        expect("index clamps", &firstRunStep(99) == &firstRunStep(3));

        FirstRunProgress progress;
        expect("starts at the first step", progress.isFirst() && !progress.isLast() && !progress.finished());
        progress.back();
        expect("back stops at the first step", progress.index() == 0);
        progress.next(); progress.next();
        expect("moves forward", progress.index() == 2 && !progress.isLast());
        progress.next();
        expect("last step reached", progress.isLast() && progress.index() == 3 && !progress.finished());
        progress.next();
        expect("next on the last step finishes", progress.finished() && progress.index() == 3);
        progress.restart();
        expect("restart", progress.isFirst() && !progress.finished());
        progress.dismiss();
        expect("dismiss finishes from any step", progress.finished());
    }

    if (failures == 0)
        std::cout << "EasyViewTests: all checks passed\n";
    return failures == 0 ? 0 : 1;
}
