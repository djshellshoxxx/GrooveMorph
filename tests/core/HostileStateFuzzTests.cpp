// A3 hostile-state fuzzing at the portable core level (docs/qa/RT-AUDIT-BETA.md section 4).
//
// Mutated / truncated scene text, .gmpreset files, loop blobs, gesture and MIDI-link text, event keys with
// huge numbers and the lane edit commands with extreme integers. Deterministic (fixed seeds), so a failure
// reproduces. Pass criteria: no crash, no sanitizer report (run under ASan+UBSan and TSan), results stay
// inside the documented domains, parse time and single allocations stay bounded, and a recovered scene is
// safe to feed to the audio engine.
//
// GM_FUZZ_ITERS=<n> scales the number of mutated inputs per target (default 400; the sanitizer audit uses 4000);
// GM_FUZZ_SEED=<n> selects another deterministic stream (default 0).
#include "core/MidiLinks.h"
#include "core/control/EuclidPattern.h"
#include "core/control/GestureLane.h"
#include "core/control/PhraseFit.h"
#include "core/control/PlacementEvaluator.h"
#include "core/control/SceneControl.h"
#include "core/control/VariationBlend.h"
#include "core/dsp/LoopBlob.h"
#include "core/dsp/Crossover.h"
#include "core/dsp/LoopImport.h"
#include "core/dsp/LoopSource.h"
#include "core/dsp/OutputCapture.h"
#include "core/dsp/RhythmEngine.h"
#include "core/preset/FactoryPresets.h"
#include "core/preset/Preset.h"
#include "core/scene/SceneBank.h"
#include "core/scene/SceneSerializer.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <new>
#include <string>
#include <string_view>
#include <vector>

namespace
{
// Largest single allocation request seen since the last reset (bounded-memory check, portable).
std::atomic<std::size_t> g_largestRequest{0};
std::atomic<std::size_t> g_totalRequested{0};

void* trackedAlloc(std::size_t n)
{
    std::size_t seen = g_largestRequest.load(std::memory_order_relaxed);
    while (n > seen && !g_largestRequest.compare_exchange_weak(seen, n, std::memory_order_relaxed))
    {
    }
    g_totalRequested.fetch_add(n, std::memory_order_relaxed);
    void* p = std::malloc(n != 0 ? n : 1);
    if (p == nullptr)
        throw std::bad_alloc();
    return p;
}
} // namespace

void* operator new(std::size_t n) { return trackedAlloc(n); }
void* operator new[](std::size_t n) { return trackedAlloc(n); }
void* operator new(std::size_t n, const std::nothrow_t&) noexcept
{
    try { return trackedAlloc(n); } catch (...) { return nullptr; }
}
void* operator new[](std::size_t n, const std::nothrow_t&) noexcept
{
    try { return trackedAlloc(n); } catch (...) { return nullptr; }
}
void operator delete(void* p, const std::nothrow_t&) noexcept { std::free(p); }
void operator delete[](void* p, const std::nothrow_t&) noexcept { std::free(p); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }

using namespace groovemorph::core;
using namespace groovemorph::core::control;

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

std::uint64_t seedOffset()
{
    if (const char* v = std::getenv("GM_FUZZ_SEED"))
        return static_cast<std::uint64_t>(std::strtoull(v, nullptr, 10)) * 0x9e3779b97f4a7c15ull;
    return 0;
}

struct Rng
{
    std::uint64_t s;
    explicit Rng(std::uint64_t seed) : s((seed ^ seedOffset()) ? (seed ^ seedOffset()) : 1) {}
    std::uint64_t next() noexcept
    {
        s ^= s << 13;
        s ^= s >> 7;
        s ^= s << 17;
        return s;
    }
    std::size_t below(std::size_t n) noexcept { return n == 0 ? 0 : static_cast<std::size_t>(next() % n); }
};

int iterations()
{
    if (const char* v = std::getenv("GM_FUZZ_ITERS"))
    {
        const int n = std::atoi(v);
        if (n > 0)
            return n;
    }
    return 400;
}

const char* const extremeTokens[] = {
    "99999999999999999999999", "-99999999999999999999", "1e999", "-1e999", "1e-999", "nan", "NaN", "inf", "-inf",
    "0x7fffffff", "2147483647", "2147483648", "-2147483648", "-2147483649", "4294967295", "4294967296",
    "18446744073709551615", "18446744073709551616", "9223372036854775807", "-9223372036854775808",
    "340282346638528859811704183484516925440", "1.0e38", "1e5000", "1e-5000", ".", "-", "-.", "1.5.5", "1e", "1e+",
    "0", "-0", "", "00000000000000000000000001", "255", "256", "65535", "65536", "1966080", "1966081", "100", "101",
    "0.0000000001", "1e30", "-1e30", "1e10", "7", "8", "9"};

std::string pickToken(Rng& rng)
{
    return extremeTokens[rng.below(sizeof(extremeTokens) / sizeof(extremeTokens[0]))];
}

bool numberChar(char c) noexcept
{
    return (c >= '0' && c <= '9') || c == '.' || c == '-' || c == '+' || c == 'e' || c == 'E';
}

