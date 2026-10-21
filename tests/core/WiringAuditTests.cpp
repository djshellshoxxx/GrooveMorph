// A2 wiring audit: caps that must agree with each other, and operationCount-wide scene round trips.
#include "core/control/LiveOverlay.h"
#include "core/control/SceneControl.h"
#include "core/dsp/LoopBlob.h"
#include "core/preset/FactoryPresets.h"
#include "core/preset/Preset.h"
#include "core/scene/SceneSerializer.h"

#include <iostream>
#include <string>
#include <string_view>

using namespace groovemorph::core;
using groovemorph::core::scene::SceneBank;
using groovemorph::core::scene::SceneSerializer;

namespace
{
int failures = 0;
void expect(std::string_view name, bool condition)
{
    if (!condition)
    {
        std::cerr << name << ": check failed\n";
        ++failures;
    }
}

// Every field at its widest textual value, every lane at capacity, every operation in use.
SceneBank worstCaseBank()
{
    SceneBank bank;
    for (std::size_t v = 0; v < variationCount; ++v)
    {
        auto s = bank.variation(v).settings;
        for (std::size_t op = 0; op < operationCount; ++op)
            s.operations[op] = {true, 0.123456789f, 0.0625f};
        auto& c = s.control;
        c.placement.enabled = true;
        c.placement.regionCount = static_cast<std::uint8_t>(control::maxProtectedRegions);
        c.lane.explicitEvents = true;
        c.lane.count = control::maxLaneEvents;
        for (std::size_t k = 0; k < control::maxLaneEvents; ++k)
        {
            auto& e = c.lane.events[k];
            e.operation = static_cast<std::uint8_t>(k % operationCount);
            e.onsetTicks = 1000000 + static_cast<std::int32_t>(k);
            e.durationTicks = 1000000;
            e.depth = 0.123456789f;
            e.sliceBeats = 0.3333333f;
            e.chance = 55;
            e.priority = 77;
        }
        for (std::size_t op = 0; op < operationCount; ++op)
        {
            c.permissions.operationLocks[op] = 15;
            c.lane.loopBeats[op] = 64;
            c.lane.chanceByOp[op] = 55;
        }
        c.lane.chanceSeed = 0xffffffffu;
        bank.setVariationSettings(v, s);
    }
    return bank;
}
} // namespace

int main()
{
    // S1: the largest possible scene text fits in the plugin-state headroom that sits beside the largest loop blob.
    // The values above are written straight into the structs (the editor never allows overlapping events), so this
    // is a size bound, not a playable scene.
    {
        const auto text = SceneSerializer::serialize(worstCaseBank());
        const std::size_t headroom = LoopBlob::maxStateBytes - (LoopBlob::maxEncodedBytes / 3 * 4 + 16);
        std::cout << "worst-case scene text: " << text.size() << " bytes; state headroom " << headroom
                  << "; preset file cap " << preset::maxFileBytes << "\n";
        expect("S1 scene text fits state headroom (leaving 64 KiB for parameters, gesture, MIDI links)",
               text.size() + 64 * 1024 <= headroom);
        // S2: a preset made from the largest bank must be loadable again (save and load share one cap).
        const auto p = preset::makePreset("Worst", "Stutter", {}, worstCaseBank());
        const auto file = preset::serializePreset(p);
        expect("S2 largest preset file is under the file cap", file.size() <= preset::maxFileBytes);
    }
    // O1..O4: live overlay order (Chaos, then trims) over all operationCount operations.
    {
        VariationSettings base;
        control::ChaosContext chaos;
        chaos.chaos = 1.0f;
        chaos.seed = 42;
        chaos.sceneIndex = 1;
        chaos.phraseTicks = 4 * 4 * control::ticksPerBeat;
        control::OperationTrims trims{};
        const auto equal = [](const VariationSettings& a, const VariationSettings& b) {
            for (std::size_t i = 0; i < operationCount; ++i)
                if (a.operations[i].enabled != b.operations[i].enabled || a.operations[i].depth != b.operations[i].depth
                    || a.operations[i].sliceBeats != b.operations[i].sliceBeats)
                    return false;
            return true;
        };
        auto none = base;
        control::applyLiveOverlays(none, &trims, nullptr);
        expect("O1 default trims and no chaos are the identity", equal(none, base));
        auto chaotic = base;
        control::applyLiveOverlays(chaotic, nullptr, &chaos);
        bool everyOpOn = true;
        for (std::size_t i = 0; i < operationCount; ++i)
            everyOpOn = everyOpOn && chaotic.operations[i].enabled;
        expect("O2 chaos 1 reaches all seven operations, including Filter sweep and Bit crush", everyOpOn);
        auto both = base;
        trims[static_cast<std::size_t>(OperationType::BitCrush)].enable = control::EnableTrim::ForceOff;
        trims[static_cast<std::size_t>(OperationType::Gate)].depth = 0.5f;
        control::applyLiveOverlays(both, &trims, &chaos);
        expect("O3 a trim set to Off wins over chaos", !both.operations[static_cast<std::size_t>(OperationType::BitCrush)].enabled);
        expect("O4 the depth trim scales the depth chaos produced",
               both.operations[static_cast<std::size_t>(OperationType::Gate)].depth
                   == chaotic.operations[static_cast<std::size_t>(OperationType::Gate)].depth * 0.5f);
        auto again = base;
        control::applyLiveOverlays(again, &trims, &chaos);
        expect("O5 deterministic", equal(again, both));
    }
    // P1: the crossover topology travels with a preset like the other band parameters.
    {
        const auto p = preset::makePreset("Band", "Stutter", {}, SceneBank{}, "", {{"bandTopology", 1.0f}, {"bandSlope", 0.0f}});
        const auto parsed = preset::parsePreset(preset::serializePreset(p));
        bool found = false;
        for (const auto& param : parsed.preset.params)
            found = found || (param.id == "bandTopology" && param.value == 1.0f);
        expect("P1 bandTopology survives a preset round trip", parsed.ok && found);
    }
    std::cout << (failures == 0 ? "WiringAuditTests passed\n" : "WiringAuditTests FAILED\n");
    return failures == 0 ? 0 : 1;
}
