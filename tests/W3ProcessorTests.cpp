// W3 processor-level tests: input sanitizing, C04 smoothing and phase capture, C05 Dry return, F02 morph,
// capacity round trip, timer lifecycle and phrase-exact evolution (docs/specs/W3-*.md).
#include "PluginProcessor.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <functional>
#include <iostream>
#include <limits>
#include <string_view>
#include <thread>
#include <vector>

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
    else
        std::cerr << "ok: " << name << '\n' << std::flush;
}

class FakePlayHead final : public juce::AudioPlayHead
{
public:
    juce::Optional<PositionInfo> getPosition() const override { return info; }
    PositionInfo info;
};

void setParameter(GrooveMorphAudioProcessor& processor, const char* id, float plainValue)
{
    auto* parameter = processor.parameters.getParameter(id);
    parameter->setValueNotifyingHost(parameter->convertTo0to1(plainValue));
}

void setTransport(FakePlayHead& playHead, std::int64_t sample)
{
    // 120 BPM 4/4 at 48 kHz: 24000 samples per quarter, 96000 per bar.
    const double ppq = static_cast<double>(sample) / 24000.0;
    playHead.info.setBpm(120.0);
    playHead.info.setTimeSignature(juce::AudioPlayHead::TimeSignature{4, 4});
    playHead.info.setIsPlaying(true);
    playHead.info.setTimeInSamples(sample);
    playHead.info.setPpqPosition(ppq);
    playHead.info.setPpqPositionOfLastBarStart(std::floor(ppq / 4.0) * 4.0);
}

float noiseAt(std::int64_t i, int channel)
{
    std::uint32_t s = static_cast<std::uint32_t>(i) * 2654435761u + static_cast<std::uint32_t>(channel) * 40503u + 12345u;
    s ^= s << 13; s ^= s >> 17; s ^= s << 5;
    return (static_cast<float>(s & 0xFFFF) / 32768.0f - 1.0f) * 0.4f;
}

// Processes one block of deterministic noise starting at `sample`; returns the output (and the input via `in`).
juce::AudioBuffer<float> processNoise(GrooveMorphAudioProcessor& processor, std::int64_t sample, int n,
                                      juce::AudioBuffer<float>* in = nullptr)
{
    juce::AudioBuffer<float> buffer(2, n);
    for (int c = 0; c < 2; ++c)
        for (int i = 0; i < n; ++i)
            buffer.setSample(c, i, noiseAt(sample + i, c));
    if (in != nullptr)
        in->makeCopyOf(buffer);
    juce::MidiBuffer midi;
    processor.processBlock(buffer, midi);
    return buffer;
}

bool allFinite(const juce::AudioBuffer<float>& b)
{
    for (int c = 0; c < b.getNumChannels(); ++c)
        for (int i = 0; i < b.getNumSamples(); ++i)
            if (!std::isfinite(b.getSample(c, i)))
                return false;
    return true;
}

// Runs the message loop for about `ms` milliseconds (the test's main thread is the message thread).
// The tests run on the message thread; waiting dispatches queued messages and timers.
void pumpMessages(int ms)
{
    juce::MessageManager::getInstance()->runDispatchLoopUntil(ms);
}

bool waitUntil(const std::function<bool()>& condition, int timeoutMs = 2000)
{
    for (int waited = 0; waited < timeoutMs && !condition(); waited += 5)
        pumpMessages(5);
    return condition();
}

groovemorph::core::VariationSettings effectScene(groovemorph::core::OperationType op, float slice = 0.25f)
{
    groovemorph::core::VariationSettings v;
    v[op] = {true, 1.0f, slice};
    return v;
}
} // namespace