// One structural or numeric mutation of `text`.
void mutateOnce(std::string& text, Rng& rng)
{
    if (text.empty())
    {
        text = pickToken(rng);
        return;
    }
    switch (rng.below(11))
    {
        case 0: // flip a byte
            text[rng.below(text.size())] = static_cast<char>(rng.next());
            break;
        case 1: // delete a range
        {
            const auto a = rng.below(text.size());
            text.erase(a, rng.below(std::min<std::size_t>(64, text.size() - a)) + 1);
            break;
        }
        case 2: // duplicate a range
        {
            const auto a = rng.below(text.size());
            const auto n = rng.below(std::min<std::size_t>(200, text.size() - a)) + 1;
            text.insert(rng.below(text.size()), text.substr(a, n));
            break;
        }
        case 3: // insert an extreme token
            text.insert(rng.below(text.size() + 1), pickToken(rng));
            break;
        case 4: // truncate
            text.resize(rng.below(text.size()));
            break;
        case 5: // replace a numeric token with an extreme one
        {
            for (int attempt = 0; attempt < 8; ++attempt)
            {
                auto a = rng.below(text.size());
                if (!numberChar(text[a]))
                    continue;
                auto b = a;
                while (b < text.size() && numberChar(text[b]))
                    ++b;
                text.replace(a, b - a, pickToken(rng));
                break;
            }
            break;
        }
        case 6: // embedded NUL / control bytes
            text.insert(rng.below(text.size() + 1), std::string(1 + rng.below(3), static_cast<char>(rng.below(32))));
            break;
        case 7: // repeat one line many times
        {
            const auto a = text.find('\n', rng.below(text.size()));
            if (a == std::string::npos)
                break;
            const auto start = text.rfind('\n', a == 0 ? 0 : a - 1);
            const auto line = text.substr(start == std::string::npos ? 0 : start, a - (start == std::string::npos ? 0 : start) + 1);
            std::string many;
            for (int i = 0; i < 300 && many.size() < 40000; ++i)
                many += line;
            text.insert(a + 1, many);
            break;
        }
        case 8: // very long line without a newline
            text.insert(rng.below(text.size() + 1), std::string(1000 + rng.below(60000), static_cast<char>('a' + rng.below(26))));
            break;
        case 9: // swap two halves
        {
            const auto m = rng.below(text.size());
            text = text.substr(m) + text.substr(0, m);
            break;
        }
        default: // event / region key with a huge index or value
        {
            const char* keys[] = {"variation.0.event.", "variation.1.event.", "variation.3.event.", "variation.2.region."};
            text += "\n";
            text += keys[rng.below(4)];
            text += pickToken(rng);
            text += ".";
            const char* fields[] = {"op", "onset", "duration", "depth", "slice", "priority", "chance", "locked", "start", "length"};
            text += fields[rng.below(10)];
            text += "=";
            text += pickToken(rng);
            text += "\n";
            break;
        }
    }
}

std::string mutate(const std::string& seed, Rng& rng)
{
    std::string text = seed;
    const int steps = 1 + static_cast<int>(rng.below(4));
    for (int i = 0; i < steps; ++i)
        mutateOnce(text, rng);
    return text;
}

// A scene bank with every optional feature populated, so the mutator hits real keys.
scene::SceneBank richBank()
{
    scene::SceneBank bank;
    for (std::size_t v = 0; v < variationCount; ++v)
    {
        auto variation = scene::SceneBank::defaultVariation(v);
        auto& s = variation.settings;
        for (std::size_t op = 0; op < operationCount; ++op)
            s.operations[op] = {true, 0.3f + 0.1f * static_cast<float>(op), 0.125f * static_cast<float>(1 + op % 4)};
        auto& c = s.control;
        c.placement.enabled = true;
        c.placement.window = WindowMode::Custom;
        c.placement.startPhase = 0.25f;
        c.placement.spanPhase = 0.5f;
        c.placement.every = 3;
        c.placement.cadenceOffset = 1;
        c.placement.originTicks = -480;
        c.placement.downbeatProtectBeats = 0.5f;
        c.placement.regionCount = 3;
        for (std::size_t r = 0; r < 3; ++r)
            c.placement.regions[r] = {static_cast<std::int32_t>(1000 * (r + 1)), 240};
        c.phase.policy = PhasePolicy::AlignBeat;
        c.phase.offsetBeats = 1.5f;
        c.phase.smoothingMs = 12.0f;
        c.lane.explicitEvents = true;
        for (std::size_t op = 0; op < laneCount; ++op)
        {
            c.lane.chanceByOp[op] = static_cast<std::uint8_t>(60 + op);
            for (int k = 0; k < 6; ++k)
            {
                LaneEvent e;
                e.operation = static_cast<std::uint8_t>(op);
                e.onsetTicks = k * 2000 + static_cast<int>(v) * 11;
                e.durationTicks = 600;
                e.depth = 0.5f + 0.05f * static_cast<float>(k);
                e.sliceBeats = (k % 2) ? 0.25f : 0.0f;
                e.priority = static_cast<std::uint8_t>(40 + k);
                e.chance = static_cast<std::uint8_t>(100 - k * 7);
                e.locked = (k == 2);
                (void) addEvent(c.lane, e, 16 * ticksPerBeat);
            }
        }
        c.lane.loopBeats[1] = 6;
        c.lane.chanceSeed = 4242;
        c.permissions.operationLocks[2] = lock::depth | lock::events;
        c.permissions.placementLocked = (v == 1);
        c.activity.enabled = true;
        c.activity.maxConcurrent = 3;
        c.activity.minSpacingBeats = 0.25f;
        c.activity.maxCoverage = 0.8f;
        bank.setVariation(v, variation);
    }
    scene::MidiNoteMap map;
    map.notes = {36, 37, 38, 39};
    map.noteOffPolicy = scene::NoteOffPolicy::ReturnToPrevious;
    bank.setMidiNoteMap(map);
    bank.setSeed(0xfeedfaceull);
    bank.setActiveIndex(2);
    return bank;
}

