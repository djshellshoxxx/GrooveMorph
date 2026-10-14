#include "PluginProcessor.h"
#include "PluginEditor.h"

#include <cstdlib>

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
        // Repeat/reverse/rate effects can emit causal history after the source
        // falls silent. Tell DAWs to retain their render tail without introducing PDC.
        expect("history effects advertise their bounded render tail",
               processor.getTailLengthSeconds() >= 4.0 &&
               processor.getTailLengthSeconds() <= 4.02);

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
        setParameter(source, "timingMode", 2.0f);
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
        expect("state restores timing mode", value("timingMode") == 2.0f);

        auto oldTree = source.parameters.copyState();
        oldTree.removeChild(oldTree.getChildWithProperty("id", "timingMode"), nullptr);
        juce::MemoryBlock legacyState;
        juce::AudioProcessor::copyXmlToBinary(*oldTree.createXml(), legacyState);
        restored.setStateInformation(legacyState.getData(), static_cast<int>(legacyState.getSize()));
        expect("legacy state resets timing to automatic", value("timingMode") == 0.0f);

        const char garbage[] = "not a state";
        restored.setStateInformation(garbage, sizeof(garbage));
        expect("malformed state ignored", std::abs(value("manualTempo") - 97.5f) < 1.0e-2f);

        // Reject an oversized state envelope without mutating valid loaded parameters.
        juce::MemoryBlock oversized;
        oversized.setSize(1024 * 1024 + 1, true);
        restored.setStateInformation(oversized.getData(), static_cast<int>(oversized.getSize()));
        expect("oversized state ignored", std::abs(value("manualTempo") - 97.5f) < 1.0e-2f);
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

    // Scene settings copy is one undoable transaction, independent of host state.
    {
        GrooveMorphAudioProcessor processor;
        const auto original = processor.getSceneBank();
        expect("copy A effects into B", processor.copyVariationSettings(0, 1));
        const auto copied = processor.getSceneBank();
        expect("processor copy changes only destination effects",
               groovemorph::core::scene::sameSettings(copied.variation(1).settings,
                                                     original.variation(0).settings));
        expect("processor copy preserves B name and MIDI",
               copied.variation(1).name == original.variation(1).name &&
               copied.midiNoteMap() == original.midiNoteMap());
        expect("processor copy leaves source intact", copied.variation(0) == original.variation(0));
        expect("processor copy can undo", processor.undoSceneEdit() &&
               processor.getSceneBank() == original);
        expect("processor copy can redo", processor.redoSceneEdit() &&
               processor.getSceneBank() == copied);
        expect("copy same effects is no-op", !processor.copyVariationSettings(0, 1));
        processor.beginAudition(1);
        expect("copy blocked during audition", !processor.copyVariationSettings(2, 1));
        processor.revertAudition();
        expect("invalid copy rejected", !processor.copyVariationSettings(0, 99));
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

    // GM-AT-004: sample and PPQ-only host seeks discard audio from the old
    // timeline; scheduled transitions remain intact and quantize afresh.
    for (bool samplesAvailable : {true, false})
    {
        GrooveMorphAudioProcessor processor;
        setParameter(processor, "masterIntensity", 1.0f);
        groovemorph::core::VariationSettings reverse;
        reverse[groovemorph::core::OperationType::ReverseSlice] = {true, 1.0f, 0.25f};
        processor.setVariationSettings(0, reverse);
        processor.prepareToPlay(48000.0, 256);
        FakePlayHead playHead;
        processor.setPlayHead(&playHead);
        juce::MidiBuffer midi;
        for (int block = 0; block < 200; ++block)
        {
            setTransport(playHead, block * 256);
            if (!samplesAvailable)
                playHead.info.setTimeInSamples(juce::nullopt);
            juce::AudioBuffer<float> audio(2, 256);
            for (int channel = 0; channel < 2; ++channel)
                for (int sample = 0; sample < 256; ++sample)
                    audio.setSample(channel, sample, 1.0f);
            processor.processBlock(audio, midi);
        }
        setTransport(playHead, 9000); // middle of a reverse slice, far behind prior block
        if (!samplesAvailable)
            playHead.info.setTimeInSamples(juce::nullopt);
        juce::AudioBuffer<float> silence(2, 256);
        silence.clear();
        processor.processBlock(silence, midi);
        bool staleHistoryAbsent = true;
        for (int channel = 0; channel < 2; ++channel)
            for (int sample = 0; sample < 256; ++sample)
                staleHistoryAbsent = staleHistoryAbsent && silence.getSample(channel, sample) == 0.0f;
        expect(samplesAvailable ? "sample seek clears history" : "PPQ seek clears history", staleHistoryAbsent);
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

    // Musical PPQ must advance to a MIDI-triggered sub-block before quantization.
    {
        GrooveMorphAudioProcessor processor;
        processor.prepareToPlay(48000.0, 8192);
        setParameter(processor, "transitionMode", 2.0f); // Next bar
        FakePlayHead playHead;
        processor.setPlayHead(&playHead);
        setTransport(playHead, 90000); // PPQ 3.75; next bar at sample 96000
        juce::AudioBuffer<float> buffer(2, 8192);
        buffer.clear();
        juce::MidiBuffer midi;
        midi.addEvent(juce::MidiMessage::noteOn(1, 62, 0.8f), 3000);
        processor.processBlock(buffer, midi);
        expect("MIDI sub-block PPQ switch at actual bar", processor.getActiveVariation() == 2
                   && processor.getLastSwitchSample() == 96000);
        processor.setPlayHead(nullptr);
    }

    // Rendering phase must agree with/without unrelated MIDI and max-block splits.
    {
        GrooveMorphAudioProcessor whole, split;
        for (auto* processor : {&whole, &split})
        {
            processor->prepareToPlay(48000.0, 256);
            setParameter(*processor, "masterIntensity", 1.0f);
            auto settings = processor->getSceneBank().variation(0).settings;
            settings[groovemorph::core::OperationType::Gate] = {true, 1.0f, 0.25f};
            processor->setVariationSettings(0, settings);
        }
        FakePlayHead playHead;
        setTransport(playHead, 18000);
        whole.setPlayHead(&playHead);
        split.setPlayHead(&playHead);
        juce::AudioBuffer<float> a(2, 2048), b(2, 2048);
        for (int channel = 0; channel < 2; ++channel)
            for (int sample = 0; sample < 2048; ++sample)
                a.setSample(channel, sample, std::sin(sample * 0.07f));
        b.makeCopyOf(a);
        juce::MidiBuffer none, unrelated;
        unrelated.addEvent(juce::MidiMessage::controllerEvent(1, 1, 64), 333);
        unrelated.addEvent(juce::MidiMessage::controllerEvent(1, 1, 32), 1191);
        whole.processBlock(a, none);
        split.processBlock(b, unrelated);
        float difference = 0.0f;
        for (int channel = 0; channel < 2; ++channel)
            for (int sample = 0; sample < 2048; ++sample)
                difference = std::max(difference, std::abs(a.getSample(channel, sample) - b.getSample(channel, sample)));
        expect("PPQ rendering independent of irrelevant MIDI splits", difference < 1.0e-5f);
        whole.setPlayHead(nullptr);
        split.setPlayHead(nullptr);
    }

    // Exercise the editor's actual callbacks, attachments and preview lifecycle.
    {
        GrooveMorphAudioProcessor processor;
        processor.prepareToPlay(48000.0, 512);
        std::unique_ptr<juce::AudioProcessorEditor> editor(processor.createEditor());
        juce::ComboBox* clockChoice = nullptr;
        juce::TextButton* preview = nullptr;
        juce::TextButton* revert = nullptr;
        juce::ToggleButton* gate = nullptr;
        for (auto* child : editor->getChildren())
        {
            if (auto* box = dynamic_cast<juce::ComboBox*>(child); box && box->getName() == "Timing method")
                clockChoice = box;
            if (auto* button = dynamic_cast<juce::TextButton*>(child))
            {
                if (button->getButtonText() == "Preview") preview = button;
                if (button->getButtonText() == "Revert preview") revert = button;
            }
            if (auto* button = dynamic_cast<juce::ToggleButton*>(child); button && button->getButtonText() == "Gate")
                gate = button;
        }
        expect("editor timing selector and scene controls exist", clockChoice && preview && revert && gate);
        if (clockChoice && preview && revert && gate)
        {
            clockChoice->setSelectedId(3, juce::sendNotificationSync);
            expect("timing selector drives persistent host parameter",
                   processor.parameters.getRawParameterValue("timingMode")->load() == 2.0f);
            processSilence(processor, 512);
            const auto before = processor.getSceneBank();
            preview->onClick();
            gate->setToggleState(!gate->getToggleState(), juce::dontSendNotification);
            gate->onClick();
            expect("editor preview edit remains isolated", processor.isAuditionDirty()
                   && processor.getSceneBank() == before);
            revert->onClick();
            expect("editor revert preserves stored bank", !processor.isAuditioning()
                   && processor.getSceneBank() == before);
            preview->onClick();
        }
        // Optional software-rendered layout evidence for the integration coordinator.
        if (const auto* path = std::getenv("GROOVEMORPH_UI_SNAPSHOT"))
        {
            juce::FileOutputStream output{juce::File{juce::String{path}}};
            output.setPosition(0);
            output.truncate();
            juce::PNGImageFormat png;
            expect("editor snapshot saved", output.openedOk()
                   && png.writeImageToStream(editor->createComponentSnapshot(editor->getLocalBounds()), output));
        }
        editor.reset();
        expect("closing editor ends preview", !processor.isAuditioning());
    }

    // Host PPQ and Sample Clock deliberately select different boundaries.
    for (int clock : {0, 1, 2})
    {
        GrooveMorphAudioProcessor processor;
        processor.prepareToPlay(48000.0, 512);
        setParameter(processor, "timingMode", static_cast<float>(clock));
        setParameter(processor, "transitionMode", 2.0f);
        FakePlayHead playHead;
        setTransport(playHead, 95900);
        playHead.info.setPpqPosition(3.99);
        processor.setPlayHead(&playHead);
        processor.requestVariation(1);
        processSilence(processor, 512);
        expect("processor selected timing clock", processor.getActiveVariation() == 1
               && processor.getLastSwitchSample() == 95900 + (clock == 2 ? 100 : 240));
        processor.setPlayHead(nullptr);
    }
    {
        GrooveMorphAudioProcessor processor;
        processor.prepareToPlay(48000.0, 256);
        setParameter(processor, "timingMode", 2.0f);
        processor.requestVariation(1);
        processSilence(processor, 256);
        expect("standalone explicit sample preview", processor.isTransportPlaying()
               && processor.getActiveVariation() == 1);
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


    // ---- Alpha: host Variation parameter semantics (A02) ----
    {
        GrooveMorphAudioProcessor processor;
        FakePlayHead playHead;
        processor.setPlayHead(&playHead);
        processor.prepareToPlay(48000.0, 256);
        setParameter(processor, "transitionMode", 4.0f); // Manual: requests stay pending
        std::int64_t sample = 0;
        const auto block = [&] {
            setTransport(playHead, sample);
            processSilence(processor, 256);
            sample += 256;
        };
        const auto shown = [&processor] {
            return static_cast<int>(std::lround(processor.parameters.getRawParameterValue("activeVariation")->load()));
        };
        block();
        setParameter(processor, "activeVariation", 1.0f);
        block();
        expect("automation request pending", processor.getQueuedVariation() == 1 && processor.getActiveVariation() == 0);
        processor.syncHostVariationParameter();
        expect("pending request keeps requested value visible", shown() == 1);
        processor.cancelQueuedSwitch();
        block();
        expect("cancel clears queue", processor.getQueuedVariation() == -1);
        processor.syncHostVariationParameter();
        expect("cancel realigns host parameter to sounding variation", shown() == 0);
        block();
        expect("realignment is not a new request", processor.getQueuedVariation() == -1 && processor.getActiveVariation() == 0);
        setParameter(processor, "transitionMode", 0.0f); // Immediate
        processor.requestVariation(3); // GUI/MIDI path
        block();
        expect("GUI request switches", processor.getActiveVariation() == 3);
        expect("param still stale before sync", shown() == 0);
        processor.syncHostVariationParameter();
        expect("GUI switch reflected in host parameter", shown() == 3);
        block();
        block();
        expect("sync does not re-request", processor.getActiveVariation() == 3 && processor.getQueuedVariation() == -1);
        // A value the audio thread has not read yet must never be overwritten.
        setParameter(processor, "activeVariation", 2.0f);
        processor.syncHostVariationParameter();
        expect("unread automation value preserved", shown() == 2);
        block();
        expect("automation applies after sync attempt", processor.getActiveVariation() == 2);
        processor.setPlayHead(nullptr);
    }

    // ---- Alpha: phrase placement gates the chain at the exact boundary (C01) ----
    {
        GrooveMorphAudioProcessor processor;
        FakePlayHead playHead;
        processor.setPlayHead(&playHead);
        const int blockSize = 300; // deliberately misaligned with the 288000-sample boundary
        processor.prepareToPlay(48000.0, blockSize);
        setParameter(processor, "masterIntensity", 1.0f);
        auto settings = processor.getSceneBank().variation(0).settings;
        settings.control.placement.enabled = true;
        settings.control.placement.window = groovemorph::core::control::WindowMode::LastBar;
        processor.setVariationSettings(0, settings);
        juce::Random random(7);
        std::int64_t sample = 0;
        bool dryBeforeWindow = true, wetInWindow = false;
        std::int64_t firstDiff = -1;
        while (sample < 288000 + 30000)
        {
            juce::AudioBuffer<float> buffer(2, blockSize), reference(2, blockSize);
            for (int c = 0; c < 2; ++c)
                for (int i = 0; i < blockSize; ++i)
                    reference.setSample(c, i, buffer.getWritePointer(c)[i] = random.nextFloat() * 0.5f - 0.25f);
            setTransport(playHead, sample);
            juce::MidiBuffer midi;
            processor.processBlock(buffer, midi);
            for (int i = 0; i < blockSize; ++i)
            {
                const bool differs = buffer.getSample(0, i) != reference.getSample(0, i);
                if (sample + i < 288000 && differs)
                    dryBeforeWindow = false;
                if (sample + i >= 288000 && differs)
                {
                    wetInWindow = true;
                    if (firstDiff < 0)
                        firstDiff = sample + i;
                }
            }
            sample += blockSize;
        }
        expect("output is exactly dry before the placement window", dryBeforeWindow);
        expect("effect engages inside the placement window", wetInWindow);
        expect("effect does not engage before the window start", firstDiff >= 288000);
        processor.setPlayHead(nullptr);
    }

    // ---- Alpha: automatic return lands on the bar boundary (C05) ----
    {
        GrooveMorphAudioProcessor processor;
        FakePlayHead playHead;
        processor.setPlayHead(&playHead);
        processor.prepareToPlay(48000.0, 256);
        setParameter(processor, "transitionMode", 0.0f); // Immediate switch to B
        setParameter(processor, "autoReturnBars", 1.0f);
        setParameter(processor, "autoReturnTarget", 0.0f); // Previous
        std::int64_t sample = 0;
        const auto block = [&] {
            setTransport(playHead, sample);
            processSilence(processor, 256);
            sample += 256;
        };
        block();
        processor.requestVariation(1);
        block();
        expect("switched to B", processor.getActiveVariation() == 1);
        expect("return armed", processor.getAutoReturnTarget() == 0);
        bool earlyReturn = false;
        while (sample < 96000 - 256)
        {
            block();
            earlyReturn = earlyReturn || processor.getActiveVariation() != 1;
        }
        expect("stays on B until the bar boundary", !earlyReturn);
        while (sample < 96000 + 512)
            block();
        expect("returned to previous", processor.getActiveVariation() == 0);
        expect("return is sample-accurate at the bar line", processor.getLastSwitchSample() == 96000);
        expect("return disarmed", processor.getAutoReturnTarget() == -1);
        processor.setPlayHead(nullptr);
    }

    // ---- Alpha: scene management and state (A04) ----
    {
        GrooveMorphAudioProcessor processor;
        const auto original = processor.getSceneBank();
        expect("duplicate scene", processor.duplicateVariation(1, 0) && processor.getSceneBank().variation(0) == original.variation(1));
        expect("duplicate undo", processor.undoSceneEdit() && processor.getSceneBank().variation(0) == original.variation(0));
        processor.resetVariation(2);
        expect("reset of default scene is a no-op edit", processor.getSceneBank().variation(2) == original.variation(2));
        auto settings = processor.getSceneBank().variation(3).settings;
        settings.control.lane.explicitEvents = true;
        groovemorph::core::control::LaneEvent event;
        event.operation = 4;
        event.onsetTicks = 960;
        event.durationTicks = 1920;
        settings.control.lane.events[0] = event;
        settings.control.lane.count = 1;
        settings.control.activity.enabled = true;
        settings.control.activity.maxConcurrent = 2;
        processor.setVariationSettings(3, settings);
        juce::MemoryBlock state;
        processor.getStateInformation(state);
        GrooveMorphAudioProcessor restored;
        restored.setStateInformation(state.getData(), static_cast<int>(state.getSize()));
        expect("placement/lane/limits survive host state", restored.getSceneBank().variation(3) == processor.getSceneBank().variation(3));
        processor.resetVariation(3);
        expect("reset restores factory scene", processor.getSceneBank().variation(3) == original.variation(3));
        expect("new parameters exist", processor.parameters.getParameter("morphTarget") != nullptr
               && processor.parameters.getParameter("morphAmount") != nullptr
               && processor.parameters.getParameter("autoReturnBars") != nullptr
               && processor.parameters.getParameter("autoReturnTarget") != nullptr);
    }


    // ---- Alpha: Standalone wrapper supplies no play state; Automatic must still run ----
    {
        FakePlayHead playHead;
        playHead.info.setTimeInSamples(0); // what JUCE's standalone player reports: samples only
        juce::AudioProcessor::setTypeOfNextNewPlugin(juce::AudioProcessor::wrapperType_Standalone);
        {
            GrooveMorphAudioProcessor standalone;
            standalone.setPlayHead(&playHead);
            standalone.prepareToPlay(48000.0, 256);
            processSilence(standalone, 256);
            expect("standalone Automatic free-runs", standalone.isTransportPlaying());
            standalone.setPlayHead(nullptr);
        }
        juce::AudioProcessor::setTypeOfNextNewPlugin(juce::AudioProcessor::wrapperType_Undefined);
        GrooveMorphAudioProcessor plugin;
        plugin.setPlayHead(&playHead);
        plugin.prepareToPlay(48000.0, 256);
        processSilence(plugin, 256);
        expect("plugin host without play state stays stopped", !plugin.isTransportPlaying());
        plugin.setPlayHead(nullptr);
    }

    if (failures == 0)
        std::cout << "PluginProcessorTests: all checks passed\n";
    return failures == 0 ? 0 : 1;
}
