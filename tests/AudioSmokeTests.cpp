// A4 offline audio smoke tests: the demo loop (and an imported loop, stretch and print paths) through the
// real processor across sample rates, block sizes, each effect alone and together, chaos levels and hostile
// transport behavior. Checks: finite output, bounded peak, exact dry at intensity 0, deterministic repeats,
// no crash under stop/start/seek storms.
#include "PluginProcessor.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace
{
int failures = 0;
int checks = 0;

void expect(const std::string& name, bool condition)
{
    ++checks;
    if (!condition)
    {
        std::cerr << "FAIL: " << name << '\n' << std::flush;
        ++failures;
    }
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

void setTransport(FakePlayHead& playHead, double sampleRate, std::int64_t sample, bool playing = true, double bpm = 120.0)
{
    const double ppq = static_cast<double>(sample) / sampleRate * bpm / 60.0;
    playHead.info.setBpm(bpm);
    playHead.info.setTimeSignature(juce::AudioPlayHead::TimeSignature{4, 4});
    playHead.info.setIsPlaying(playing);
    playHead.info.setTimeInSamples(sample);
    playHead.info.setPpqPosition(ppq);
    playHead.info.setPpqPositionOfLastBarStart(std::floor(ppq / 4.0) * 4.0);
}

float noiseAt(std::int64_t i, int channel)
{
    std::uint32_t s = static_cast<std::uint32_t>(i) * 2654435761u + static_cast<std::uint32_t>(channel) * 40503u + 12345u;
    s ^= s << 13; s ^= s >> 17; s ^= s << 5;
    const float n = (static_cast<float>(s & 0xFFFF) / 32768.0f - 1.0f) * 0.3f;
    return n + 0.4f * std::sin(0.03f * static_cast<float>(i % 100000));
}

struct Stats
{
    bool finite = true;
    float peak = 0.0f;
    std::uint64_t hash = 1469598103934665603ull;
    void add(float v)
    {
        if (!std::isfinite(v))
            finite = false;
        else
            peak = std::max(peak, std::abs(v));
        std::uint32_t bits;
        std::memcpy(&bits, &v, sizeof bits);
        hash = (hash ^ bits) * 1099511628211ull;
    }
};

using groovemorph::core::OperationType;
using groovemorph::core::VariationSettings;

VariationSettings sceneFor(int effect) // 0..6 = one effect alone, 7 = all seven together
{
    VariationSettings v;
    for (std::size_t op = 0; op < groovemorph::core::operationCount; ++op)
        if (effect == 7 || static_cast<int>(op) == effect)
            v.operations[op] = {true, 1.0f, 0.25f};
    return v;
}

struct Config
{
    double sampleRate = 48000.0;
    int blockSize = 512;
    int effect = 7;
    float chaos = 0.0f;
    float intensity = 1.0f;
    int source = 1; // 0 input, 1 demo loop
    double seconds = 4.5; // a bit more than two bars at 120 BPM
};

// Renders a configuration through a fresh processor; `input` is deterministic noise-plus-tone.
Stats render(const Config& c, std::vector<float>* capture = nullptr, std::vector<float>* inputCapture = nullptr)
{
    GrooveMorphAudioProcessor processor;
    FakePlayHead playHead;
    processor.setPlayHead(&playHead);
    processor.prepareToPlay(c.sampleRate, c.blockSize);
    processor.setVariationSettings(0, sceneFor(c.effect));
    setParameter(processor, "masterIntensity", c.intensity);
    setParameter(processor, "chaos", c.chaos);
    processor.setSourceMode(c.source);
    Stats stats;
    const std::int64_t total = static_cast<std::int64_t>(c.seconds * c.sampleRate);
    juce::AudioBuffer<float> buffer(2, c.blockSize);
    juce::MidiBuffer midi;
    for (std::int64_t at = 0; at < total; at += c.blockSize)
    {
        setTransport(playHead, c.sampleRate, at);
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < c.blockSize; ++i)
                buffer.setSample(ch, i, noiseAt(at + i, ch));
        if (inputCapture != nullptr)
            for (int i = 0; i < c.blockSize; ++i)
                inputCapture->push_back(buffer.getSample(0, i));
        processor.processBlock(buffer, midi);
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < c.blockSize; ++i)
                stats.add(buffer.getSample(ch, i));
        if (capture != nullptr)
            for (int i = 0; i < c.blockSize; ++i)
                capture->push_back(buffer.getSample(0, i));
    }
    processor.setPlayHead(nullptr);
    return stats;
}