using Clock = std::chrono::steady_clock;
double secondsSince(Clock::time_point t) { return std::chrono::duration<double>(Clock::now() - t).count(); }

// Everything the audio thread could do with a recovered bank must stay finite and bounded.
void exerciseBank(const scene::SceneBank& bank)
{
    static RhythmEngine engine;
    static std::vector<float> l(256), r(256);
    static bool ready = false;
    if (!ready)
    {
        engine.prepare(48000.0, 256, 2);
        ready = true;
    }
    const auto snapshot = bank.snapshot();
    GridContext grid;
    RhythmTiming timing;
    timing.samplesPerBeat = 22050.0;
    timing.isPlaying = true;
    for (std::size_t v = 0; v < variationCount; ++v)
    {
        const auto& settings = snapshot.variations[v];
        std::uint32_t mask = 0;
        for (std::size_t op = 0; op < operationCount; ++op)
            mask |= settings.operations[op].enabled ? (1u << op) : 0u;
        for (double pos : {0.0, 1.37, 15.999, 16.0, 1.0e6, -3.5})
        {
            const auto t = evaluate(settings.control, grid, pos, mask, pos);
            if (!(t.ticksUntilChange > 0.0) || std::isnan(t.ticksUntilChange))
            {
                expect("evaluate ticksUntilChange positive for a recovered scene", false);
                return;
            }
        }
        const auto m = morph(settings, snapshot.variations[(v + 1) % variationCount], 0.5f, 16 * ticksPerBeat);
        expect("morph of recovered scenes is sanitary", m.control.lane.count <= maxLaneEvents);
        for (int b = 0; b < 2; ++b)
        {
            for (std::size_t i = 0; i < l.size(); ++i)
                l[i] = r[i] = std::sin(static_cast<float>(i) * 0.05f);
            float* ch[2] = {l.data(), r.data()};
            timing.beatAtBlockStart = 3.0 + 0.5 * b;
            engine.process(ch, 2, 256, settings, 1.0f, timing);
            for (std::size_t i = 0; i < l.size(); ++i)
                if (!std::isfinite(l[i]) || !std::isfinite(r[i]))
                {
                    expect("engine output finite for a recovered scene", false);
                    return;
                }
        }
    }
}

void checkBankSanitary(const scene::SceneBank& bank, const char* what)
{
    expect(std::string(what) + ": active index in range", bank.activeIndex() < variationCount);
    for (std::size_t v = 0; v < variationCount; ++v)
    {
        auto copy = bank.variation(v).settings;
        const bool changed = scene::sanitize(copy);
        if (changed)
            std::cerr << what << ": variation " << v << " is not already sanitized\n";
        expect(std::string(what) + ": settings already sanitized", !changed);
        expect(std::string(what) + ": name bounded", bank.variation(v).name.size() <= scene::maxVariationNameLength);
        for (const char ch : bank.variation(v).name)
            expect(std::string(what) + ": name printable", ch >= 0x20 && ch <= 0x7e);
        expect(std::string(what) + ": lane count bounded", bank.variation(v).settings.control.lane.count <= maxLaneEvents);
    }
}

void fuzzScenes()
{
    const auto rich = richBank();
    std::vector<std::string> seeds{scene::SceneSerializer::serialize(scene::SceneBank{}),
                                   scene::SceneSerializer::serialize(rich)};
    for (const auto& p : preset::factoryPresets())
        if (seeds.size() < 8)
            seeds.push_back(p.sceneText);
    expect("rich bank round-trips exactly", scene::SceneSerializer::deserialize(seeds[1]).bank == rich);

    Rng rng(0xa3a3a3a3ull);
    const int n = iterations();
    double worst = 0.0;
    std::size_t recovered = 0, defaulted = 0, ok = 0;
    g_largestRequest = 0;
    for (int i = 0; i < n; ++i)
    {
        const auto& seed = seeds[rng.below(seeds.size())];
        std::string text = mutate(seed, rng);
        const auto start = Clock::now();
        const auto result = scene::SceneSerializer::deserialize(text);
        worst = std::max(worst, secondsSince(start));
        switch (result.report.status)
        {
            case scene::LoadStatus::Ok: ++ok; break;
            case scene::LoadStatus::Recovered: ++recovered; break;
            case scene::LoadStatus::Defaulted: ++defaulted; break;
        }
        expect("recovery notes bounded", result.report.messages.size() <= 80);
        checkBankSanitary(result.bank, "fuzz scene");
        // Idempotence: what we recovered must serialize and reload to the same bank.
        const auto again = scene::SceneSerializer::deserialize(scene::SceneSerializer::serialize(result.bank));
        if (!(again.bank == result.bank))
        {
            std::cerr << "iteration " << i << ": recovered bank does not round-trip\n";
            expect("recovered bank round-trips", false);
        }
        if (i % 7 == 0)
            exerciseBank(result.bank);
        if (failures > 20)
            break;
    }
    // Every prefix of a valid file (truncation anywhere).
    for (std::size_t cut = 0; cut < seeds[1].size(); cut += 1 + seeds[1].size() / 700)
    {
        const auto result = scene::SceneSerializer::deserialize(std::string_view(seeds[1]).substr(0, cut));
        checkBankSanitary(result.bank, "truncated scene");
    }
    std::cout << "scene fuzz: " << n << " inputs (ok " << ok << ", recovered " << recovered << ", defaulted " << defaulted
              << "), slowest parse " << worst << " s, largest allocation " << g_largestRequest.load() << " bytes\n";
    expect("scene parse time bounded (< 5 s each)", worst < 5.0);
    expect("scene parse allocations bounded (< 64 MiB each)", g_largestRequest.load() < (64u << 20));
    expect("fuzz exercised all outcomes", ok + recovered + defaulted == static_cast<std::size_t>(n));
}

