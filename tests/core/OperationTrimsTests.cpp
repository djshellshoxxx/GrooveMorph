#include "core/control/OperationTrims.h"
#include "core/scene/SceneBank.h"

#include <cmath>
#include <cstring>
#include <iostream>
#include <limits>
#include <set>
#include <string>
#include <string_view>

using namespace groovemorph::core;
using namespace groovemorph::core::control;

namespace {
int failures = 0;
void expect(std::string_view name, bool condition) {
    if (!condition) { std::cerr << name << ": check failed\n"; ++failures; }
}
} // namespace

int main() {
    // A06-01: ids are stable, readable and generated for every operation, including indices 5 and 6.
    expect("A06-01 op0", operationTrimId(0, TrimKind::Enable) == "op0Enable" && operationTrimId(0, TrimKind::Depth) == "op0Depth"
                          && operationTrimId(0, TrimKind::Slice) == "op0Slice");
    expect("A06-01 op5", operationTrimId(5, TrimKind::Depth) == "op5Depth" && operationTrimId(5, TrimKind::Enable) == "op5Enable"
                          && operationTrimId(5, TrimKind::Slice) == "op5Slice");
    expect("A06-01 op6", operationTrimId(6, TrimKind::Slice) == "op6Slice");
    expect("A06-01 two digit index is extensible", operationTrimId(12, TrimKind::Depth) == "op12Depth");
    {
        std::set<std::string> seen;
        bool unique = true;
        for (std::size_t op = 0; op < 16; ++op)
            for (std::size_t k = 0; k < trimKindCount; ++k)
                unique = seen.insert(operationTrimId(op, static_cast<TrimKind>(k))).second && unique;
        expect("A06-01 ids unique over 16 operations", unique && seen.size() == 16 * trimKindCount);
        // Prefix-free: op1 + "2Depth" can never collide with op12 because kinds are fixed words.
        expect("A06-01 no collision op1/op12", operationTrimId(1, TrimKind::Depth) != operationTrimId(12, TrimKind::Depth));
    }
    // Existing host parameter ids must never be shadowed by a trim id.
    for (const char* legacy : {"masterIntensity", "dryWet", "manualTempo", "phraseBars", "activeVariation", "bypass",
                               "morphTarget", "morphAmount", "bandMode", "audioTrigger"})
        for (std::size_t op = 0; op < operationCount + 2; ++op)
            for (std::size_t k = 0; k < trimKindCount; ++k)
                expect("A06-01 no clash with legacy ids", operationTrimId(op, static_cast<TrimKind>(k)) != legacy);
    expect("A06-01 names", !operationTrimName(0, TrimKind::Depth).empty() && operationTrimName(5, TrimKind::Slice).find("Slice") != std::string::npos
                            && operationDisplayName(40) == "Effect 41");

    // A06-02: default trims are the identity and leave the settings bit-identical.
    {
        VariationSettings settings;
        for (std::size_t i = 0; i < operationCount; ++i)
            settings.operations[i] = OperationSettings{(i % 2) == 0, 0.1f + 0.13f * static_cast<float>(i), 0.0625f * static_cast<float>(i + 1)};
        const auto original = settings;
        OperationTrims trims{};
        expect("A06-02 identity", isIdentity(trims));
        applyTrims(settings, trims);
        expect("A06-02 bit identical", std::memcmp(&settings.operations, &original.operations, sizeof(settings.operations)) == 0);
        // Values a host round trip can produce within an ulp or two are still the default.
        trims[0].depth = std::nextafter(1.0f, 0.0f);
        trims[1].slice = 1.0f + 1.0e-6f;
        expect("A06-02 near default is identity", isIdentity(trims));
        applyTrims(settings, trims);
        expect("A06-02 near default is bit identical", std::memcmp(&settings.operations, &original.operations, sizeof(settings.operations)) == 0);
        // Non-finite host values are the default, not poison.
        trims[2].depth = std::numeric_limits<float>::quiet_NaN();
        trims[3].slice = std::numeric_limits<float>::infinity();
        applyTrims(settings, trims);
        expect("A06-02 non-finite ignored", std::memcmp(&settings.operations, &original.operations, sizeof(settings.operations)) == 0);
    }

    // A06-03: each trim changes only its own operation and field, and clamps to the scene domains.
    {
        VariationSettings base;
        for (auto& op : base.operations)
            op = OperationSettings{true, 0.8f, 0.5f};
        OperationTrims trims{};
        trims[1].depth = 0.5f;
        auto out = base;
        applyTrims(out, trims);
        expect("A06-03 depth multiplies", std::abs(out.operations[1].depth - 0.4f) < 1e-6f && out.operations[1].enabled
                                           && out.operations[1].sliceBeats == 0.5f);
        expect("A06-03 other rows untouched", out.operations[0].depth == 0.8f && out.operations[2].depth == 0.8f);
        trims = OperationTrims{};
        trims[0].slice = 4.0f;
        out = base;
        applyTrims(out, trims);
        expect("A06-03 slice multiplies", out.operations[0].sliceBeats == 2.0f);
        VariationSettings wide = base;
        wide.operations[0].sliceBeats = 3.0f;
        out = wide;
        applyTrims(out, trims);
        expect("A06-03 slice clamps to 4 beats", out.operations[0].sliceBeats == 4.0f);
        trims[0].slice = 0.25f;
        out = base;
        applyTrims(out, trims);
        expect("A06-03 slice shrinks", std::abs(out.operations[0].sliceBeats - 0.125f) < 1e-6f);
        VariationSettings tiny = base;
        tiny.operations[0].sliceBeats = 0.0625f;
        out = tiny;
        applyTrims(out, trims);
        expect("A06-03 slice clamps to 1/16", out.operations[0].sliceBeats == 0.0625f);
        trims = OperationTrims{};
        trims[2].enable = EnableTrim::ForceOff;
        out = base;
        applyTrims(out, trims);
        expect("A06-03 force off", !out.operations[2].enabled && out.operations[2].depth == 0.8f);
        VariationSettings off;
        trims = OperationTrims{};
        trims[3].enable = EnableTrim::ForceOn;
        applyTrims(off, trims);
        expect("A06-03 force on", off.operations[3].enabled && !off.operations[0].enabled);
        trims = OperationTrims{};
        trims[0].depth = 7.0f; // out of range host value is clamped, never amplifies
        out = base;
        applyTrims(out, trims);
        expect("A06-03 depth never amplifies", out.operations[0].depth <= 0.8f);
    }

    // A06-04: the overlay never touches the control block (placement, lane, permissions), only operation rows.
    {
        VariationSettings base;
        base.control.placement.enabled = true;
        base.control.placement.every = 3;
        base.control.lane.explicitEvents = true;
        base.control.permissions.operationLocks[0] = 15;
        const auto copy = base;
        OperationTrims trims{};
        for (auto& t : trims)
            t = OperationTrim{EnableTrim::ForceOn, 0.5f, 2.0f};
        applyTrims(base, trims);
        expect("A06-04 control block untouched", control::sameControl(base.control, copy.control)
                                                  && base.control.placement.every == 3 && base.control.lane.explicitEvents
                                                  && base.control.permissions.operationLocks[0] == 15);
    }

    // A06-05: operation index 5 and 6 exist whenever the build has that many operations (operationCount-driven).
    for (std::size_t op = 0; op < operationCount; ++op)
        expect("A06-05 every operation has an id", !operationTrimId(op, TrimKind::Depth).empty());
    expect("A06-05 trim array covers operationCount", OperationTrims{}.size() == operationCount);

    if (failures == 0) std::cout << "OperationTrimsTests: all checks passed\n";
    return failures == 0 ? 0 : 1;
}
