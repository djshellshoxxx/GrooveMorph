// A2 wiring audit: caps that must agree with each other, and operationCount-wide scene round trips.
#include "core/control/LiveOverlay.h"
#include "core/control/SceneControl.h"
#include "core/dsp/LoopBlob.h"
#include "core/preset/FactoryPresets.h"
#include "core/preset/Preset.h"
#include "core/scene/SceneSerializer.h"
#include "core/trace/TraceFrame.h"
#include "core/viz/Palette.h"

#include <iostream>
#include <string>
#include <string_view>

using namespace groovemorph::core;
using groovemorph::core::scene::SceneBank;
using groovemorph::core::scene::SceneSerializer;

// Constants that mirror the operation count in other layers must follow it.
static_assert(trace::stageCount == operationCount, "trace frames carry one amount per operation");
static_assert(control::laneCount == operationCount, "one lane per operation");

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
    // R1..R3: every scene key survives serialize -> deserialize -> serialize for a scene that uses all seven operations.
    {
        SceneBank bank;
        const std::int64_t phraseTicks = 4 * 4 * control::ticksPerBeat;
        for (std::size_t v = 0; v < variationCount; ++v)
        {
            auto s = bank.variation(v).settings;
            auto& c = s.control;
            c.placement.enabled = true;
            c.placement.regionCount = 2;
            c.placement.regions[0] = {960, 960};
            c.placement.regions[1] = {4800, 480};
            c.phase.smoothingMs = 12.5f;
            c.lane.explicitEvents = true;
            for (std::size_t op = 0; op < operationCount; ++op)
            {
                s.operations[op] = {op % 2 == 0, 0.25f + 0.1f * static_cast<float>(op), 0.0625f * static_cast<float>(op + 1)};
                control::LaneEvent e;
                e.operation = static_cast<std::uint8_t>(op);
                e.onsetTicks = static_cast<std::int32_t>(op * 1920 + v * 120);
                e.durationTicks = 480;
                e.depth = 0.5f;
                e.chance = static_cast<std::uint8_t>(40 + op);
                expect("R0 event added", control::addEvent(c.lane, e, phraseTicks) == control::EditStatus::Ok);
                c.permissions.operationLocks[op] = static_cast<std::uint8_t>(op % 16);
                c.activity.priority[op] = static_cast<std::uint8_t>(10 + op);
                c.lane.loopBeats[op] = static_cast<std::uint8_t>(op);
                c.lane.chanceByOp[op] = static_cast<std::uint8_t>(90 - op);
            }
            c.lane.chanceSeed = 0xdeadbeefu + static_cast<std::uint32_t>(v);
            bank.setVariationSettings(v, s);
        }
        const auto text = SceneSerializer::serialize(bank);
        const auto back = SceneSerializer::deserialize(text);
        expect("R1 loads without recovery notes", back.report.status == groovemorph::core::scene::LoadStatus::Ok);
        bool same = true;
        for (std::size_t v = 0; v < variationCount; ++v)
            same = same && groovemorph::core::scene::sameSettings(bank.variation(v).settings, back.bank.variation(v).settings);
        expect("R2 every field of all seven operations and lanes round-trips", same);
        expect("R3 serialization is a fixed point", SceneSerializer::serialize(back.bank) == text);
    }
    // H1..H3: hostile operation / event indices and numbers never crash, never grow the report, never escape the arrays.
    {
        std::string hostile = "groovemorph-scenes v1\n";
        for (const char* key : {"variation.0.op.bogus.enabled=1", "variation.0.perm.op.7=3", "variation.0.perm.op.-1=3",
                                "variation.0.activity.prio.9=1", "variation.0.lane.loop.7=5", "variation.0.lane.chance.99=5",
                                "variation.0.event.256.op=1", "variation.0.event.65536.op=1", "variation.0.event.0.op=200",
                                "variation.0.event.0.chance=9999", "variation.0.lane.seed=99999999999999999999",
                                "variation.0.phase.smooth=nan", "variation.0.phase.smooth=1e99", "variation.9.name=x",
                                "variation.0.region.8.start=1", "variation.0.placement.regions=255"})
            hostile += std::string(key) + "\n";
        for (int i = 0; i < 5000; ++i)
            hostile += "variation.0.event." + std::to_string(i) + ".duration=-1\n";
        const auto r = SceneSerializer::deserialize(hostile);
        expect("H1 hostile text loads", r.report.status != groovemorph::core::scene::LoadStatus::Defaulted || !r.report.messages.empty());
        expect("H2 recovery report stays bounded", r.report.messages.size() <= 70);
        const auto& c = r.bank.variation(0).settings.control;
        expect("H3 values clamped", c.lane.count <= control::maxLaneEvents && c.placement.regionCount <= control::maxProtectedRegions
                                      && c.lane.chanceByOp[0] <= 100);
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