void fuzzHugeEventKeys()
{
    // Event slots far beyond capacity, huge numbers in every field, and thousands of distinct slots.
    std::string text = "groovemorph-scenes v1\n";
    for (int v = 0; v < 4; ++v)
        for (int k = 0; k < 600; ++k)
            for (const char* f : {"op", "onset", "duration", "depth", "slice", "priority", "chance", "locked", "restart"})
                text += "variation." + std::to_string(v) + ".event." + std::to_string(k * 997) + "." + f + "="
                        + extremeTokens[static_cast<std::size_t>(k + v) % (sizeof(extremeTokens) / sizeof(extremeTokens[0]))] + "\n";
    const auto start = Clock::now();
    const auto result = scene::SceneSerializer::deserialize(text);
    expect("huge event keys parse quickly", secondsSince(start) < 10.0);
    checkBankSanitary(result.bank, "huge event keys");
    exerciseBank(result.bank);

    // Valid-looking slots with extreme but parseable values.
    const auto rich = scene::SceneSerializer::serialize(richBank());
    for (const char* huge : {"2147483647", "2147483648", "-2147483648", "99999999999999999999", "1e38", "4294967295"})
    {
        std::string t = rich;
        std::size_t pos = 0;
        int replaced = 0;
        while ((pos = t.find(".onset=", pos)) != std::string::npos && replaced < 40)
        {
            const auto end = t.find('\n', pos);
            t.replace(pos + 7, end - pos - 7, huge);
            pos = pos + 7;
            ++replaced;
        }
        pos = 0;
        while ((pos = t.find(".duration=", pos)) != std::string::npos && replaced < 80)
        {
            const auto end = t.find('\n', pos);
            t.replace(pos + 10, end - pos - 10, huge);
            pos = pos + 10;
            ++replaced;
        }
        const auto r = scene::SceneSerializer::deserialize(t);
        checkBankSanitary(r.bank, "huge onset/duration");
        exerciseBank(r.bank);
    }
}

void fuzzPresets()
{
    std::vector<std::string> seeds;
    for (const auto& p : preset::factoryPresets())
        if (seeds.size() < 6)
            seeds.push_back(preset::serializePreset(p));
    seeds.push_back(preset::serializePreset(preset::makePreset("User preset", "Stutter", {"a", "b", "c"}, richBank(), "me",
                                                                {{"masterIntensity", 0.5f}, {"dryWet", 0.25f}})));
    Rng rng(0x5eed5eedull);
    const int n = iterations();
    std::size_t accepted = 0;
    g_largestRequest = 0;
    double worst = 0.0;
    for (int i = 0; i < n; ++i)
    {
        std::string text = mutate(seeds[rng.below(seeds.size())], rng);
        const auto start = Clock::now();
        const auto parsed = preset::parsePreset(text);
        worst = std::max(worst, secondsSince(start));
        if (!parsed.ok)
            continue;
        ++accepted;
        const auto& p = parsed.preset;
        expect("preset name bounded", p.name.size() <= preset::maxNameLength && !p.name.empty());
        expect("preset tags bounded", p.tags.size() <= preset::maxTags);
        expect("preset params bounded", p.params.size() <= preset::maxParams);
        for (const auto& param : p.params)
            expect("preset param value in 0..1", std::isfinite(param.value) && param.value >= 0.0f && param.value <= 1.0f);
        expect("preset scene blob bounded", p.sceneText.size() <= preset::maxFileBytes);
        if (const auto bank = preset::bankOf(p))
        {
            checkBankSanitary(*bank, "preset bank");
            if (i % 5 == 0)
                exerciseBank(*bank);
        }
        // Serializing an accepted preset and parsing it again must also be accepted.
        expect("accepted preset re-parses", preset::parsePreset(preset::serializePreset(p)).ok);
    }
    for (std::size_t cut = 0; cut < seeds.back().size(); cut += 1 + seeds.back().size() / 500)
        (void) preset::parsePreset(std::string_view(seeds.back()).substr(0, cut));
    // Oversize input is rejected without reading it all into scene storage.
    (void) preset::parsePreset(std::string(preset::maxFileBytes + 1, 'x'));
    (void) preset::parsePreset(std::string(4u << 20, '\n'));
    std::cout << "preset fuzz: " << n << " inputs, " << accepted << " accepted, slowest " << worst
              << " s, largest allocation " << g_largestRequest.load() << " bytes\n";
    expect("preset parse time bounded", worst < 5.0);
    expect("preset parse allocations bounded", g_largestRequest.load() < (64u << 20));
}

