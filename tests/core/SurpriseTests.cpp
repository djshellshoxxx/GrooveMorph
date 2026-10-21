// N05 acceptance tests S1-S2 (docs/specs/N05-EASY-MODE.md).
#include "core/control/SceneControl.h"
#include "core/control/VariationBlend.h"
#include "core/scene/SceneBank.h"
#include "core/variation/Surprise.h"

#include <iostream>
#include <set>
#include <string_view>

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
    const scene::SceneBank bank;
    // N05-S1: deterministic, audible, different from the stored scene; repeat presses differ.
    for (std::size_t index = 0; index < variationCount; ++index)
    {
        const auto& stored = bank.variation(index).settings;
        const auto a = buildSurprise(stored, bank.seed(), 0);
        const auto b = buildSurprise(stored, bank.seed(), 0);
        expect("deterministic", a.ok && b.ok && scene::sameSettings(a.settings, b.settings) && a.candidate == b.candidate);
        expect("different from the stored scene", a.ok && !scene::sameSettings(a.settings, stored) && a.changedOperations != 0);
        expect("audible", a.ok && hasAudibleEffect(a.settings));
        expect("sanitized", a.ok && [&] { auto c = a.settings; return !scene::sanitize(c); }());
    }
    {
        const auto& stored = bank.variation(0).settings;
        std::set<std::uint32_t> candidates;
        bool distinct = false;
        auto first = buildSurprise(stored, bank.seed(), 0);
        for (std::uint32_t attempt = 1; attempt < 8; ++attempt)
        {
            const auto r = buildSurprise(stored, bank.seed(), first.candidate + attempt);
            candidates.insert(r.candidate);
            distinct = distinct || (r.ok && !scene::sameSettings(r.settings, first.settings));
        }
        expect("later presses give different results", distinct);
        expect("candidate numbers advance", candidates.size() > 1);
    }
    {
        // The result of buildSurprise is never an all-off scene: a source with one quiet effect never goes silent.
        VariationSettings quiet;
        quiet[OperationType::Gate] = {true, 0.5f, 0.25f};
        bool everAudible = true;
        for (std::uint64_t seed = 1; seed <= 200; ++seed)
        {
            const auto r = buildSurprise(quiet, seed, 0);
            everAudible = everAudible && (!r.ok || hasAudibleEffect(r.settings));
        }
        expect("never offers a scene without an audible effect", everAudible);
        VariationSettings silent; // nothing enabled
        expect("hasAudibleEffect false for all-off", !hasAudibleEffect(silent));
        VariationSettings faint;
        faint[OperationType::Gate] = {true, 0.05f, 0.25f};
        expect("hasAudibleEffect false below the depth floor", !hasAudibleEffect(faint));
    }

    // N05-S2: locks respected, fully locked scene yields ok = false and the scene back unchanged.
    {
        auto stored = bank.variation(1).settings;
        for (auto& l : stored.control.permissions.operationLocks)
            l = control::lock::all;
        const auto before = stored;
        const auto r = buildSurprise(stored, bank.seed(), 0);
        expect("fully locked: not ok", !r.ok);
        expect("fully locked: scene returned unchanged", scene::sameSettings(r.settings, before));
        expect("input untouched", scene::sameSettings(stored, before));

        auto partial = bank.variation(0).settings;
        partial.control.permissions.operationLocks[0] = control::lock::all;
        bool lockedKept = true;
        for (std::uint32_t attempt = 0; attempt < 64; ++attempt)
        {
            const auto p = buildSurprise(partial, bank.seed(), attempt);
            if (p.ok)
                lockedKept = lockedKept && scene::sameSettings(p.settings.operations[0], partial.operations[0]);
        }
        expect("a locked operation is never changed", lockedKept);
    }

    if (failures == 0)
        std::cout << "SurpriseTests: all checks passed\n";
    return failures == 0 ? 0 : 1;
}