std::string describe(const Config& c)
{
    return std::to_string(static_cast<int>(c.sampleRate)) + "Hz/" + std::to_string(c.blockSize) + "/fx" + std::to_string(c.effect)
           + "/chaos" + std::to_string(c.chaos);
}

void matrix()
{
    const std::vector<double> rates{44100.0, 48000.0, 96000.0};
    const std::vector<int> blocks{1, 64, 512, 4096};
    for (const double sr : rates)
        for (const int bs : blocks)
            for (int effect = 0; effect <= 7; ++effect)
                for (const float chaos : {0.0f, 0.5f, 1.0f})
                {
                    Config c;
                    c.sampleRate = sr;
                    c.blockSize = bs;
                    c.effect = effect;
                    c.chaos = chaos;
                    c.seconds = bs == 1 ? 2.2 : 4.5; // one-sample blocks are slow; still crosses the first bar
                    const auto s = render(c);
                    const auto name = describe(c);
                    expect("finite " + name, s.finite);
                    expect("bounded peak " + name + " (" + std::to_string(s.peak) + ")", s.peak <= 4.0f);
                }
    std::cerr << "matrix done\n";
}

void dryAndDeterminism()
{
    for (const double sr : {44100.0, 48000.0, 96000.0})
        for (const int bs : {1, 64, 512, 4096})
            for (int effect = 0; effect <= 7; ++effect)
            {
                Config c;
                c.sampleRate = sr;
                c.blockSize = bs;
                c.effect = effect;
                c.intensity = 0.0f;
                c.source = 0;
                c.seconds = bs == 1 ? 1.0 : 2.5;
                std::vector<float> out, in;
                render(c, &out, &in);
                bool exact = out.size() == in.size();
                for (std::size_t i = 0; exact && i < out.size(); ++i)
                    exact = out[i] == in[i];
                expect("bit-exact at intensity 0 " + describe(c), exact);
            }
    for (const int bs : {64, 512})
        for (int effect : {2, 7})
            for (const float chaos : {0.0f, 1.0f})
            {
                Config c;
                c.blockSize = bs;
                c.effect = effect;
                c.chaos = chaos;
                c.seconds = 3.0;
                const auto a = render(c);
                const auto b = render(c);
                expect("deterministic repeat " + describe(c), a.hash == b.hash);
            }
    std::cerr << "dry/determinism done\n";
}

void bypassParameter()
{
    GrooveMorphAudioProcessor processor;
    FakePlayHead playHead;
    processor.setPlayHead(&playHead);
    processor.prepareToPlay(48000.0, 256);
    processor.setVariationSettings(0, sceneFor(7));
    setParameter(processor, "masterIntensity", 1.0f);
    setParameter(processor, "chaos", 1.0f);
    processor.getBypassParameter()->setValueNotifyingHost(1.0f);
    bool exact = true;
    std::int64_t at = 0;
    juce::AudioBuffer<float> buffer(2, 256);
    juce::MidiBuffer midi;
    for (int block = 0; block < 100; ++block, at += 256)
    {
        setTransport(playHead, 48000.0, at);
        for (int i = 0; i < 256; ++i)
            for (int ch = 0; ch < 2; ++ch)
                buffer.setSample(ch, i, noiseAt(at + i, ch));
        processor.processBlock(buffer, midi);
        if (block >= 10) // after the bypass crossfade
            for (int i = 0; i < 256; ++i)
                exact = exact && buffer.getSample(0, i) == noiseAt(at + i, 0);
    }
    expect("host bypass reaches bit-exact dry with every effect and chaos on", exact);
}