std::uint32_t crc32(const std::uint8_t* data, std::size_t size) noexcept
{
    std::uint32_t c = 0xFFFFFFFFu;
    for (std::size_t i = 0; i < size; ++i)
    {
        c ^= data[i];
        for (int k = 0; k < 8; ++k)
            c = (c & 1u) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
    }
    return c ^ 0xFFFFFFFFu;
}

// Makes a mutated blob structurally plausible again (payload length field and CRC match), so the fuzzer gets past
// the integrity checks and exercises the field validation and the entropy decoder themselves.
void resign(std::vector<std::uint8_t>& bytes)
{
    constexpr std::size_t header = 36, crc = 4;
    if (bytes.size() < header + crc)
        return;
    const auto payload = static_cast<std::uint32_t>(bytes.size() - header - crc);
    for (int k = 0; k < 4; ++k)
        bytes[32 + static_cast<std::size_t>(k)] = static_cast<std::uint8_t>(payload >> (8 * k));
    const auto sum = crc32(bytes.data(), bytes.size() - crc);
    for (int k = 0; k < 4; ++k)
        bytes[bytes.size() - crc + static_cast<std::size_t>(k)] = static_cast<std::uint8_t>(sum >> (8 * k));
}

void fuzzLoopBlobs()
{
    std::vector<float> samples(6000);
    for (std::size_t i = 0; i < samples.size(); ++i)
        samples[i] = 0.6f * std::sin(static_cast<float>(i) * 0.03f) + ((i % 97) == 0 ? 0.3f : 0.0f);
    std::vector<float> right(samples.rbegin(), samples.rend());
    const float* stereo[2] = {samples.data(), right.data()};
    const float* mono[1] = {samples.data()};
    std::vector<std::uint8_t> blobStereo, blobMono;
    expect("encode stereo", LoopBlob::encode(stereo, 2, static_cast<std::int64_t>(samples.size()), 44100.0, 4.0, 0.5, blobStereo) == LoopBlob::Status::Ok);
    expect("encode mono", LoopBlob::encode(mono, 1, static_cast<std::int64_t>(samples.size()), 48000.0, 8.0, 0.0, blobMono) == LoopBlob::Status::Ok);
    LoopBlob::Audio clean;
    expect("clean decode", LoopBlob::decode(blobStereo.data(), blobStereo.size(), clean) == LoopBlob::Status::Ok
                               && clean.left.size() == samples.size());

    Rng rng(0xb10bb10bull);
    const int n = iterations() * 4;
    g_largestRequest = 0;
    std::size_t decoded = 0;
    for (int i = 0; i < n; ++i)
    {
        auto bytes = (rng.below(2) == 0) ? blobStereo : blobMono;
        const int steps = 1 + static_cast<int>(rng.below(5));
        for (int s = 0; s < steps && !bytes.empty(); ++s)
        {
            switch (rng.below(5))
            {
                case 0: bytes[rng.below(bytes.size())] ^= static_cast<std::uint8_t>(1u << rng.below(8)); break;
                case 1: bytes[rng.below(bytes.size())] = static_cast<std::uint8_t>(rng.next()); break;
                case 2: bytes.resize(rng.below(bytes.size() + 1)); break;
                case 3: bytes.insert(bytes.begin() + static_cast<std::ptrdiff_t>(rng.below(bytes.size() + 1)),
                                     static_cast<std::uint8_t>(rng.next())); break;
                default: // forge a header field (sample count / payload size / rate) with an extreme value
                {
                    const std::size_t at = 8 + rng.below(32);
                    for (std::size_t k = 0; k < 4 && at + k < bytes.size(); ++k)
                        bytes[at + k] = (rng.below(2) == 0) ? 0xff : static_cast<std::uint8_t>(rng.next());
                    break;
                }
            }
        }
        if (rng.below(10) < 7)
            resign(bytes);
        LoopBlob::Audio out;
        const auto status = LoopBlob::decode(bytes.data(), bytes.size(), out);
        if (status == LoopBlob::Status::Ok)
        {
            ++decoded;
            expect("decoded loop length bounded", out.left.size() == out.right.size()
                       && static_cast<double>(out.left.size()) <= LoopBlob::maxSeconds * out.sampleRate + 1.0);
            expect("decoded rate in range", out.sampleRate >= LoopBlob::minSampleRate && out.sampleRate <= LoopBlob::maxSampleRate);
            expect("decoded beats finite and bounded", std::isfinite(out.loopBeats) && out.loopBeats > 0.0
                                                          && out.loopBeats <= LoopBlob::maxLoopBeats && std::isfinite(out.originBeats));
            for (const float v : out.left)
                if (!std::isfinite(v) || std::abs(v) > 1.0f)
                {
                    expect("decoded samples finite and within +-1", false);
                    break;
                }
        }
    }
    // Every prefix of a valid blob.
    for (std::size_t cut = 0; cut < blobStereo.size(); cut += 1 + blobStereo.size() / 1000)
    {
        LoopBlob::Audio out;
        (void) LoopBlob::decode(blobStereo.data(), cut, out);
    }
    // A header that claims the maximum sample count with a tiny payload must not allocate for it.
    {
        auto forged = blobMono;
        forged.resize(std::min<std::size_t>(forged.size(), 60));
        for (std::size_t at = 8; at + 4 <= forged.size() && at < 40; at += 4)
        {
            const auto saved = std::vector<std::uint8_t>(forged.begin() + static_cast<std::ptrdiff_t>(at), forged.begin() + static_cast<std::ptrdiff_t>(at) + 4);
            for (std::size_t k = 0; k < 4; ++k)
                forged[at + k] = 0xff;
            LoopBlob::Audio out;
            (void) LoopBlob::decode(forged.data(), forged.size(), out);
            std::copy(saved.begin(), saved.end(), forged.begin() + static_cast<std::ptrdiff_t>(at));
        }
    }
    std::cout << "loop blob fuzz: " << n << " inputs, " << decoded << " decoded, largest allocation "
              << g_largestRequest.load() << " bytes\n";
    expect("loop blob decode allocations bounded (< 64 MiB)", g_largestRequest.load() < (64u << 20));
}