static void runTests()
{
    using groovemorph::core::OperationType;

    // ---- W3-NF: NaN/Inf never reach the output, on any path ----
    {
        const float nan = std::numeric_limits<float>::quiet_NaN();
        const float inf = std::numeric_limits<float>::infinity();
        for (int bypass = 0; bypass < 2; ++bypass)
            for (int mode = 0; mode < 3; ++mode)
                for (int topology = 0; topology < 3; ++topology)
                    for (int slope = 0; slope < 2; ++slope)
                    {
                        GrooveMorphAudioProcessor processor;
                        FakePlayHead playHead;
                        processor.setPlayHead(&playHead);
                        processor.prepareToPlay(48000.0, 256);
                        processor.setVariationSettings(0, effectScene(OperationType::Repeat));
                        setParameter(processor, "masterIntensity", 1.0f);
                        setParameter(processor, "bandMode", static_cast<float>(mode));
                        setParameter(processor, "bandTopology", static_cast<float>(topology));
                        setParameter(processor, "bandSlope", static_cast<float>(slope));
                        if (bypass == 1)
                            setParameter(processor, "dryWet", 0.0f);
                        bool finite = true;
                        std::int64_t sample = 0;
                        for (int block = 0; block < 40; ++block)
                        {
                            juce::AudioBuffer<float> buffer(2, 256);
                            for (int c = 0; c < 2; ++c)
                                for (int i = 0; i < 256; ++i)
                                    buffer.setSample(c, i, noiseAt(sample + i, c));
                            if (block >= 4 && block < 8)
                            {
                                buffer.setSample(0, 10, nan);
                                buffer.setSample(1, 100, inf);
                                buffer.setSample(0, 200, -inf);
                            }
                            setTransport(playHead, sample);
                            juce::MidiBuffer midi;
                            if (bypass == 1 && block == 20)
                                processor.processBlockBypassed(buffer, midi);
                            else
                                processor.processBlock(buffer, midi);
                            finite = finite && allFinite(buffer);
                            sample += 256;
                        }
                        const std::string tag = "bypass " + std::to_string(bypass) + " mode " + std::to_string(mode)
                                                + " topo " + std::to_string(topology) + " slope " + std::to_string(slope);
                        expect("W3-NF output stays finite (" + tag + ")", finite);
                        expect("W3-NF counted the replaced samples (" + tag + ")", processor.getNonFiniteSampleCount() == 12);
                        processor.setPlayHead(nullptr);
                    }
        // A bypassed block: clean samples pass through exactly, bad ones become zero.
        GrooveMorphAudioProcessor processor;
        processor.prepareToPlay(48000.0, 256);
        juce::AudioBuffer<float> buffer(2, 64);
        for (int i = 0; i < 64; ++i)
        {
            buffer.setSample(0, i, 0.25f);
            buffer.setSample(1, i, -0.5f);
        }
        buffer.setSample(0, 5, nan);
        juce::MidiBuffer midi;
        processor.processBlockBypassed(buffer, midi);
        expect("W3-NF bypassed block: NaN becomes 0", buffer.getSample(0, 5) == 0.0f);
        expect("W3-NF bypassed block: clean samples are untouched", buffer.getSample(0, 6) == 0.25f && buffer.getSample(1, 5) == -0.5f);
    }

    // ---- W3-C04S / W3-PC: smoothing time and phase capture ----
    {
        GrooveMorphAudioProcessor processor;
        FakePlayHead playHead;
        processor.setPlayHead(&playHead);
        processor.prepareToPlay(48000.0, 256);
        setParameter(processor, "transitionMode", 2.0f); // Next Bar
        auto scene1 = effectScene(OperationType::Repeat);
        scene1.control.phase.offsetBeats = 1.0f;
        scene1.control.phase.smoothingMs = 25.0f;
        processor.setVariationSettings(1, scene1);
        std::int64_t sample = 0;
        const auto block = [&] {
            setTransport(playHead, sample);
            processNoise(processor, sample, 256);
            sample += 256;
        };
        block();
        expect("W3-C04S default smoothing is the engine default", processor.getTransitionSmoothingMs() == 0.0f);
        processor.requestVariation(1);
        block();
        // Edit the destination after the request: the launch must still use the captured configuration.
        auto edited = scene1;
        edited.control.phase.offsetBeats = 2.0f;
        edited.control.phase.smoothingMs = 100.0f;
        processor.setVariationSettings(1, edited);
        while (sample < 96000 + 512)
            block();
        expect("W3-PC the switch fired", processor.getActiveVariation() == 1);
        expect("W3-PC phase uses the config captured at request time", std::abs(processor.getPhaseShiftBeats() - 1.0) < 1.0e-9);
        expect("W3-C04S smoothing uses the config captured at request time", processor.getTransitionSmoothingMs() == 25.0f);
        // A request made after the edit uses the edited values.
        processor.requestVariation(0);
        while (sample < 2 * 96000 + 512)
            block();
        processor.requestVariation(1);
        while (sample < 3 * 96000 + 512)
            block();
        expect("W3-PC a later request uses the edited phase", std::abs(processor.getPhaseShiftBeats() - 2.0) < 1.0e-9);
        expect("W3-C04S a later request uses the edited smoothing", processor.getTransitionSmoothingMs() == 100.0f);
        processor.setPlayHead(nullptr);
    }

    // ---- W3-DR: Dry return target ----
    {
        GrooveMorphAudioProcessor processor;
        FakePlayHead playHead;
        processor.setPlayHead(&playHead);
        processor.prepareToPlay(48000.0, 256);
        processor.setVariationSettings(0, groovemorph::core::VariationSettings{});
        processor.setVariationSettings(1, effectScene(OperationType::Repeat));
        setParameter(processor, "masterIntensity", 1.0f);
        setParameter(processor, "transitionMode", 0.0f); // Immediate
        setParameter(processor, "autoReturnBars", 1.0f);
        setParameter(processor, "autoReturnTarget", 5.0f); // Dry
        std::int64_t sample = 0;
        double wetDifferenceBefore = 0.0, dryDifference = 0.0;
        bool stateArmed = false;
        const auto block = [&] {
            setTransport(playHead, sample);
            juce::AudioBuffer<float> in;
            const auto out = processNoise(processor, sample, 256, &in);
            double diff = 0.0;
            for (int c = 0; c < 2; ++c)
                for (int i = 0; i < 256; ++i)
                    diff = std::max<double>(diff, std::abs(out.getSample(c, i) - in.getSample(c, i)));
            if (sample + 256 <= 96000 && processor.getActiveVariation() == 1)
                wetDifferenceBefore = std::max(wetDifferenceBefore, diff);
            if (sample >= 96000 + 4800)
                dryDifference = std::max(dryDifference, diff);
            sample += 256;
        };
        block();
        processor.requestVariation(1);
        block();
        block();
        stateArmed = processor.getDryReturnState() == 1;
        expect("W3-DR armed after the switch", stateArmed);
        expect("W3-DR a Dry arming does not claim a scene target", processor.getAutoReturnTarget() == -1);
        while (sample < 96000 + 256 * 40)
            block();
        expect("W3-DR the effect was audible before the boundary", wetDifferenceBefore > 0.01);
        expect("W3-DR active after the boundary", processor.getDryReturnState() == 2);
        expect("W3-DR output equals the input once the ramp is done", dryDifference < 1.0e-6);
        expect("W3-DR the active scene is unchanged", processor.getActiveVariation() == 1);
        processor.requestVariation(1);
        for (int i = 0; i < 40; ++i)
            block();
        expect("W3-DR a scene request ends the Dry state", processor.getDryReturnState() == 0);
        processor.setPlayHead(nullptr);
    }

    // ---- W3-MORPH: deterministic, block-size independent, amount 0 equals no morph ----
    {
        const auto render = [](int blockSize, int target, float amount) {
            GrooveMorphAudioProcessor processor;
            FakePlayHead playHead;
            processor.setPlayHead(&playHead);
            processor.prepareToPlay(48000.0, 1024);
            auto a = effectScene(OperationType::Repeat);
            a.control.lane.explicitEvents = true;
            a.control.lane.events[0].operation = static_cast<std::uint8_t>(OperationType::Repeat);
            a.control.lane.events[0].onsetTicks = 0;
            a.control.lane.events[0].durationTicks = 1920;
            a.control.lane.count = 1;
            auto b = effectScene(OperationType::Gate, 0.5f);
            b.control.lane.explicitEvents = true;
            b.control.lane.events[0].operation = static_cast<std::uint8_t>(OperationType::Gate);
            b.control.lane.events[0].onsetTicks = 1920;
            b.control.lane.events[0].durationTicks = 1920;
            b.control.lane.count = 1;
            processor.setVariationSettings(0, a);
            processor.setVariationSettings(1, b);
            setParameter(processor, "masterIntensity", 1.0f);
            setParameter(processor, "morphTarget", static_cast<float>(target));
            setParameter(processor, "morphAmount", amount);
            std::vector<float> out;
            for (std::int64_t sample = 0; sample < 48000 * 4; sample += blockSize)
            {
                setTransport(playHead, sample);
                const auto b2 = processNoise(processor, sample, blockSize);
                for (int i = 0; i < blockSize; ++i)
                    out.push_back(b2.getSample(0, i));
            }
            processor.setPlayHead(nullptr);
            return out;
        };
        const auto none = render(480, 0, 0.5f);
        const auto zero = render(480, 2, 0.0f);
        expect("W3-MORPH amount 0 equals no morph", none == zero);
        const auto half512 = render(480, 2, 0.5f);
        const auto half1024 = render(960, 2, 0.5f);
        expect("W3-MORPH baseline (no morph) is block-size independent", none == render(960, 0, 0.5f));
        {
            std::size_t first = 0;
            while (first < half512.size() && first < half1024.size() && half512[first] == half1024[first])
                ++first;
            std::cerr << "info: morph block-size first difference at " << first << " of " << half512.size() << "\n";
        }
        expect("W3-MORPH is deterministic across block sizes", half512 == half1024);
        expect("W3-MORPH a half morph differs from both ends", half512 != none && half512 != render(480, 2, 1.0f));
        bool finite = true;
        for (float v : half512)
            finite = finite && std::isfinite(v);
        expect("W3-MORPH finite", finite);
    }

    // ---- W3-CAP: 256 events and 8 regions survive host state ----
    {
        GrooveMorphAudioProcessor processor;
        auto settings = processor.getSceneBank().variation(2).settings;
        settings.control.lane.explicitEvents = true;
        for (std::size_t i = 0; i < groovemorph::core::control::maxLaneEvents; ++i)
        {
            auto& e = settings.control.lane.events[i];
            e.operation = static_cast<std::uint8_t>(i % 7);
            e.onsetTicks = static_cast<std::int32_t>(i * 60);
            e.durationTicks = 60;
            e.priority = static_cast<std::uint8_t>(i % 100);
        }
        settings.control.lane.count = static_cast<std::uint16_t>(groovemorph::core::control::maxLaneEvents);
        settings.control.placement.enabled = true;
        for (std::size_t i = 0; i < groovemorph::core::control::maxProtectedRegions; ++i)
            settings.control.placement.regions[i] = {static_cast<std::int32_t>(i * 400), 100};
        settings.control.placement.regionCount = static_cast<std::uint8_t>(groovemorph::core::control::maxProtectedRegions);
        processor.setVariationSettings(2, settings);
        juce::MemoryBlock state;
        processor.getStateInformation(state);
        GrooveMorphAudioProcessor restored;
        restored.setStateInformation(state.getData(), static_cast<int>(state.getSize()));
        expect("W3-CAP 256 events and 8 regions survive host state",
               restored.getSceneBank().variation(2) == processor.getSceneBank().variation(2));
        expect("W3-CAP the lane really holds 256 events", restored.getSceneBank().variation(2).settings.control.lane.count == 256);
        std::cerr << "info: sizeof(SceneSnapshot) = " << sizeof(groovemorph::core::scene::SceneSnapshot) << " bytes\n";
        expect("W3-CAP the audio snapshot stays a bounded plain copy", sizeof(groovemorph::core::scene::SceneSnapshot) < 512 * 1024);
    }

    // ---- W3-TM: timer lifecycle ----
    {
        // Created, started and destroyed on a non-message thread.
        std::atomic<bool> done{false};
        std::thread worker([&] {
            auto processor = std::make_unique<GrooveMorphAudioProcessor>();
            processor->ensureServices();
            processor->prepareToPlay(48000.0, 256);
            processor->ensureServices();
            processor.reset();
            done = true;
        });
        worker.join();
        expect("W3-TM-T01 create/start/destroy off the message thread", done.load());
        pumpMessages(100); // queued messages run against a dead owner harmlessly
        expect("W3-TM-T01 queued messages after destruction are harmless", true);

        GrooveMorphAudioProcessor processor;
        expect("W3-TM-T03 nothing starts at construction", !processor.areServicesRunning());
        for (int i = 0; i < 100; ++i)
            processor.ensureServices();
        expect("W3-TM-T03 ensureServices is idempotent and starts the timer on the message thread",
               waitUntil([&] { return processor.areServicesRunning(); }));

        // Core-level: detach waits for a tick in flight and no tick runs afterwards.
        std::atomic<int> ticks{0};
        std::atomic<int> late{0};
        std::atomic<bool> detached{false};
        ServiceTimer timer([&] {
            if (detached.load())
                ++late;
            ++ticks;
            std::this_thread::sleep_for(std::chrono::milliseconds(15));
        }, 50);
        timer.ensureStarted();
        pumpMessages(200);
        expect("W3-TM-T02 the timer ticks", ticks.load() > 0);
        std::thread detacher([&] {
            timer.detach();
            detached = true;
        });
        detacher.join();
        const int before = ticks.load();
        pumpMessages(200);
        expect("W3-TM-T02 no tick after detach returned", ticks.load() == before && late.load() == 0);
        timer.detach();
        expect("W3-TM-T03 detach twice is safe and stops the timer", waitUntil([&] { return !timer.isRunning(); }));
    }

    // ---- W3-TM-T05: an evolution step lands on the first sample of the phrase, for any block size ----
    {
        constexpr std::int64_t phrase = 384000;
        const auto run = [&](int blockSize, bool evolve) {
            GrooveMorphAudioProcessor processor;
            FakePlayHead playHead;
            processor.setPlayHead(&playHead);
            processor.prepareToPlay(48000.0, 1024);
            processor.setVariationSettings(0, effectScene(OperationType::Repeat, 0.5f));
            setParameter(processor, "masterIntensity", 1.0f);
            if (evolve)
                processor.setEvolution(true, 0, 1, 1.0f, 2, 1234);
            std::vector<float> out;
            for (std::int64_t sample = 0; sample < phrase + 16000; sample += blockSize)
            {
                setTransport(playHead, sample);
                const auto b = processNoise(processor, sample, blockSize);
                for (int i = 0; i < blockSize; ++i)
                    out.push_back(b.getSample(0, i));
                processor.updateEvolution();
            }
            processor.setPlayHead(nullptr);
            return out;
        };
        const auto reference = run(64, false);
        std::int64_t firstDiff64 = -1;
        for (int blockSize : {64, 257, 1024})
        {
            const auto evolved = run(blockSize, true);
            std::int64_t first = -1;
            for (std::size_t i = 0; i < std::min(reference.size(), evolved.size()); ++i)
                if (reference[i] != evolved[i])
                {
                    first = static_cast<std::int64_t>(i);
                    break;
                }
            if (blockSize == 64)
                firstDiff64 = first;
            expect("W3-TM-T05 the evolved output starts at the phrase boundary (block " + std::to_string(blockSize) + ", first diff " + std::to_string(first) + ")",
                   first >= phrase && first <= phrase + 16);
            expect("W3-TM-T05 same sample for every block size (block " + std::to_string(blockSize) + ")", first == firstDiff64);
        }
    }

    if (failures == 0)
        std::cerr << "W3ProcessorTests: all checks passed\n";
}

int main()
{
    juce::ScopedJuceInitialiser_GUI juceInitialiser;
    // The tests run on the message thread (the main thread) and pump it with runDispatchLoopUntil.
    runTests();
    return failures == 0 ? 0 : 1;
}