void transportStorms()
{
    for (const int bs : {1, 64, 512, 4096})
        for (const double sr : {44100.0, 96000.0})
        {
            GrooveMorphAudioProcessor processor;
            FakePlayHead playHead;
            processor.setPlayHead(&playHead);
            processor.prepareToPlay(sr, bs);
            processor.setVariationSettings(0, sceneFor(7));
            processor.setVariationSettings(1, sceneFor(2));
            setParameter(processor, "masterIntensity", 1.0f);
            setParameter(processor, "chaos", 0.8f);
            processor.setSourceMode(1);
            std::uint32_t rng = 12345u + static_cast<std::uint32_t>(bs);
            const auto next = [&] { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return rng; };
            Stats stats;
            juce::AudioBuffer<float> buffer(2, bs);
            juce::MidiBuffer midi;
            std::int64_t at = 0;
            const int blocks = bs == 1 ? 12000 : (bs == 4096 ? 120 : 600);
            for (int block = 0; block < blocks; ++block)
            {
                const auto r = next() % 10;
                const bool playing = r > 2;
                if (r == 0)
                    at = static_cast<std::int64_t>(next() % 4000000);   // seek anywhere
                else if (r == 1)
                    at = 0;                                              // rewind
                else if (r == 2 && at > 100)
                    at -= 100;                                           // tiny backwards jump
                else
                    at += bs;
                const double bpm = (next() % 7 == 0) ? 60.0 + static_cast<double>(next() % 180) : 120.0;
                if (next() % 97 == 0)
                    processor.setPlayHead(nullptr);
                else
                {
                    processor.setPlayHead(&playHead);
                    setTransport(playHead, sr, at, playing, bpm);
                }
                if (next() % 53 == 0)
                    setParameter(processor, "activeVariation", static_cast<float>(next() % 4));
                if (next() % 71 == 0)
                    processor.requestVariation(next() % 4);
                if (next() % 211 == 0)
                    processor.triggerManualSwitch();
                for (int ch = 0; ch < 2; ++ch)
                    for (int i = 0; i < bs; ++i)
                        buffer.setSample(ch, i, noiseAt(at + i, ch));
                processor.processBlock(buffer, midi);
                for (int ch = 0; ch < 2; ++ch)
                    for (int i = 0; i < bs; ++i)
                        stats.add(buffer.getSample(ch, i));
            }
            const auto name = "storm " + std::to_string(bs) + "/" + std::to_string(static_cast<int>(sr));
            expect(name + " finite", stats.finite);
            expect(name + " bounded (" + std::to_string(stats.peak) + ")", stats.peak <= 4.0f);
            processor.setPlayHead(nullptr);
        }
    std::cerr << "storms done\n";
}

// A short synthetic loop: decaying clicks on a tone, 2 s at 44.1 kHz so import has to resample.
juce::File makeLoopWav()
{
    const auto file = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("gm-smoke-loop.wav");
    file.deleteFile();
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::FileOutputStream> stream(file.createOutputStream());
    std::unique_ptr<juce::AudioFormatWriter> writer(wav.createWriterFor(stream.get(), 44100.0, 2, 24, {}, 0));
    if (writer != nullptr)
    {
        stream.release();
        const int n = 88200;
        juce::AudioBuffer<float> b(2, n);
        for (int i = 0; i < n; ++i)
        {
            const float env = std::exp(-static_cast<float>(i % 11025) / 1800.0f);
            const float v = 0.8f * env * std::sin(0.05f * static_cast<float>(i));
            b.setSample(0, i, v);
            b.setSample(1, i, -v);
        }
        writer->writeFromAudioSampleBuffer(b, 0, n);
    }
    return file;
}

