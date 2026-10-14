// VF-T06b: lap summaries are built from measured frames; repeats line up and changes are detected.
#include "core/control/PlacementEvaluator.h"
#include "core/dsp/RhythmEngine.h"
#include "core/trace/TraceRing.h"
#include "core/viz/LapRecorder.h"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <string_view>
#include <vector>

using namespace groovemorph::core;
using namespace groovemorph::core::trace;
using namespace groovemorph::core::viz;
namespace ctl = groovemorph::core::control;

namespace {
int failures = 0;
void expect(std::string_view name, bool condition) {
    if (!condition) { std::cerr << name << ": check failed\n"; ++failures; }
}

constexpr double sampleRate = 48000.0;
constexpr double spb = 4800.0;
constexpr double phraseBeats = 16.0;       // 4 bars of 4/4
constexpr std::size_t phraseSamples = 76800;
const ctl::GridContext grid{4, 4};

// Engine driven like the processor; scene may change at a given phrase.
std::vector<TraceFrame> run(const VariationSettings& first, const VariationSettings& second, std::size_t switchAtSample, std::size_t total) {
    RhythmEngine engine;
    engine.prepare(sampleRate, 1024, 1);
    TraceRing ring;
    ring.setActive(true);
    engine.setTraceSink(&ring);
    std::vector<float> x(total, 0.1f);
    std::vector<TraceFrame> all;
    std::vector<TraceFrame> tmp(ringCapacity);
    for (std::size_t done = 0; done < total;) {
        const VariationSettings& scene = done < switchAtSample ? first : second;
        std::uint32_t mask = 0;
        for (std::size_t i = 0; i < operationCount; ++i) if (scene.operations[i].enabled) mask |= 1u << i;
        int n = static_cast<int>(std::min<std::size_t>(256, total - done));
        RhythmTiming rhythm{spb, static_cast<double>(done) / spb, true};
        VariationSettings gated;
        const VariationSettings* render = &scene;
        if (ctl::isActive(scene.control)) {
            const auto t = ctl::evaluate(scene.control, grid, rhythm.beatAtBlockStart, mask);
            if (t.gated) {
                gated = scene;
                for (std::size_t i = 0; i < operationCount; ++i) {
                    if (!t.active[i]) gated.operations[i].enabled = false;
                    else { gated.operations[i].depth *= t.depthScale[i]; if (t.sliceBeats[i] > 0.0f) gated.operations[i].sliceBeats = t.sliceBeats[i]; rhythm.phaseOffsetBeats[i] = t.phaseOffsetBeats[i]; }
                }
                render = &gated;
                const double samples = t.ticksUntilChange / ctl::ticksPerBeat * spb;
                if (std::isfinite(samples) && samples < static_cast<double>(n)) n = std::max(1, static_cast<int>(std::llround(samples)));
            }
        }
        if (switchAtSample > done && done + static_cast<std::size_t>(n) > switchAtSample) n = static_cast<int>(switchAtSample - done);
        float* ch[] = {x.data() + done};
        engine.process(ch, 1, n, *render, 1.0f, rhythm);
        done += static_cast<std::size_t>(n);
        const auto got = ring.copyLatest(tmp.data(), tmp.size());
        // keep only frames not already collected
        for (std::size_t i = 0; i < got; ++i)
            if (all.empty() || tmp[i].sample > all.back().sample || tmp[i].epoch != all.back().epoch) all.push_back(tmp[i]);
    }
    return all;
}

VariationSettings windowed(float start, float span) {
    VariationSettings v;
    v[OperationType::Repeat] = {true, 1.0f, 0.25f};
    v.control.placement.enabled = true;
    v.control.placement.window = ctl::WindowMode::Custom;
    v.control.placement.startPhase = start;
    v.control.placement.spanPhase = span;
    return v;
}
} // namespace

