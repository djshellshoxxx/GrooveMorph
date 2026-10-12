#include "PluginProcessor.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <iostream>
#include <string_view>

using groovemorph::core::TimingSource;
namespace Missing = groovemorph::core::MissingField;

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

void processSilence(GrooveMorphAudioProcessor& processor, int samples)
{
    juce::AudioBuffer<float> buffer(2, samples);
    buffer.clear();
    juce::MidiBuffer midi;
    processor.processBlock(buffer, midi);
}
} // namespace

int main()
{
    juce::ScopedJuceInitialiser_GUI juceInitialiser;

    {
        GrooveMorphAudioProcessor processor;
        processor.prepareToPlay(48000.0, 256);
        expect("zero latency", processor.getLatencySamples() == 0);

        auto layout = processor.getBusesLayout();
        expect("stereo layout supported", processor.checkBusesLayoutSupported(layout));
        layout.inputBuses.getReference(0) = juce::AudioChannelSet::mono();
        layout.outputBuses.getReference(0) = juce::AudioChannelSet::mono();
        expect("mono layout supported", processor.checkBusesLayoutSupported(layout));
        layout.outputBuses.getReference(0) = juce::AudioChannelSet::stereo();
        expect("mono-to-stereo rejected", !processor.checkBusesLayoutSupported(layout));
    }

    {
        GrooveMorphAudioProcessor source;
        setParameter(source, "masterIntensity", 0.25f);
        setParameter(source, "manualTempo", 97.5f);
        setParameter(source, "manualMeterNumerator", 7.0f);
        setParameter(source, "phraseBars", 8.0f);
        juce::MemoryBlock state;
        source.getStateInformation(state);

        GrooveMorphAudioProcessor restored;
        restored.setStateInformation(state.getData(), static_cast<int>(state.getSize()));
        const auto value = [&restored](const char* id) {
            return restored.parameters.getRawParameterValue(id)->load();
        };
        expect("state restores intensity", std::abs(value("masterIntensity") - 0.25f) < 1.0e-4f);
        expect("state restores tempo", std::abs(value("manualTempo") - 97.5f) < 1.0e-2f);
        expect("state restores meter", value("manualMeterNumerator") == 7.0f);
        expect("state restores phrase bars", value("phraseBars") == 8.0f);

        const char garbage[] = "not a state";
        restored.setStateInformation(garbage, sizeof(garbage));
        expect("malformed state ignored", std::abs(value("manualTempo") - 97.5f) < 1.0e-2f);
    }

    {
        GrooveMorphAudioProcessor processor;
        expect("not prepared before prepareToPlay", !processor.isPrepared());
        processor.prepareToPlay(48000.0, 256);
        processSilence(processor, 256);
        expect("no playhead is manual", processor.hasValidTiming()
                                            && processor.getTimingSource() == TimingSource::Manual);

        FakePlayHead playHead;
        playHead.info.setBpm(128.0);
        playHead.info.setTimeSignature(juce::AudioPlayHead::TimeSignature{3, 4});
        playHead.info.setIsPlaying(true);
        playHead.info.setTimeInSamples(1024);
        processor.setPlayHead(&playHead);
        processSilence(processor, 256);
        expect("full playhead is host", processor.getTimingSource() == TimingSource::Host
                                            && processor.getMissingTimingFields() == 0);

        playHead.info.setTimeSignature(juce::nullopt);
        processSilence(processor, 256);
        expect("missing meter reported", processor.getTimingSource() == TimingSource::Mixed
                                             && processor.getMissingTimingFields() == Missing::meter);
        processor.setPlayHead(nullptr);
        processor.releaseResources();
        expect("not prepared after release", !processor.isPrepared());
    }

    // GM-FR-005: intensity 0 is the dry path, bit for bit, through processBlock.
    {
        GrooveMorphAudioProcessor processor;
        setParameter(processor, "masterIntensity", 0.0f);
        processor.prepareToPlay(48000.0, 256);
        FakePlayHead playHead;
        processor.setPlayHead(&playHead);
        juce::Random random(1234);
        bool identical = true;
        for (int block = 0; block < 40; ++block)
        {
            setTransport(playHead, block * 256);
            juce::AudioBuffer<float> buffer(2, 256);
            for (int c = 0; c < 2; ++c)
                for (int i = 0; i < 256; ++i)
                    buffer.setSample(c, i, random.nextFloat() * 2.0f - 1.0f);
            juce::AudioBuffer<float> input(buffer);
            juce::MidiBuffer midi;
            processor.processBlock(buffer, midi);
            for (int c = 0; c < 2; ++c)
                identical = identical
                            && std::memcmp(buffer.getReadPointer(c), input.getReadPointer(c),
                                           sizeof(float) * 256) == 0;
        }
        expect("intensity 0 null test", identical);

        // Nonzero intensity actually transforms (sanity check that the null test is meaningful).
        setParameter(processor, "masterIntensity", 1.0f);
        float maxDiff = 0.0f;
        for (int block = 40; block < 80; ++block)
        {
            setTransport(playHead, block * 256);
            juce::AudioBuffer<float> buffer(2, 256);
            for (int i = 0; i < 256; ++i)
                buffer.setSample(0, i, std::sin(0.05f * static_cast<float>(block * 256 + i)));
            buffer.copyFrom(1, 0, buffer, 0, 0, 256);
            juce::AudioBuffer<float> input(buffer);
            juce::MidiBuffer midi;
            processor.processBlock(buffer, midi);
            for (int i = 0; i < 256; ++i)
                maxDiff = std::max(maxDiff, std::abs(buffer.getSample(0, i) - input.getSample(0, i)));
        }
        expect("intensity 1 transforms", maxDiff > 0.01f);
        processor.setPlayHead(nullptr);
    }

    // GM-FR-004/007: automated variation change queues and lands on the next host bar.
    {
        GrooveMorphAudioProcessor processor;
        processor.prepareToPlay(48000.0, 512);
        FakePlayHead playHead;
        processor.setPlayHead(&playHead);
        setParameter(processor, "transitionMode", 2.0f); // Next Bar
        std::int64_t sample = 90000;
        setTransport(playHead, sample);
        processSilence(processor, 512);
        expect("starts on A", processor.getActiveVariation() == 0 && processor.getQueuedVariation() < 0);

        setParameter(processor, "activeVariation", 1.0f);
        sample += 512;
        setTransport(playHead, sample);
        processSilence(processor, 512);
        expect("param change queues", processor.getQueuedVariation() == 1 && processor.getActiveVariation() == 0);
        expect("samples until switch", processor.getSamplesUntilSwitch() == 96000 - (sample + 512));
        while (sample < 100000)
        {
            sample += 512;
            setTransport(playHead, sample);
            processSilence(processor, 512);
        }
        expect("switch on bar", processor.getActiveVariation() == 1 && processor.getLastSwitchSample() == 96000);
        expect("queue cleared", processor.getQueuedVariation() < 0 && processor.getSamplesUntilSwitch() < 0);

        // Manual mode: UI request waits for trigger, which switches at the next block start.
        setParameter(processor, "transitionMode", 4.0f);
        processor.requestVariation(3);
        sample += 512;
        setTransport(playHead, sample);
        processSilence(processor, 512);
        expect("manual queued", processor.getQueuedVariation() == 3 && processor.getSamplesUntilSwitch() < 0);
        processor.triggerManualSwitch();
        sample += 512;
        setTransport(playHead, sample);
        processSilence(processor, 512);
        expect("manual trigger", processor.getActiveVariation() == 3 && processor.getLastSwitchSample() == sample);
        processor.setPlayHead(nullptr);
    }

    // GM-FR-008: MIDI note-ons switch at their sample position or queue.
    {
        GrooveMorphAudioProcessor processor;
        processor.prepareToPlay(48000.0, 512);
        FakePlayHead playHead;
        processor.setPlayHead(&playHead);
        setParameter(processor, "transitionMode", 0.0f); // Immediate
        std::int64_t sample = 4800;
        setTransport(playHead, sample);
        {
            juce::AudioBuffer<float> buffer(2, 512);
            buffer.clear();
            juce::MidiBuffer midi;
            midi.addEvent(juce::MidiMessage::noteOn(1, 62, 0.8f), 100);
            processor.processBlock(buffer, midi);
        }
        expect("midi immediate at offset", processor.getActiveVariation() == 2
                                               && processor.getLastSwitchSample() == sample + 100);
        expect("midi counted", processor.getMidiNoteEventCount() == 1);

        setParameter(processor, "transitionMode", 2.0f); // Next Bar
        sample += 512;
        setTransport(playHead, sample);
        {
            juce::AudioBuffer<float> buffer(2, 512);
            buffer.clear();
            juce::MidiBuffer midi;
            midi.addEvent(juce::MidiMessage::noteOn(1, 63, 0.8f), 10);
            midi.addEvent(juce::MidiMessage::noteOff(1, 63), 20);
            midi.addEvent(juce::MidiMessage::noteOn(1, 99, 0.8f), 30); // unmapped
            processor.processBlock(buffer, midi);
        }
        expect("midi queues next bar", processor.getQueuedVariation() == 3 && processor.getActiveVariation() == 2);
        expect("note-off ignored by default", processor.getQueuedVariation() == 3);
        processor.setPlayHead(nullptr);
    }

    // GM-FR-010: state round trip includes scene edits and MIDI map; malformed scenes default.
    {
        using groovemorph::core::OperationType;
        namespace scene = groovemorph::core::scene;
        GrooveMorphAudioProcessor source;
        source.setOperationSettings(1, OperationType::Gate, {true, 0.3f, 0.5f});
        source.setVariationName(2, "Bridge");
        auto map = source.getSceneBank().midiNoteMap();
        map.notes = {36, 38, 40, 41};
        map.noteOffPolicy = scene::NoteOffPolicy::ReturnToPrevious;
        source.setMidiNoteMap(map);
        source.setSceneSeed(777);
        expect("edits undoable", source.canUndoSceneEdit());
        expect("undo works", source.undoSceneEdit() && source.getSceneBank().seed() != 777);
        expect("redo works", source.redoSceneEdit() && source.getSceneBank().seed() == 777);

        source.beginAudition(0);
        auto edited = source.getSceneBank().variation(0);
        edited.settings[OperationType::TapeStop].enabled = true;
        source.setAuditionEdit(edited);
        expect("audition dirty", source.isAuditioning() && source.isAuditionDirty());
        expect("audition does not touch bank",
               source.getSceneBank().variation(0) == scene::SceneBank::defaultVariation(0));
        expect("audition commit", source.commitAudition() && !source.isAuditioning());

        juce::MemoryBlock state;
        source.getStateInformation(state);
        GrooveMorphAudioProcessor restored;
        restored.setStateInformation(state.getData(), static_cast<int>(state.getSize()));
        expect("scene bank round trip", restored.getSceneBank() == source.getSceneBank());
        expect("scene load ok", restored.getLastSceneLoadReport().status == scene::LoadStatus::Ok);
        expect("bypass parameter exposed", restored.getBypassParameter() != nullptr);

        auto xml = juce::parseXML(juce::String("<GrooveMorphState><SceneBank data=\"groovemorph-scenes v99&#10;junk\"/></GrooveMorphState>"));
        juce::MemoryBlock bad;
        juce::AudioProcessor::copyXmlToBinary(*xml, bad);
        restored.setStateInformation(bad.getData(), static_cast<int>(bad.getSize()));
        expect("malformed scene defaults", restored.getSceneBank() == scene::SceneBank{});
        expect("malformed scene reported", restored.getLastSceneLoadReport().status != scene::LoadStatus::Ok);
    }

    // GM-FR-012: host bypass crossfades to the exact dry path.
    {
        GrooveMorphAudioProcessor processor;
        setParameter(processor, "masterIntensity", 1.0f);
        setParameter(processor, "activeVariation", 1.0f); // B: gate
        setParameter(processor, "transitionMode", 0.0f);
        processor.prepareToPlay(48000.0, 64);
        FakePlayHead playHead;
        processor.setPlayHead(&playHead);
        const auto sine = [](std::int64_t n) { return 0.5f * std::sin(0.01f * static_cast<float>(n)); };
        std::int64_t sample = 0;
        float maxStep = 0.0f;
        float previous = 0.0f;
        bool dryAfterRamp = true;
        bool rampedGradually = false;
        for (int block = 0; block < 150; ++block)
        {
            if (block == 71) // gate B (1/8 beat = 3000 samples, open first half) is closed at 4544
                processor.getBypassParameter()->setValueNotifyingHost(1.0f);
            setTransport(playHead, sample);
            juce::AudioBuffer<float> buffer(2, 64);
            for (int i = 0; i < 64; ++i)
                buffer.setSample(0, i, sine(sample + i));
            buffer.copyFrom(1, 0, buffer, 0, 0, 64);
            juce::MidiBuffer midi;
            processor.processBlock(buffer, midi);
            for (int i = 0; i < 64; ++i)
            {
                const float out = buffer.getSample(0, i);
                if (block >= 71)
                {
                    maxStep = std::max(maxStep, std::abs(out - previous));
                    const auto sinceBypass = sample + i - 4544;
                    if (sinceBypass >= 480 && out != sine(sample + i))
                        dryAfterRamp = false;
                    if (sinceBypass == 240 && std::abs(out - sine(sample + i)) > 1.0e-3f
                        && std::abs(out) > 1.0e-3f)
                        rampedGradually = true;
                }
                previous = out;
            }
            sample += 64;
        }
        expect("bypass reaches exact dry", dryAfterRamp);
        expect("bypass ramps", rampedGradually);
        expect("bypass click-free", maxStep < 0.01f);
        processor.setPlayHead(nullptr);
    }

    if (failures == 0)
        std::cout << "PluginProcessorTests: all checks passed\n";
    return failures == 0 ? 0 : 1;
}
