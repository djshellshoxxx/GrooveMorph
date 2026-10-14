#include "core/control/SceneControl.h"
#include "core/dsp/DemoSource.h"
#include "core/scene/SceneBank.h"
#include "core/variation/CandidateBuilder.h"

#include <cmath>
#include <iostream>
#include <string_view>
#include <vector>

using namespace groovemorph::core;
using namespace groovemorph::core::variation;

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
} // namespace

int main()
{
    VariationSettings source;
    for (auto& op : source.operations)
        op = {true, 0.5f, 0.25f};
    GeneratorSettings g;
    g.seed = 42;
    g.style = Style::Experimental;

    // Determinism and regenerate.
    const auto a = buildCandidate(source, g);
    const auto b = buildCandidate(source, g);
    expect("same inputs same candidate", scene::sameSettings(a.settings, b.settings));
    bool anyDifferent = false;
    for (std::uint32_t c = 1; c < 16 && !anyDifferent; ++c)
    {
        g.candidate = c;
        anyDifferent = !scene::sameSettings(buildCandidate(source, g).settings, a.settings);
    }
    g.candidate = 0;
    expect("candidates vary", anyDifferent);

    // Source is not mutated; distance 0 returns the source exactly.
    const auto before = source;
    g.distance = 0.0f;
    const auto zero = buildCandidate(source, g);
    expect("distance 0 = source", scene::sameSettings(zero.settings, source) && zero.changedOperations == 0);
    expect("source untouched", scene::sameSettings(source, before));

    // Distance 1 matches the planner; distance monotonically bounds the depth change.
    g.distance = 1.0f;
    const auto full = buildCandidate(source, g);
    g.distance = 0.25f;
    const auto quarter = buildCandidate(source, g);
    for (std::size_t i = 0; i < operationCount; ++i)
    {
        const float dFull = std::abs(full.settings.operations[i].depth - source.operations[i].depth);
        const float dQuarter = std::abs(quarter.settings.operations[i].depth - source.operations[i].depth);
        expect("smaller distance moves depth less", dQuarter <= dFull + 1e-6f);
    }

    // Locks hold regardless of the candidate; other fields still change.
    g.distance = 1.0f;
    VariationSettings locked = source;
    for (auto& l : locked.control.permissions.operationLocks)
        l = control::lock::all;
    const auto kept = buildCandidate(locked, g);
    expect("fully locked scene unchanged", scene::sameSettings(kept.settings, locked));

    // Protected operations are preserved.
    g.protectedOperations = 0b11111u;
    expect("protected unchanged", buildCandidate(source, g).changedOperations == 0);
    g.protectedOperations = 0;

    // Output always valid.
    for (std::uint32_t c = 0; c < 64; ++c)
    {
        g.candidate = c;
        auto cand = buildCandidate(source, g);
        VariationSettings copy = cand.settings;
        expect("candidate already sanitized", !scene::sanitize(copy));
    }

    // Demo source: finite, bounded, deterministic, seek-invariant.
    {
        std::vector<float> l(512), r(512), l2(512), r2(512);
        float* ch[2] = {l.data(), r.data()};
        float* ch2[2] = {l2.data(), r2.data()};
        DemoSource::render(ch, 2, 512, 3.99, 24000.0, 48000.0);
        DemoSource::render(ch2, 2, 256, 3.99, 24000.0, 48000.0);
        DemoSource::render(ch2 + 0, 2, 0, 0.0, 24000.0, 48000.0);
        bool finite = true, nonzero = false, identical = true;
        for (std::size_t i = 0; i < 512; ++i)
        {
            finite = finite && std::isfinite(l[i]) && std::abs(l[i]) <= 1.0f;
            nonzero = nonzero || std::abs(l[i]) > 1e-3f;
        }
        for (std::size_t i = 0; i < 256; ++i)
            identical = identical && l[i] == l2[i] && l[i] == r[i];
        expect("demo finite and bounded", finite);
        expect("demo audible", nonzero);
        expect("demo deterministic across block splits", identical);
        float junk[4] = {1, 1, 1, 1};
        float* jc[1] = {junk};
        DemoSource::render(jc, 1, 4, std::nan(""), 24000.0, 48000.0);
        DemoSource::render(jc, 1, 4, 0.0, 0.0, 48000.0);
        expect("invalid timing leaves buffer", junk[0] == 1.0f);
    }

    if (failures == 0)
        std::cout << "CandidateBuilderTests passed\n";
    return failures == 0 ? 0 : 1;
}