int main() {
    // Cells and coverage helpers.
    {
        LapCell c;
        c.frames = 10;
        c.count[0] = 10;
        c.count[1] = 4;
        expect("coverage full", c.coverage(0) == 255);
        expect("coverage partial", c.coverage(1) == 102);
        expect("coverage empty cell", LapCell{}.coverage(0) == 0);
        expect("active threshold", c.active(0) && !c.active(1));
    }

    // Two laps with the same placement window: effect only in the second half; laps line up exactly.
    {
        const auto frames = run(windowed(0.5f, 0.5f), windowed(0.5f, 0.5f), 0, phraseSamples * 3);
        LapRecorder rec;
        rec.feed(frames.data(), frames.size(), phraseBeats);
        expect("three laps seen", rec.lapCount() == 3);
        const Lap* a = rec.lapAt(0); // newest
        const Lap* b = rec.lapAt(1);
        expect("laps exist", a && b && a->index == b->index + 1);
        bool firstHalfQuiet = true, secondHalfActive = true;
        const Lap* mid = rec.lapAt(1); // a complete lap
        for (std::size_t c = 4; c < lapCells / 2 - 4; ++c) firstHalfQuiet = firstHalfQuiet && !mid->cells[c].active(0);
        for (std::size_t c = lapCells / 2 + 4; c < lapCells - 4; ++c) secondHalfActive = secondHalfActive && mid->cells[c].frames > 0;
        expect("effect silent in the first half of the phrase", firstHalfQuiet);
        // Repeat's slot 0 plays live; the other three slots of each cycle are the moving part: expect substantial activity.
        std::size_t activeCells = 0;
        for (std::size_t c = lapCells / 2; c < lapCells; ++c) if (mid->cells[c].active(0)) ++activeCells;
        expect("effect active in the second half", secondHalfActive && activeCells > lapCells / 4);
        const auto d = lapDiff(*rec.lapAt(2), *rec.lapAt(1));
        std::size_t differing = 0;
        for (auto m : d.mask) if (m) ++differing;
        expect("identical laps do not differ", differing == 0);
    }

    // The scene changes at the start of lap 1: lap 1 differs from lap 0 exactly where the window moved.
    {
        const auto frames = run(windowed(0.5f, 0.5f), windowed(0.0f, 0.5f), phraseSamples, phraseSamples * 3);
        LapRecorder rec;
        rec.feed(frames.data(), frames.size(), phraseBeats);
        const Lap* old = rec.lapAt(2);
        const Lap* changed = rec.lapAt(1);
        expect("laps present", old && changed);
        const auto d = lapDiff(*old, *changed);
        std::size_t diffCells = 0;
        for (std::size_t c = 6; c < lapCells - 6; ++c) if (d.mask[c] & 1u) ++diffCells;
        expect("window move is detected as a difference", diffCells > lapCells / 3);
        expect("diff mask reports the repeat effect", (d.mask[lapCells / 4 + 3] & 1u) != 0); // mid-cycle: slot 0 of a repeat cycle is live
    }

    // Epoch change (seek) resets the recorder; non-timed frames are ignored.
    {
        auto frames = run(windowed(0.0f, 1.0f), windowed(0.0f, 1.0f), 0, phraseSamples + 12800);
        LapRecorder rec;
        rec.feed(frames.data(), frames.size(), phraseBeats);
        expect("laps recorded", rec.lapCount() >= 1);
        TraceFrame f = frames.back();
        f.epoch += 1; f.sample = 127;
        rec.feed(&f, 1, phraseBeats);
        expect("new epoch discards old laps", rec.lapCount() <= 1);
        rec.reset();
        expect("reset clears", rec.lapCount() == 0);
        TraceFrame idle; idle.epoch = 7; idle.sample = 127; idle.flags = 0;
        rec.feed(&idle, 1, phraseBeats);
        expect("untimed frames record nothing", rec.lapCount() == 0);
        rec.feed(&idle, 1, 0.0);
        rec.feed(nullptr, 5, phraseBeats);
        expect("bad input is ignored", rec.lapCount() == 0);
    }

    // Capacity: only the newest maxLaps are kept.
    {
        const auto frames = run(windowed(0.0f, 1.0f), windowed(0.0f, 1.0f), 0, phraseSamples * 11);
        LapRecorder rec;
        rec.feed(frames.data(), frames.size(), phraseBeats);
        expect("keeps at most maxLaps", rec.lapCount() == maxLaps);
        expect("newest first", rec.lapAt(0)->index == 10 && rec.lapAt(maxLaps - 1)->index == 10 - static_cast<std::int64_t>(maxLaps) + 1);
        expect("out of range", rec.lapAt(maxLaps) == nullptr);
    }

    if (failures == 0) std::cout << "LapRecorderTests: all checks passed\n";
    return failures == 0 ? 0 : 1;
}