void fuzzSmallTextFormats()
{
    GestureLane seedLane;
    for (int i = 0; i < 40; ++i)
        seedLane.record(i / 40.0, static_cast<float>(i % 7) / 7.0f);
    const std::string gestureSeed = seedLane.serialize();
    MidiLinkTable links;
    links.link(21, "p:masterIntensity");
    links.link(22, "s:0.depth");
    links.link(23, "s:3.slice");
    const std::string linkSeed = links.serialize();

    Rng rng(0x6e57ull);
    const int n = iterations() * 2;
    for (int i = 0; i < n; ++i)
    {
        GestureLane lane;
        const auto restored = lane.deserialize(mutate(gestureSeed, rng));
        expect("gesture bins bounded", restored <= static_cast<std::size_t>(GestureLane::binCount) && lane.filledBins() <= GestureLane::binCount);
        for (const double phase : {0.0, 0.5, 0.999999, 1.0, -1.0, 1.0e300, std::numeric_limits<double>::quiet_NaN(),
                                   std::numeric_limits<double>::infinity()})
        {
            const float v = lane.sample(phase);
            expect("gesture sample finite and normalised", std::isfinite(v) && v <= 1.0f);
            lane.record(phase, v);
        }
        const auto table = MidiLinkTable::deserialize(mutate(linkSeed, rng));
        expect("midi link table bounded", table.links().size() <= 256);
        for (const auto& link : table.links())
            expect("midi link cc range", link.cc >= 0 && link.cc < 128 && MidiLinkTable::validTarget(link.target));
        (void) MidiLinkTable::deserialize(std::string(40000, ';'));
    }
    // Loop import mapping with extreme numbers.
    for (const double seconds : {0.0, -1.0, 1.0e-300, 20.0, 1.0e300, std::numeric_limits<double>::quiet_NaN()})
        for (const int beats : {-1, 0, 1, 4, 32, 2147483647})
            for (const double bpm : {0.0, 120.0, -5.0, 1.0e300, std::numeric_limits<double>::infinity()})
                for (const int bars : {-3, 0, 1, 16, 2147483647})
                {
                    const auto m = mapLoop(seconds, beats, bpm, bars, bpm);
                    if (m.valid)
                        expect("mapLoop valid results finite", std::isfinite(m.beats) && m.beats > 0.0 && std::isfinite(m.nativeBpm));
                }
}

