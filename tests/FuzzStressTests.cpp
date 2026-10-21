// Randomized fuzz/stress test through the real processor. Each round: random sample rate and block size,
// every host parameter set to a random value, random scenes, random MIDI, hostile transport, UI-thread actions
// (scene switches, undo, audition, evolution, surprise, loop capture, print, presets, gestures) and state
// round trips between blocks. Checks: no crash, finite output, bounded peak, state round trip loads.
// Usage: GrooveMorphFuzzTests [rounds=40] [firstSeed=1] [secondsPerRound=6]
#include "PluginProcessor.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <vector>

namespace
{
using groovemorph::core::OperationType;
using groovemorph::core::VariationSettings;

int failures = 0;
std::uint64_t totalBlocks = 0;

class FakePlayHead final : public juce::AudioPlayHead
{
public:
    juce::Optional<PositionInfo> getPosition() const override { return hostSilent ? juce::Optional<PositionInfo>{} : info; }
    PositionInfo info;
    bool hostSilent = false;
};

struct Fail
{
    std::uint64_t seed;
    std::string what;
};

void report(std::uint64_t seed, const std::string& what)
{
    ++failures;
    std::cerr << "FUZZ-FAIL seed=" << seed << ": " << what << '\n' << std::flush;
}

float rf(juce::Random& r, float lo, float hi) { return lo + (hi - lo) * r.nextFloat(); }

VariationSettings randomScene(juce::Random& r)
{
    VariationSettings v;
    for (std::size_t op = 0; op < groovemorph::core::operationCount; ++op)
        v.operations[op] = {r.nextBool(), rf(r, 0.0f, 1.0f),
                            std::vector<float>{0.0625f, 0.125f, 0.25f, 0.5f, 1.0f, 2.0f, 4.0f}[(std::size_t) r.nextInt(7)]};
    return v;
}

void setTransport(FakePlayHead& ph, juce::Random& r, double sr, std::int64_t sample, bool playing, double bpm, int num, int den)
{
    const double ppq = static_cast<double>(sample) / sr * bpm / 60.0;
    ph.info = {};
    ph.info.setBpm(bpm);
    ph.info.setTimeSignature(juce::AudioPlayHead::TimeSignature{num, den});
    ph.info.setIsPlaying(playing);
    ph.info.setTimeInSamples(sample);
    ph.info.setPpqPosition(ppq);
    ph.info.setPpqPositionOfLastBarStart(std::floor(ppq / (num * 4.0 / den)) * (num * 4.0 / den));
    if (r.nextInt(20) == 0)
        ph.info.setIsLooping(true);
}

void fillInput(juce::AudioBuffer<float>& b, juce::Random& r, int mode)
{
    for (int c = 0; c < b.getNumChannels(); ++c)
        for (int i = 0; i < b.getNumSamples(); ++i)
        {
            float v = 0.0f;
            switch (mode)
            {
                case 0: v = 0.0f; break;
                case 1: v = rf(r, -1.0f, 1.0f); break;
                case 2: v = std::sin(0.05f * (float) i + (float) c) * 0.8f; break;
                case 3: v = r.nextInt(300) == 0 ? 1.0f : 0.0f; break; // clicks
                case 4: v = rf(r, -4.0f, 4.0f); break;                // hot, > 0 dBFS
                default: v = rf(r, -0.5f, 0.5f); break;
            }
            b.setSample(c, i, v);
        }
}

void randomMidi(juce::MidiBuffer& m, juce::Random& r, int n)
{
    for (int k = 0, count = r.nextInt(4); k < count; ++k)
    {
        const int at = r.nextInt(n);
        switch (r.nextInt(8))
        {
            case 0: m.addEvent(juce::MidiMessage::noteOn(1 + r.nextInt(16), r.nextInt(128), (juce::uint8) r.nextInt(128)), at); break;
            case 1: m.addEvent(juce::MidiMessage::noteOff(1 + r.nextInt(16), r.nextInt(128)), at); break;
            case 2: m.addEvent(juce::MidiMessage::controllerEvent(1 + r.nextInt(16), r.nextInt(128), r.nextInt(128)), at); break;
            case 3: m.addEvent(juce::MidiMessage::programChange(1 + r.nextInt(16), r.nextInt(128)), at); break;
            case 4: m.addEvent(juce::MidiMessage::pitchWheel(1, r.nextInt(16384)), at); break;
            case 5: m.addEvent(juce::MidiMessage::allNotesOff(1), at); break;
            case 6: { const juce::uint8 sx[] = {0x01, 0x02, 0x03}; m.addEvent(juce::MidiMessage::createSysExMessage(sx, 3), at); break; }
            default: m.addEvent(juce::MidiMessage::midiClock(), at); break;
        }
    }
}

void uiAction(GrooveMorphAudioProcessor& p, juce::Random& r)
{
    switch (r.nextInt(30))
    {
        case 0: p.requestVariation((std::size_t) r.nextInt(4)); break;
        case 1: p.triggerManualSwitch(); break;
        case 2: p.cancelQueuedSwitch(); break;
        case 3: p.restartActiveScene(); break;
        case 4: p.setVariationSettings((std::size_t) r.nextInt(4), randomScene(r)); break;
        case 5: (void) p.undoSceneEdit(); break;
        case 6: (void) p.redoSceneEdit(); break;
        case 7: p.beginAudition((std::size_t) r.nextInt(4)); break;
        case 8: p.toggleAuditionCompare(); break;
        case 9: (void) p.commitAudition(); break;
        case 10: p.revertAudition(); break;
        case 11: p.resetVariation((std::size_t) r.nextInt(4)); break;
        case 12: p.setSceneSeed(((std::uint64_t) r.nextInt64()) ^ 0x9e3779b97f4a7c15ull); break;
        case 13: p.setSourceMode(r.nextInt(5) - 1); break;
        case 14: p.setEvolution(r.nextBool(), (std::size_t) r.nextInt(4), 1 + r.nextInt(8), r.nextFloat(), r.nextInt(4), r.nextInt64()); p.updateEvolution(); break;
        case 15: (void) p.requestSurprise(); break;
        case 16: p.cancelSurprise(); break;
        case 17: p.updateSurprise(); break;
        case 18: (void) p.armLoopCapture(r.nextInt(20)); break;
        case 19: p.cancelLoopCapture(); p.pollLoopCapture(); break;
        case 20: (void) p.armPrint(r.nextInt(18), r.nextBool(), r.nextBool()); break;
        case 21: p.cancelPrint(); { GrooveMorphAudioProcessor::PrintResult res; (void) p.takePrintResult(res); } break;
        case 22: (void) p.applyPreset(r.nextInt(p.getNumPrograms() + 2) - 1); break;
        case 23: p.setGestureTarget(r.nextInt(6) - 1); p.setGestureRecording(r.nextBool()); p.setGesturePlaying(r.nextBool()); p.updateGesture(); break;
        case 24: p.clearGesture(); break;
        case 25: p.panicEasy(); break;
        case 26: p.setMidiMacrosEnabled(r.nextBool()); p.applyMidiMacros(); break;
        case 27: p.clearLoop(); break;
        case 28: p.beginMidiLearn(r.nextBool() ? "masterIntensity" : "morphAmount"); (void) p.linkMidi(r.nextInt(130) - 1, "masterIntensity"); p.servicePendingControllers(); break;
        default: p.resetToDefaults(); break;
    }
}

void runRound(std::uint64_t seed, double seconds)
{
    juce::Random r((juce::int64) (seed * 0x9e3779b97f4a7c15ull + 0x1234567ull));
    for (int w = 0; w < 8; ++w) (void) r.nextInt();
    static const double rates[] = {8000.0, 22050.0, 44100.0, 48000.0, 88200.0, 96000.0, 192000.0};
    static const int blocks[] = {1, 7, 32, 64, 100, 128, 256, 480, 512, 1024, 2048, 4096};
    const double sr = rates[r.nextInt(7)];
    const int maxBlock = blocks[r.nextInt(12)];
    const bool mono = r.nextInt(6) == 0;

    auto processor = std::make_unique<GrooveMorphAudioProcessor>();
    FakePlayHead ph;
    processor->setPlayHead(&ph);
    if (mono)
        processor->setPlayConfigDetails(1, 1, sr, maxBlock);
    processor->prepareToPlay(sr, maxBlock);

    auto randomizeParams = [&] {
        for (auto* prm : processor->getParameters())
            if (prm->getName(32) != "Bypass" || r.nextInt(8) == 0)
                prm->setValueNotifyingHost(r.nextInt(5) == 0 ? (r.nextBool() ? 0.0f : 1.0f) : r.nextFloat());
    };
    randomizeParams();
    for (std::size_t i = 0; i < 4; ++i)
        processor->setVariationSettings(i, randomScene(r));
    processor->setSourceMode(r.nextInt(3));

    const int channels = mono ? 1 : 2;
    juce::AudioBuffer<float> buf(channels, maxBlock);
    juce::MidiBuffer midi;
    std::int64_t pos = 0;
    double bpm = 60.0 + r.nextInt(140);
    bool playing = true;
    int num = 4, den = 4;
    const std::int64_t total = static_cast<std::int64_t>(seconds * sr);
    float worst = 0.0f;
    int inputMode = r.nextInt(6);
    double slowest = 0.0, slowestMs = 0.0;

    for (std::int64_t done = 0; done < total;)
    {
        int n = r.nextInt(6) == 0 ? 1 + r.nextInt(maxBlock) : maxBlock;
        n = std::min(n, maxBlock);
        // hostile transport
        const int ev = r.nextInt(200);
        if (ev == 0) playing = !playing;
        else if (ev == 1) pos = r.nextInt64() % (std::int64_t) (sr * 3600) ;
        else if (ev == 2) pos = 0;
        else if (ev == 3) bpm = r.nextBool() ? 20.0 + r.nextInt(280) : (r.nextBool() ? 1.0 : 999.0);
        else if (ev == 4) { static const int nn[] = {1, 2, 3, 4, 5, 6, 7, 9, 12, 13}; static const int dd[] = {1, 2, 4, 8, 16}; num = nn[r.nextInt(10)]; den = dd[r.nextInt(5)]; }
        else if (ev == 5) ph.hostSilent = !ph.hostSilent;
        else if (ev == 6) inputMode = r.nextInt(6);
        else if (ev == 7) { processor->releaseResources(); processor->prepareToPlay(sr, maxBlock); }
        else if (ev == 8) randomizeParams();
        else if (ev == 9) { if (r.nextInt(4) == 0) processor->prepareToPlay(rates[r.nextInt(7)], blocks[r.nextInt(12)] < maxBlock ? blocks[r.nextInt(12)] : maxBlock); }
        else if (ev == 10)
        {
            juce::MemoryBlock state;
            processor->getStateInformation(state);
            if (r.nextInt(5) == 0 && state.getSize() > 8) // corrupt
                for (int k = 0; k < 4; ++k)
                    state[(size_t) r.nextInt((int) state.getSize())] = (char) r.nextInt(256);
            if (r.nextInt(8) == 0) state.setSize(state.getSize() / 2);
            processor->setStateInformation(state.getData(), (int) state.getSize());
        }
        for (int a = 0, na = r.nextInt(100) == 0 ? 3 : 0; a < na; ++a)
            uiAction(*processor, r);
        if (r.nextInt(25) == 0)
            uiAction(*processor, r);
        setTransport(ph, r, sr, pos, playing, bpm, num, den);

        buf.setSize(channels, n, false, false, true);
        fillInput(buf, r, inputMode);
        midi.clear();
        randomMidi(midi, r, n);
        const auto t0 = std::chrono::steady_clock::now();
        processor->processBlock(buf, midi);
        const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        if (n >= 32) slowest = std::max(slowest, ms / (1000.0 * n / sr) * 100.0); // % of block budget (blocks >= 32 only)
        slowestMs = std::max(slowestMs, ms);
        if (ms > 3.0) std::cerr << "SLOW seed=" << seed << " block=" << totalBlocks << " n=" << n << " ms=" << ms << " ev=" << ev << " play=" << playing << " silentHost=" << ph.hostSilent << " evolving=" << processor->isEvolving() << " stretch=" << processor->isLoopStretchEnabled() << " busy=" << processor->isLoopStretchBusy() << " loop=" << processor->hasLoop() << " src=" << processor->getSourceMode() << '\n';
        ++totalBlocks;

        for (int c = 0; c < channels; ++c)
            for (int i = 0; i < n; ++i)
            {
                const float v = buf.getSample(c, i);
                if (!std::isfinite(v))
                {
                    report(seed, "non-finite output at sample " + std::to_string(done + i));
                    return;
                }
                worst = std::max(worst, std::abs(v));
            }
        if (playing) pos += n;
        done += n;
        if (r.nextInt(300) == 0) { processor->pollLoopCapture(); processor->updateEvolution(); processor->updateSurprise(); processor->updateGesture(); }
    }
    if (processor->getNonFiniteSampleCount() != 0)
        report(seed, "processor sanitizer saw non-finite samples: " + std::to_string(processor->getNonFiniteSampleCount()));
    if (worst > 64.0f)
        report(seed, "excessive peak " + std::to_string(worst));

    // state round trip must load into a fresh processor
    juce::MemoryBlock state;
    processor->getStateInformation(state);
    GrooveMorphAudioProcessor fresh;
    fresh.setStateInformation(state.getData(), (int) state.getSize());
    juce::MemoryBlock again;
    fresh.getStateInformation(again);
    if (again.getSize() == 0)
        report(seed, "state round trip produced empty state");
    std::cout << "seed " << seed << " sr=" << sr << " blk=" << maxBlock << (mono ? " mono" : " stereo")
              << " peak=" << worst << " worstBlock%=" << (int) slowest << " worstMs=" << slowestMs << '\n';
    processor->releaseResources();
}

// Loop capture / import / stretch / print cycles with odd and corrupt files.
void runModes(std::uint64_t seed)
{
    juce::Random r((juce::int64) (seed * 0x9e3779b97f4a7c15ull + 4242));
    for (int w = 0; w < 8; ++w) (void) r.nextInt();
    const double sr = std::vector<double>{22050.0, 44100.0, 48000.0, 96000.0}[(std::size_t) r.nextInt(4)];
    const int blockSize = std::vector<int>{64, 256, 512, 1024}[(std::size_t) r.nextInt(4)];
    GrooveMorphAudioProcessor p;
    FakePlayHead ph;
    p.setPlayHead(&ph);
    p.prepareToPlay(sr, blockSize);
    const double bpm = 70.0 + r.nextInt(100);
    std::int64_t pos = 0;
    juce::AudioBuffer<float> buf(2, blockSize);
    juce::MidiBuffer midi;
    auto run = [&](double secs, bool play = true) {
        const auto total = static_cast<std::int64_t>(secs * sr);
        for (std::int64_t d = 0; d < total; d += blockSize)
        {
            fillInput(buf, r, 5);
            setTransport(ph, r, sr, pos, play, bpm, 4, 4);
            midi.clear();
            p.processBlock(buf, midi);
            if (play) pos += blockSize;
            for (int c = 0; c < 2; ++c)
                for (int i = 0; i < blockSize; ++i)
                    if (!std::isfinite(buf.getSample(c, i)))
                    {
                        report(seed, "mode run: non-finite output");
                        return;
                    }
            ++totalBlocks;
        }
    };
    run(0.5);
    for (auto* prm : p.getParameters())
        if (prm->getName(32) != "Bypass") prm->setValueNotifyingHost(r.nextFloat());
    if (auto* stretch = p.parameters.getParameter("loopStretch")) stretch->setValueNotifyingHost(r.nextBool() ? 1.0f : 0.0f);
    for (std::size_t i = 0; i < 4; ++i) p.setVariationSettings(i, randomScene(r));

    // 1. imports: valid, odd and corrupt files
    const auto dir = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("gm-fuzz-" + juce::String((juce::int64) seed));
    dir.createDirectory();
    int imported = 0;
    for (int k = 0; k < 6; ++k)
    {
        const auto f = dir.getChildFile("f" + juce::String(k) + ".wav");
        f.deleteFile();
        if (k == 0) { f.replaceWithData("garbage", 7); }
        else if (k == 1) { juce::MemoryBlock junk(4096); for (size_t i = 0; i < junk.getSize(); ++i) junk[i] = (char) r.nextInt(256); f.replaceWithData(junk.getData(), junk.getSize()); }
        else
        {
            const double fsr = std::vector<double>{8000.0, 44100.0, 48000.0, 96000.0}[(std::size_t) r.nextInt(4)];
            const double len = k == 2 ? 0.0002 : k == 3 ? 25.0 : 0.3 + 6.0 * r.nextDouble();
            const int ch = r.nextBool() ? 1 : 2;
            juce::WavAudioFormat fmt;
            std::unique_ptr<juce::FileOutputStream> os(f.createOutputStream());
            std::unique_ptr<juce::AudioFormatWriter> w(fmt.createWriterFor(os.get(), fsr, (unsigned) ch, std::vector<int>{16, 24, 32}[(std::size_t) r.nextInt(3)], {}, 0));
            if (w != nullptr)
            {
                os.release();
                const int n = std::max(1, (int) (len * fsr));
                juce::AudioBuffer<float> b(ch, n);
                for (int c = 0; c < ch; ++c)
                    for (int i = 0; i < n; ++i)
                        b.setSample(c, i, 0.7f * std::sin(0.01f * (float) i * (float) (c + 1)) * std::exp(-(float) (i % 9000) / 3000.0f));
                w->writeFromAudioSampleBuffer(b, 0, n);
            }
        }
        const auto result = p.importLoopFile(f, r.nextInt(18), r.nextBool() ? 0.0 : 60.0 + r.nextInt(120), r.nextBool() ? 0.0 : 0.5 + r.nextInt(4));
        if (result.wasOk()) ++imported;
        p.pollLoopCapture();
        p.updateLoopStretch();
        run(0.4 + r.nextDouble());
        if (r.nextBool()) p.setSourceMode(r.nextInt(3));
    }
    dir.deleteRecursively();

    // 2. loop capture cycle
    p.clearLoop();
    p.setSourceMode(0);
    const int capBars = 1 + r.nextInt(3);
    const bool armed = p.armLoopCapture(capBars);
    run((capBars + 1) * 4 * 60.0 / bpm + 2.5); // capture starts on the next bar line
    p.pollLoopCapture();
    if (armed && !p.hasLoop() && p.getLoopCaptureFailure() == 0 && capBars * 4 * 60.0 / bpm < 19.0)
        report(seed, "loop capture armed but produced neither a loop nor a failure reason (bars=" + std::to_string(capBars) + ", bpm=" + std::to_string(bpm) + ")");
    for (int k = 0; k < 3; ++k) { p.updateLoopStretch(); run(0.3); }

    // 3. print cycle
    const int printBars = 1 + r.nextInt(4);
    const bool printArmed = p.armPrint(printBars, r.nextBool(), r.nextBool());
    GrooveMorphAudioProcessor::PrintResult res;
    bool got = false;
    const double budget = printBars * 4 * 60.0 / bpm + p.getPrintTailSeconds() + 4.0;
    for (double t = 0; t < budget && !got; t += 0.25)
    {
        run(0.25);
        got = p.takePrintResult(res);
    }
    if (printArmed && !got && p.getPrintFailure() == groovemorph::core::OutputCapture::Failure::None)
        report(seed, "print armed (" + std::to_string(printBars) + " bars) but no result and no failure within the expected time");
    if (got)
    {
        for (int c = 0; c < res.audio.getNumChannels(); ++c)
            for (int i = 0; i < res.audio.getNumSamples(); ++i)
                if (!std::isfinite(res.audio.getSample(c, i)))
                {
                    report(seed, "printed audio non-finite");
                    c = res.audio.getNumChannels();
                    break;
                }
        const double expectBody = printBars * 4 * 60.0 / bpm * res.sampleRate;
        if (std::abs(res.bodySamples - expectBody) > res.sampleRate * 0.05)
            report(seed, "print body length " + std::to_string(res.bodySamples) + " expected ~" + std::to_string((long) expectBody));
    }
    // 4. stop/start with all of it live
    run(0.3, false);
    run(0.3);
    std::cout << "modes seed " << seed << " sr=" << sr << " imported=" << imported << " loop=" << p.hasLoop() << " print=" << got << '\n';
}
} // namespace

int main(int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI gui;
    const int rounds = argc > 1 ? std::atoi(argv[1]) : 40;
    const std::uint64_t first = argc > 2 ? (std::uint64_t) std::atoll(argv[2]) : 1;
    const double seconds = argc > 3 ? std::atof(argv[3]) : 6.0;
    for (int i = 0; i < rounds; ++i)
        runRound(first + (std::uint64_t) i, seconds);
    for (int i = 0; i < rounds; ++i)
        runModes(first + (std::uint64_t) i);
    std::cout << "fuzz rounds=" << rounds << " blocks=" << totalBlocks << " failures=" << failures << '\n';
    return failures == 0 ? 0 : 1;
}