void loopPaths()
{
    const auto wav = makeLoopWav();
    for (const bool stretch : {false, true})
        for (const double sr : {44100.0, 48000.0, 96000.0})
            for (const double hostBpm : {90.0, 120.0, 150.0})
                for (const int bs : {64, 4096})
                {
                    GrooveMorphAudioProcessor processor;
                    FakePlayHead playHead;
                    processor.setPlayHead(&playHead);
                    processor.prepareToPlay(sr, bs);
                    const auto r = processor.importLoopFile(wav, 0, 0.0);
                    expect("loop imports", r.wasOk() && processor.hasLoop());
                    processor.setSourceMode(2);
                    processor.setVariationSettings(0, sceneFor(7));
                    setParameter(processor, "masterIntensity", 1.0f);
                    setParameter(processor, "chaos", 0.5f);
                    setParameter(processor, "loopStretch", stretch ? 1.0f : 0.0f);
                    Stats stats;
                    juce::AudioBuffer<float> buffer(2, bs);
                    juce::MidiBuffer midi;
                    const std::int64_t total = static_cast<std::int64_t>(5.0 * sr);
                    int tick = 0;
                    for (std::int64_t at = 0; at < total; at += bs)
                    {
                        setTransport(playHead, sr, at, true, hostBpm);
                        buffer.clear();
                        processor.processBlock(buffer, midi);
                        if (++tick % 8 == 0)
                        {
                            processor.updateLoopStretch(); // normally the processor timer
                            if (stretch)
                                std::this_thread::sleep_for(std::chrono::milliseconds(2));
                        }
                        for (int ch = 0; ch < 2; ++ch)
                            for (int i = 0; i < bs; ++i)
                                stats.add(buffer.getSample(ch, i));
                    }
                    const auto name = std::string("loop stretch=") + (stretch ? "on" : "off") + " " + std::to_string(static_cast<int>(sr))
                                      + "Hz " + std::to_string(static_cast<int>(hostBpm)) + "bpm/" + std::to_string(bs);
                    expect(name + " finite", stats.finite);
                    expect(name + " bounded (" + std::to_string(stats.peak) + ")", stats.peak <= 4.0f);
                    expect(name + " audible", stats.peak > 0.01f);
                    processor.setPlayHead(nullptr);
                }

    // Print path: wet and dry capture, with and without a tail, every effect on, chaos on.
    for (const bool wetOnly : {false, true})
        for (const bool tail : {false, true})
            for (const double sr : {44100.0, 96000.0})
            {
                GrooveMorphAudioProcessor processor;
                FakePlayHead playHead;
                processor.setPlayHead(&playHead);
                const int bs = 512;
                processor.prepareToPlay(sr, bs);
                processor.setVariationSettings(0, sceneFor(7));
                setParameter(processor, "masterIntensity", 1.0f);
                setParameter(processor, "chaos", 1.0f);
                processor.setSourceMode(1);
                const bool armed = processor.armPrint(1, wetOnly, tail);
                expect("print arms", armed);
                const double barSamples = 2.0 * sr;
                const std::int64_t total = static_cast<std::int64_t>(barSamples * 5.0 + 6.0 * sr);
                juce::AudioBuffer<float> buffer(2, bs);
                juce::MidiBuffer midi;
                for (std::int64_t at = 0; at < total; at += bs)
                {
                    setTransport(playHead, sr, at);
                    buffer.clear();
                    processor.processBlock(buffer, midi);
                }
                GrooveMorphAudioProcessor::PrintResult result;
                const bool got = processor.takePrintResult(result);
                const auto name = std::string("print wet=") + (wetOnly ? "1" : "0") + " tail=" + (tail ? "1" : "0") + " " + std::to_string(static_cast<int>(sr));
                expect(name + " collected", got);
                if (got)
                {
                    Stats stats;
                    for (int ch = 0; ch < result.audio.getNumChannels(); ++ch)
                        for (int i = 0; i < result.audio.getNumSamples(); ++i)
                            stats.add(result.audio.getSample(ch, i));
                    expect(name + " finite", stats.finite);
                    expect(name + " bounded (" + std::to_string(stats.peak) + ")", stats.peak <= 4.0f);
                    expect(name + " one bar body", std::abs(result.bodySamples - static_cast<int>(barSamples)) <= 2);
                }
                processor.setPlayHead(nullptr);
            }
    wav.deleteFile();
    std::cerr << "loop/print done\n";
}
} // namespace

int main()
{
    juce::ScopedJuceInitialiser_GUI juceInitialiser;
    std::thread worker([] {
        dryAndDeterminism();
        bypassParameter();
        transportStorms();
        loopPaths();
        matrix();
        std::cerr << "AudioSmokeTests: " << checks << " checks, " << failures << " failures\n";
        juce::MessageManager::callAsync([] { juce::MessageManager::getInstance()->stopDispatchLoop(); });
    });
    juce::MessageManager::getInstance()->runDispatchLoop();
    worker.join();
    return failures == 0 ? 0 : 1;
}
