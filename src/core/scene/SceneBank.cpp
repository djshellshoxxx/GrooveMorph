#include "core/scene/SceneBank.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace groovemorph::core::scene
{
namespace
{
bool clampField(float& value, float lo, float hi, float fallback) noexcept
{
    const float original = value;
    if (!std::isfinite(value))
        value = fallback;
    else
        value = std::clamp(value, lo, hi);
    return !(value == original); // NaN input compares unequal -> changed
}
} // namespace

bool sanitize(OperationSettings& settings) noexcept
{
    const OperationSettings defaults{};
    bool changed = clampField(settings.depth, minDepth, maxDepth, defaults.depth);
    changed = clampField(settings.sliceBeats, minSliceBeats, maxSliceBeats, defaults.sliceBeats)
              || changed;
    return changed;
}

bool sanitize(VariationSettings& settings) noexcept
{
    bool changed = false;
    for (auto& op : settings.operations)
        changed = sanitize(op) || changed;
    return changed;
}

std::string sanitizeName(std::string_view name)
{
    std::string out(name.substr(0, maxVariationNameLength));
    for (auto& c : out)
        if (c < 0x20 || c > 0x7E)
            c = '_';
    return out;
}

bool sameSettings(const OperationSettings& a, const OperationSettings& b) noexcept
{
    return a.enabled == b.enabled && a.depth == b.depth && a.sliceBeats == b.sliceBeats;
}

bool sameSettings(const VariationSettings& a, const VariationSettings& b) noexcept
{
    for (std::size_t i = 0; i < operationCount; ++i)
        if (!sameSettings(a.operations[i], b.operations[i]))
            return false;
    return true;
}

Variation SceneBank::defaultVariation(std::size_t index)
{
    Variation v;
    auto on = [&v](OperationType t, float depth, float slice) {
        v.settings[t] = OperationSettings{true, depth, slice};
    };
    switch (index)
    {
        case 0:
            v.name = "A Repeat";
            on(OperationType::Repeat, 1.0f, 0.25f);
            break;
        case 1:
            v.name = "B Gate";
            on(OperationType::Gate, 0.8f, 0.125f);
            break;
        case 2:
            v.name = "C Reverse Half";
            on(OperationType::ReverseSlice, 1.0f, 0.5f);
            on(OperationType::HalfTime, 1.0f, 1.0f);
            break;
        default:
            v.name = "D Tape Stop";
            on(OperationType::TapeStop, 1.0f, 1.0f);
            break;
    }
    return v;
}

SceneBank::SceneBank()
{
    for (std::size_t i = 0; i < variationCount; ++i)
        variations[i] = defaultVariation(i);
}

const Variation& SceneBank::variation(std::size_t index) const noexcept
{
    return variations[std::min(index, variationCount - 1)];
}

void SceneBank::setVariation(std::size_t index, Variation v)
{
    if (index >= variationCount)
        return;
    v.name = sanitizeName(v.name);
    sanitize(v.settings);
    variations[index] = std::move(v);
}

void SceneBank::setVariationName(std::size_t index, std::string_view name)
{
    if (index < variationCount)
        variations[index].name = sanitizeName(name);
}

void SceneBank::setVariationSettings(std::size_t index, const VariationSettings& settings)
{
    if (index >= variationCount)
        return;
    variations[index].settings = settings;
    sanitize(variations[index].settings);
}

void SceneBank::setActiveIndex(std::size_t index) noexcept
{
    active = std::min(index, variationCount - 1);
}

void SceneBank::setMidiNoteMap(const MidiNoteMap& map) noexcept
{
    midiMap = map;
    for (auto& n : midiMap.notes)
        n = std::clamp(n, midiNoteMin, midiNoteMax);
    if (midiMap.noteOffPolicy != NoteOffPolicy::Ignore
        && midiMap.noteOffPolicy != NoteOffPolicy::ReturnToPrevious)
        midiMap.noteOffPolicy = NoteOffPolicy::Ignore;
}

SceneSnapshot SceneBank::snapshot() const noexcept
{
    SceneSnapshot s;
    for (std::size_t i = 0; i < variationCount; ++i)
        s.variations[i] = variations[i].settings;
    s.activeIndex = active;
    s.seed = seedValue;
    s.midiMap = midiMap;
    return s;
}
} // namespace groovemorph::core::scene