void fuzzEditCommands()
{
    // Lane edits and scene-level commands with extreme integers and phrase lengths (signed overflow, huge loops).
    Rng rng(0xed17ull);
    const std::int64_t phrases[] = {0, 1, -1, 960, 15360, 1966080, std::int64_t{1} << 40, std::numeric_limits<std::int64_t>::max(),
                                    std::numeric_limits<std::int64_t>::min()};
    const std::int32_t extreme[] = {0, 1, -1, 959, 960, 15360, 1966080, std::numeric_limits<std::int32_t>::max(),
                                    std::numeric_limits<std::int32_t>::min(), std::numeric_limits<std::int32_t>::max() - 1};
    const int n = iterations();
    for (int i = 0; i < n; ++i)
    {
        VariationSettings scene = richBank().variation(rng.below(variationCount)).settings;
        const auto phrase = phrases[rng.below(sizeof(phrases) / sizeof(phrases[0]))];
        for (int k = 0; k < 6; ++k)
        {
            LaneEvent e;
            e.operation = static_cast<std::uint8_t>(rng.next());
            e.onsetTicks = extreme[rng.below(sizeof(extreme) / sizeof(extreme[0]))];
            e.durationTicks = extreme[rng.below(sizeof(extreme) / sizeof(extreme[0]))];
            e.depth = (rng.below(4) == 0) ? std::numeric_limits<float>::quiet_NaN() : static_cast<float>(rng.below(300)) / 100.0f - 1.0f;
            e.sliceBeats = (rng.below(4) == 0) ? std::numeric_limits<float>::infinity() : static_cast<float>(rng.below(900)) / 100.0f - 2.0f;
            e.priority = static_cast<std::uint8_t>(rng.next());
            e.chance = static_cast<std::uint8_t>(rng.next());
            (void) addEvent(scene.control.lane, e, phrase);
            (void) replaceEvent(scene.control.lane, rng.below(scene.control.lane.count + 3), e, phrase);
            if (rng.below(3) == 0)
                (void) removeEvent(scene.control.lane, rng.below(scene.control.lane.count + 3));
        }
        (void) scaleEvents(scene.control.lane, static_cast<std::int64_t>(rng.below(80)) - 5, static_cast<std::int64_t>(rng.below(80)) - 5,
                           phrase, scene.control.permissions);
        (void) reduceEvents(scene.control.lane, scene.control.activity, scene.control.permissions, phrase, rng.below(2) == 0);
        (void) convertLegacyToEvents(scene.control.lane, static_cast<std::uint32_t>(rng.next()), phrase);
        (void) checkPhraseFit(scene.control.lane, phrase, &scene.control.permissions);
        scene.control.activity.minSpacingBeats = (rng.below(3) == 0) ? std::numeric_limits<float>::quiet_NaN() : 1.0e30f;
        scene.control.activity.maxCoverage = -1.0f;
        (void) sanitize(scene.control, static_cast<std::int32_t>(std::clamp<std::int64_t>(phrase, 0, std::numeric_limits<std::int32_t>::max())));
        expect("lane count bounded after edits", scene.control.lane.count <= maxLaneEvents);

        EuclidParams params;
        params.steps = static_cast<std::uint8_t>(rng.next());
        params.pulses = static_cast<std::uint8_t>(rng.next());
        params.rotation = static_cast<std::int8_t>(rng.next());
        params.operation = static_cast<std::uint8_t>(rng.next());
        params.subdivision = static_cast<std::uint8_t>(rng.next());
        params.gatePercent = static_cast<std::uint8_t>(rng.next());
        params.accentEvery = static_cast<std::uint8_t>(rng.next());
        params.accentDepth = (rng.below(3) == 0) ? std::numeric_limits<float>::quiet_NaN() : 7.0f;
        params.density = static_cast<std::uint8_t>(rng.next());
        (void) applyEuclid(scene, params, phrase, rng.below(10));
        (void) buildClearCandidate(scene, static_cast<std::uint8_t>(rng.next()), phrase);
        expect("lane count bounded after euclid", scene.control.lane.count <= maxLaneEvents);
    }
}
// A3-03: reduceEvents used to unroll every event over the phrase; with a short loop and a hostile phrase length
// (INT64_MAX, 2^40) that needed billions of spans and the process was killed for lack of memory.
void regressionReduceHugePhrase()
{
    g_largestRequest = 0;
    for (const std::int64_t phrase : {std::numeric_limits<std::int64_t>::max(), std::int64_t{1} << 40, maxPhraseTicks + 1})
    {
        VariationSettings scene = richBank().variation(0).settings;
        scene.control.lane.loopBeats.fill(1); // 960-tick ring: the worst case for unrolling
        scene.control.activity.minSpacingBeats = 0.0f;
        const auto before = scene.control.lane.count;
        const auto report = reduceEvents(scene.control.lane, scene.control.activity, scene.control.permissions, phrase, true);
        expect("reduceEvents rejects an out-of-range phrase without touching the lane",
               report.kept == 0 && report.dropped == 0 && scene.control.lane.count == before);
    }
    VariationSettings scene = richBank().variation(0).settings;
    scene.control.lane.loopBeats.fill(1);
    (void) reduceEvents(scene.control.lane, scene.control.activity, scene.control.permissions, maxPhraseTicks, false);
    expect("reduceEvents at the largest legal phrase stays under 64 MiB per allocation", g_largestRequest.load() < (64u << 20));
}

// A3-07: applyEuclid computed ceil(ring / tile) tiles with signed arithmetic and looped over all of them.
void regressionEuclidHugePhrase()
{
    for (const std::int64_t phrase : {std::numeric_limits<std::int64_t>::max(), std::int64_t{1} << 40, maxPhraseTicks + 1,
                                      std::numeric_limits<std::int64_t>::min(), std::int64_t{0}, std::int64_t{-1}})
    {
        VariationSettings scene = richBank().variation(0).settings;
        EuclidParams params;
        params.steps = 1;
        params.pulses = 1;
        params.subdivision = 16;
        params.repeat = true;
        const auto start = Clock::now();
        const auto report = applyEuclid(scene, params, phrase, 0);
        expect("applyEuclid rejects an out-of-range phrase", report.status == EuclidStatus::Invalid);
        expect("applyEuclid rejects an out-of-range phrase quickly", secondsSince(start) < 1.0);
    }
    VariationSettings scene = richBank().variation(0).settings;
    EuclidParams params;
    params.steps = 1;
    params.pulses = 1;
    params.subdivision = 16;
    const auto start = Clock::now();
    (void) applyEuclid(scene, params, maxPhraseTicks, 0);
    expect("applyEuclid at the largest legal phrase is fast", secondsSince(start) < 2.0);
}

// A3-04 / A3-05 and friends: audio-path entry points given absurd arguments must stay total and finite.
void hostileDspArguments()
{
    constexpr double nan = std::numeric_limits<double>::quiet_NaN();
    constexpr double inf = std::numeric_limits<double>::infinity();
    // evaluate(): event with chance at positions whose cycle index exceeds int64.
    {
        SceneControl c;
        c.lane.explicitEvents = true;
        LaneEvent e;
        e.durationTicks = 16 * ticksPerBeat;
        e.chance = 50;
        (void) addEvent(c.lane, e, 16 * ticksPerBeat);
        GridContext g;
        for (const double pos : {1.0e15, 1.0e19, 1.0e25, 1.0e300, -1.0e300, 4.0e18, nan, inf})
        {
            const auto t = evaluate(c, g, pos, 0x7f, pos);
            expect("evaluate stays finite at hostile positions", t.ticksUntilChange > 0.0 && !std::isnan(t.ticksUntilChange));
        }
    }
    // Engine: NaN / infinite phase offsets and crossfade must leave the audio finite.
    {
        RhythmEngine engine;
        engine.prepare(48000.0, 256, 2);
        VariationSettings scene;
        for (auto& op : scene.operations)
            op = {true, 1.0f, 0.25f};
        std::vector<float> l(256), r(256);
        float* ch[2] = {l.data(), r.data()};
        bool finite = true;
        for (const double offset : {nan, inf, -inf, 1.0e300, -1.0e300})
        {
            RhythmTiming t;
            t.samplesPerBeat = 24000.0;
            t.isPlaying = true;
            t.phaseOffsetBeats.fill(offset);
            for (int b = 0; b < 40; ++b)
            {
                for (std::size_t i = 0; i < l.size(); ++i)
                    l[i] = r[i] = std::sin(static_cast<float>(i + 256 * b) * 0.03f);
                t.beatAtBlockStart = 2.0 + b * 256.0 / 24000.0;
                engine.process(ch, 2, 256, scene, 1.0f, t);
                for (std::size_t i = 0; i < l.size(); ++i)
                    finite = finite && std::isfinite(l[i]) && std::isfinite(r[i]);
            }
        }
        expect("engine output finite with non-finite phase offsets", finite);
    }
    // LoopCapture / OutputCapture with absurd tempo: never "Done" with a nonsense length.
    for (const double spb : {1.0e300, 1.0e-300, 1.0e18, 0.0, -5.0, nan, inf})
    {
        LoopCapture capture;
        capture.prepare(48000.0);
        OutputCapture output;
        output.prepare(48000.0);
        std::vector<float> l(256, 0.25f), r(256, 0.25f);
        const float* ch[2] = {l.data(), r.data()};
        expect("loop capture arm", capture.arm(1, 4));
        expect("output capture arm", output.arm(1, 4, 0.0));
        for (int b = 0; b < 8; ++b)
        {
            capture.process(ch, 2, 256, 4.0 * b, spb, true, 4);
            output.process(ch, 2, 256, 4.0 * b, spb, true);
        }
        if (capture.state() == LoopCapture::State::Done)
            expect("loop capture length sane", capture.recordedSamples() > 0 && capture.recordedSamples() <= 20 * 48000);
        if (output.state() == OutputCapture::State::Done)
            expect("output capture length sane", output.recordedSamples() > 0 && output.recordedSamples() <= output.capacitySamples());
    }
    // Band split with non-finite cutoff and tiny / huge blocks.
    {
        dsp::BandSplit split;
        split.prepare(48000.0, 64);
        std::vector<float> l(64, 0.5f), r(64, -0.5f);
        float* ch[2] = {l.data(), r.data()};
        for (const double hz : {nan, inf, -inf, 0.0, -10.0, 1.0e9})
        {
            split.setCutoff(hz);
            split.split(ch, 2, 64);
            split.merge(ch, 2, 64);
            split.split(ch, 2, 4096); // larger than the prepared block: clamped, never overrun
            split.merge(ch, 2, 4096);
        }
        expect("band split stays finite", std::isfinite(l[0]) && std::isfinite(r[63]));
    }
}
} // namespace

int main()
{
    std::cout << "[fuzzScenes]" << std::endl;
    fuzzScenes();
    std::cout << "[fuzzHugeEventKeys]" << std::endl;
    fuzzHugeEventKeys();
    std::cout << "[fuzzPresets]" << std::endl;
    fuzzPresets();
    std::cout << "[fuzzLoopBlobs]" << std::endl;
    fuzzLoopBlobs();
    std::cout << "[fuzzSmallTextFormats]" << std::endl;
    fuzzSmallTextFormats();
    std::cout << "[fuzzEditCommands]" << std::endl;
    fuzzEditCommands();
    std::cout << "[regressions]" << std::endl;
    regressionReduceHugePhrase();
    regressionEuclidHugePhrase();
    hostileDspArguments();
    if (failures == 0)
        std::cout << "HostileStateFuzzTests passed\n";
    return failures == 0 ? 0 : 1;
}
