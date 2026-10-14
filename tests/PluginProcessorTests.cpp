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

        // Audit regression: the same null holds with the band split active, including after a NaN block.
        for (const float mode : {1.0f, 2.0f})
        {
            GrooveMorphAudioProcessor split;
            setParameter(split, "masterIntensity", 0.0f);
            setParameter(split, "bandMode", mode);
            split.prepareToPlay(48000.0, 256);
            FakePlayHead splitHead;
            split.setPlayHead(&splitHead);
            juce::Random rng(99);
            bool exact = true, finite = true;
            for (int block = 0; block < 30; ++block)
            {
                setTransport(splitHead, block * 256);
                juce::AudioBuffer<float> buffer(2, 256);
                for (int c = 0; c < 2; ++c)
                    for (int i = 0; i < 256; ++i)
                        buffer.setSample(c, i, rng.nextFloat() * 2.0f - 1.0f);
                if (block == 3)
                    buffer.setSample(0, 5, std::nanf(""));
                juce::AudioBuffer<float> input(buffer);
                juce::MidiBuffer midi;
                split.processBlock(buffer, midi);
                for (int c = 0; c < 2; ++c)
                    for (int i = 0; i < 256; ++i)
                    {
                        const float in = input.getSample(c, i);
                        const float out = buffer.getSample(c, i);
                        if (block > 3)
                            finite = finite && std::isfinite(out);
                        if (block != 3 && std::isfinite(in))
                            exact = exact && std::memcmp(&in, &out, sizeof(float)) == 0;
                    }
            }
            expect("band split at wet 0 is bit-exact and survives NaN", exact && finite);
        }

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

    // C04: the destination scene's phase policy sets the pattern shift at the switch frame.
    {
        GrooveMorphAudioProcessor processor;
        setParameter(processor, "phraseBars", 4.0f);
        processor.prepareToPlay(48000.0, 512);
        FakePlayHead playHead;
        processor.setPlayHead(&playHead);
        setParameter(processor, "transitionMode", 0.0f); // Immediate
        auto restartScene = processor.getSceneBank().variation(1).settings;
        restartScene.control.phase.policy = groovemorph::core::control::PhasePolicy::Restart;
        processor.setVariationSettings(1, restartScene);
        std::int64_t sample = 30000; // global meter-beat position 1.25
        setTransport(playHead, sample);
        processSilence(processor, 512);
        expect("preserve scene keeps the transport phase", processor.getPhaseShiftBeats() == 0.0);
        processor.requestVariation(1);
        sample += 512;
        setTransport(playHead, sample);
        processSilence(processor, 512);
        const double beat = static_cast<double>(sample) / 24000.0;
        const double local = std::fmod(beat + processor.getPhaseShiftBeats(), 16.0);
        expect("restart scene starts its pattern at phrase beat 0", processor.getActiveVariation() == 1
                                                                       && (local < 1e-6 || 16.0 - local < 1e-6));
        processor.requestVariation(0);
        sample += 512;
        setTransport(playHead, sample);
        processSilence(processor, 512);
        expect("switching back to a preserve scene returns to the transport phase", processor.getPhaseShiftBeats() == 0.0);
        processor.restartActiveScene();
        sample += 512;
        setTransport(playHead, sample);
        processSilence(processor, 512);
        const double beat2 = static_cast<double>(sample) / 24000.0;
        const double local2 = std::fmod(beat2 + processor.getPhaseShiftBeats(), 16.0);
        expect("restart command restarts the active scene", processor.getActiveVariation() == 0
                                                                && (local2 < 1e-6 || 16.0 - local2 < 1e-6));
        sample += 100000; // a seek returns to the transport's own phase
        setTransport(playHead, sample);
        processSilence(processor, 512);
        expect("a seek cancels the phase shift", processor.getPhaseShiftBeats() == 0.0);
        processor.setPlayHead(nullptr);
    }

    // Gesture target and evolution settings travel with the project state; garbage restores safely.
    {
        GrooveMorphAudioProcessor source;
        source.setGestureTarget(2);
        source.setEvolution(true, 3, 4, 0.6f, 2, 7);
        juce::MemoryBlock block;
        source.getStateInformation(block);
        GrooveMorphAudioProcessor restored;
        restored.setStateInformation(block.getData(), static_cast<int>(block.getSize()));
        const auto evolution = restored.getEvolutionSettings();
        expect("gesture target restored", restored.getGestureTarget() == 2);
        expect("evolution restored", evolution.enabled && evolution.scene == 3 && evolution.everyPhrases == 4
                                         && std::abs(evolution.distance - 0.6f) < 1e-5f && evolution.style == 2);
        GrooveMorphAudioProcessor fresh;
        expect("a fresh processor has no evolution", !fresh.getEvolutionSettings().enabled && fresh.getGestureTarget() == 0);
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


    // ---- Alpha 2: auto-return in phrase units (C05) ----
    {
        GrooveMorphAudioProcessor processor;
        FakePlayHead playHead;
        processor.setPlayHead(&playHead);
        processor.prepareToPlay(48000.0, 256);
        setParameter(processor, "transitionMode", 0.0f);
        setParameter(processor, "autoReturnBars", 1.0f);
        setParameter(processor, "autoReturnUnit", 1.0f); // phrases (4 bars)
        std::int64_t sample = 0;
        const auto block = [&] {
            setTransport(playHead, sample);
            processSilence(processor, 256);
            sample += 256;
        };
        block();
        processor.requestVariation(1);
        block();
        while (sample < 384000 + 512)
            block();
        expect("phrase-unit return lands on the phrase boundary", processor.getActiveVariation() == 0
               && processor.getLastSwitchSample() == 384000);
        processor.setPlayHead(nullptr);
    }

    // ---- Alpha 2: band split keeps one part of the spectrum out of the effects (F03/F10) ----
    {
        const auto measure = [](float mode, double& lowRms, double& highRms) {
            GrooveMorphAudioProcessor processor;
            FakePlayHead playHead;
            processor.setPlayHead(&playHead);
            processor.prepareToPlay(48000.0, 480);
            setParameter(processor, "masterIntensity", 1.0f);
            setParameter(processor, "bandMode", mode);
            setParameter(processor, "bandCrossover", 400.0f);
            groovemorph::core::VariationSettings settings;
            settings[groovemorph::core::OperationType::Gate] = {true, 1.0f, 1.0f}; // closed 2nd half of each beat
            processor.setVariationSettings(0, settings);
            double lowSum = 0.0, highSum = 0.0;
            std::int64_t counted = 0;
            for (std::int64_t sample = 0; sample < 48000 * 3; sample += 480)
            {
                juce::AudioBuffer<float> buffer(2, 480);
                for (int i = 0; i < 480; ++i)
                {
                    const double t = static_cast<double>(sample + i) / 48000.0;
                    const float low = 0.3f * static_cast<float>(std::sin(6.283185307179586 * 50.0 * t));
                    const float high = 0.3f * static_cast<float>(std::sin(6.283185307179586 * 5000.0 * t));
                    buffer.setSample(0, i, low + high);
                    buffer.setSample(1, i, low + high);
                }
                setTransport(playHead, sample);
                juce::MidiBuffer midi;
                processor.processBlock(buffer, midi);
                // Closed part of the gate: beat fraction 0.6 .. 0.95, well after the ramp.
                const double beatFraction = std::fmod(static_cast<double>(sample) / 24000.0, 1.0);
                if (sample > 48000 && beatFraction > 0.6 && beatFraction < 0.95)
                    for (int i = 0; i < 480; ++i)
                    {
                        const double t = static_cast<double>(sample + i) / 48000.0;
                        const double sLow = std::sin(6.283185307179586 * 50.0 * t);
                        const double sHigh = std::sin(6.283185307179586 * 5000.0 * t);
                        const double y = buffer.getSample(0, i);
                        lowSum += y * sLow;   // projection on each tone (blocks hold whole cycles)
                        highSum += y * sHigh;
                        ++counted;
                    }
            }
            lowRms = 2.0 * lowSum / static_cast<double>(counted);   // amplitude of the 50 Hz component
            highRms = 2.0 * highSum / static_cast<double>(counted); // amplitude of the 5 kHz component
            processor.setPlayHead(nullptr);
        };
        double low = 0, high = 0;
        measure(0.0f, low, high);
        expect("full band: gate silences both tones", std::abs(low) < 0.03 && std::abs(high) < 0.03);
        measure(1.0f, low, high);
        expect("protect lows: bass survives the closed gate, highs are gated", low > 0.2 && std::abs(high) < 0.03);
        measure(2.0f, low, high);
        expect("lows only: highs bypass the closed gate, bass is gated", high > 0.25 && std::abs(low) < 0.1);
    }

    // ---- Alpha 2: audio trigger changes scene on input transients (F07) ----
    {
        GrooveMorphAudioProcessor processor;
        FakePlayHead playHead;
        processor.setPlayHead(&playHead);
        processor.prepareToPlay(48000.0, 480);
        setParameter(processor, "transitionMode", 0.0f);
        setParameter(processor, "audioTrigger", 1.0f); // next scene
        std::int64_t sample = 0;
        bool changed = false;
        for (int b = 0; b < 48000 * 3 / 480; ++b)
        {
            juce::AudioBuffer<float> buffer(2, 480);
            for (int i = 0; i < 480; ++i)
            {
                const std::int64_t since = (sample + i) % 24000;
                const float click = since < 1200 ? 0.8f * std::exp(-static_cast<float>(since) / 300.0f) : 0.0f;
                const float v = 0.005f * std::sin(0.05f * static_cast<float>(sample + i)) + click;
                buffer.setSample(0, i, v);
                buffer.setSample(1, i, v);
            }
            setTransport(playHead, sample);
            juce::MidiBuffer midi;
            processor.processBlock(buffer, midi);
            changed = changed || processor.getActiveVariation() != 0;
            sample += 480;
        }
        expect("onsets detected", processor.getOnsetCount() >= 4);
        expect("audio trigger advanced the scene", changed);
        processor.setPlayHead(nullptr);
    }

    // ---- Alpha 2: MIDI macros (F12) ----
    {
        GrooveMorphAudioProcessor processor;
        FakePlayHead playHead;
        processor.setPlayHead(&playHead);
        processor.prepareToPlay(48000.0, 256);
        setParameter(processor, "transitionMode", 0.0f);
        setParameter(processor, "masterIntensity", 0.2f);
        const auto send = [&](std::initializer_list<juce::uint8> bytes) {
            juce::MidiBuffer midi;
            midi.addEvent(bytes.begin(), static_cast<int>(bytes.size()), 0);
            setTransport(playHead, 0);
            juce::AudioBuffer<float> buffer(2, 256);
            buffer.clear();
            processor.processBlock(buffer, midi);
        };
        send({0xB0, 20, 127});
        send({0xC0, 2});
        processor.applyMidiMacros();
        expect("macros off by default: CC and program change ignored",
               std::abs(processor.parameters.getRawParameterValue("masterIntensity")->load() - 0.2f) < 1e-4f
               && processor.getActiveVariation() == 0);
        processor.setMidiMacrosEnabled(true);
        send({0xB0, 20, 127});
        send({0xB0, 22, 64});
        send({0xC0, 2});
        send({0xC0, 9}); // out of range: ignored
        processor.applyMidiMacros();
        expect("CC 20 sets intensity", std::abs(processor.parameters.getRawParameterValue("masterIntensity")->load() - 1.0f) < 1e-3f);
        expect("CC 22 sets morph amount", std::abs(processor.parameters.getRawParameterValue("morphAmount")->load() - 64.0f / 127.0f) < 1e-3f);
        expect("program change selects a scene", processor.getActiveVariation() == 2);
        processor.setPlayHead(nullptr);
    }


    // ---- Alpha 2: capture a loop and play it back beat-locked (F09) ----
    {
        GrooveMorphAudioProcessor processor;
        FakePlayHead playHead;
        processor.setPlayHead(&playHead);
        const int blockSize = 480;
        processor.prepareToPlay(48000.0, blockSize);
        setParameter(processor, "masterIntensity", 0.0f); // dry path: output == the (replaced) input exactly
        expect("capture arms", processor.armLoopCapture(1));
        std::vector<float> recorded;
        std::int64_t sample = 5 * blockSize; // start mid-bar: capture waits for the next bar line
        const auto signal = [](std::int64_t i) { return 0.5f * static_cast<float>(std::sin(0.002 * static_cast<double>(i))); };
        while (processor.getLoopCaptureState() != groovemorph::core::LoopCapture::State::Done && sample < 96000 * 4)
        {
            juce::AudioBuffer<float> buffer(2, blockSize);
            for (int i = 0; i < blockSize; ++i)
            {
                buffer.setSample(0, i, signal(sample + i));
                buffer.setSample(1, i, signal(sample + i));
            }
            setTransport(playHead, sample);
            juce::MidiBuffer midi;
            processor.processBlock(buffer, midi);
            sample += blockSize;
        }
        expect("capture completed", processor.getLoopCaptureState() == groovemorph::core::LoopCapture::State::Done);
        processor.pollLoopCapture();
        expect("loop published", processor.hasLoop() && std::abs(processor.getLoopSeconds() - 2.0) < 1e-3);
        processor.setSourceMode(2);
        bool exact = true;
        for (std::int64_t pos = 96000 * 3; pos < 96000 * 3 + 4800; pos += blockSize)
        {
            juce::AudioBuffer<float> buffer(2, blockSize);
            buffer.clear();
            setTransport(playHead, pos);
            juce::MidiBuffer midi;
            processor.processBlock(buffer, midi);
            for (int i = 0; i < blockSize; ++i)
            {
                // The capture started on the bar line at sample 96000, so the loop is the signal
                // from [96000, 192000) repeated every bar (96000 samples).
                const float want = signal(96000 + ((pos + i) % 96000));
                exact = exact && std::abs(buffer.getSample(0, i) - want) < 1e-3f;
            }
        }
        expect("loop plays back locked to the bar grid", exact);
        processor.clearLoop();
        expect("loop cleared", !processor.hasLoop());
        processor.setPlayHead(nullptr);
    }

    // ---- Alpha 2: file import validation (F09) ----
    {
        GrooveMorphAudioProcessor processor;
        processor.prepareToPlay(48000.0, 256);
        expect("missing file rejected", processor.importLoopFile(juce::File("/nonexistent/none.wav"), 2).failed());
        const auto tmp = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("gm-import-test.wav");
        {
            juce::WavAudioFormat wav;
            std::unique_ptr<juce::FileOutputStream> stream(tmp.createOutputStream());
            std::unique_ptr<juce::AudioFormatWriter> writer(wav.createWriterFor(stream.get(), 44100.0, 1, 16, {}, 0));
            if (writer != nullptr)
            {
                stream.release();
                juce::AudioBuffer<float> data(1, 44100);
                for (int i = 0; i < 44100; ++i)
                    data.setSample(0, i, 0.3f * std::sin(0.05f * static_cast<float>(i)));
                writer->writeFromAudioSampleBuffer(data, 0, 44100);
            }
        }
        expect("bad bar count rejected", processor.importLoopFile(tmp, 0).failed());
        expect("wav imports", processor.importLoopFile(tmp, 2).wasOk() && processor.hasLoop()
                                  && std::abs(processor.getLoopSeconds() - 1.0) < 1e-3);
        tmp.deleteFile();
        const auto text = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("gm-not-audio.wav");
        text.replaceWithText("not audio");
        expect("non-audio file rejected", processor.importLoopFile(text, 1).failed());
        text.deleteFile();
    }

    // ---- Alpha 2: gesture record/playback (F11) ----
    {
        GrooveMorphAudioProcessor processor;
        FakePlayHead playHead;
        processor.setPlayHead(&playHead);
        processor.prepareToPlay(48000.0, 256);
        processor.setGestureTarget(0); // morph amount
        const auto atPhrase = [&](double fraction) {
            setTransport(playHead, static_cast<std::int64_t>(fraction * 384000.0)); // 4-bar phrase
            processSilence(processor, 256);
        };
        processor.setGestureRecording(true);
        for (int step = 0; step < 8; ++step)
        {
            const double fraction = step / 8.0;
            atPhrase(fraction);
            setParameter(processor, "morphAmount", static_cast<float>(step) / 7.0f);
            processor.updateGesture();
        }
        expect("gesture recorded", processor.getGestureFill() >= 8);
        processor.setGestureRecording(false);
        setParameter(processor, "morphAmount", 0.0f);
        processor.setGesturePlaying(true);
        atPhrase(5.0 / 8.0);
        processor.updateGesture();
        const float played = processor.parameters.getRawParameterValue("morphAmount")->load();
        expect("gesture plays back the recorded value", std::abs(played - 5.0f / 7.0f) < 0.05f);
        processor.clearGesture();
        expect("gesture cleared", processor.getGestureFill() == 0 && !processor.isGesturePlaying());
        processor.setPlayHead(nullptr);
    }

    // ---- Alpha 2: phrase evolution never touches stored scenes (F05) ----
    {
        GrooveMorphAudioProcessor processor;
        FakePlayHead playHead;
        processor.setPlayHead(&playHead);
        processor.prepareToPlay(48000.0, 256);
        const auto stored = processor.getSceneBank();
        processor.setEvolution(true, 0, 1, 1.0f, 2, 1234);
        std::int64_t sample = 0;
        const auto advanceTo = [&](std::int64_t target) {
            while (sample < target)
            {
                setTransport(playHead, sample);
                processSilence(processor, 256);
                processor.updateEvolution();
                sample += 256;
            }
        };
        advanceTo(10000);
        expect("no evolution step within the first phrase", processor.getEvolutionStep() == 0);
        advanceTo(384000 + 2560);
        expect("evolves at the phrase boundary", processor.getEvolutionStep() == 1);
        advanceTo(2 * 384000 + 2560);
        expect("and again next phrase", processor.getEvolutionStep() == 2);
        expect("stored scene is untouched", processor.getSceneBank() == stored);
        juce::MemoryBlock state;
        processor.getStateInformation(state);
        GrooveMorphAudioProcessor restored;
        restored.setStateInformation(state.getData(), static_cast<int>(state.getSize()));
        expect("evolution is not saved into project state", restored.getSceneBank() == stored);
        processor.setEvolution(false, 0, 1, 1.0f, 2, 1234);
        expect("evolution off", !processor.isEvolving());
        processor.setPlayHead(nullptr);
    }


    // ---- Audit regressions ----
    {
        // Capture stays contiguous when placement boundaries split the render segments.
        GrooveMorphAudioProcessor processor;
        FakePlayHead playHead;
        processor.setPlayHead(&playHead);
        const int blockSize = 480;
        processor.prepareToPlay(48000.0, blockSize);
        setParameter(processor, "masterIntensity", 0.0f);
        auto settings = processor.getSceneBank().variation(0).settings;
        settings.control.placement.enabled = true;
        settings.control.placement.window = groovemorph::core::control::WindowMode::Custom;
        settings.control.placement.startPhase = 0.1f;
        settings.control.placement.spanPhase = 0.3f;
        settings.control.placement.downbeatProtectBeats = 0.7f;
        processor.setVariationSettings(0, settings);
        expect("capture arms with placement active", processor.armLoopCapture(1));
        const auto signal = [](std::int64_t i) { return 0.5f * static_cast<float>(std::sin(0.002 * static_cast<double>(i))); };
        std::int64_t sample = 7 * blockSize;
        while (processor.getLoopCaptureState() != groovemorph::core::LoopCapture::State::Done && sample < 96000 * 4)
        {
            juce::AudioBuffer<float> buffer(2, blockSize);
            for (int i = 0; i < blockSize; ++i)
            {
                buffer.setSample(0, i, signal(sample + i));
                buffer.setSample(1, i, signal(sample + i));
            }
            setTransport(playHead, sample);
            juce::MidiBuffer midi;
            processor.processBlock(buffer, midi);
            sample += blockSize;
        }
        processor.pollLoopCapture();
        processor.setSourceMode(2);
        bool exact = processor.hasLoop();
        for (std::int64_t pos = 96000 * 3; pos < 96000 * 3 + 4800 && exact; pos += blockSize)
        {
            juce::AudioBuffer<float> buffer(2, blockSize);
            buffer.clear();
            setTransport(playHead, pos);
            juce::MidiBuffer midi;
            processor.processBlock(buffer, midi);
            for (int i = 0; i < blockSize; ++i)
                exact = exact && std::abs(buffer.getSample(0, i) - signal(96000 + ((pos + i) % 96000))) < 1e-3f;
        }
        expect("captured loop has no duplicated spans when placement splits segments", exact);
        processor.setPlayHead(nullptr);
    }
    {
        // Scene operations keep the destination's locked fields and lock configuration.
        GrooveMorphAudioProcessor processor;
        auto locked = processor.getSceneBank().variation(1).settings;
        locked.operations[2] = {true, 0.55f, 0.5f};
        locked.control.permissions.operationLocks[2] = groovemorph::core::control::lock::depth;
        processor.setVariationSettings(1, locked);
        expect("duplicate keeps the destination's locked depth",
               processor.duplicateVariation(0, 1)
               && processor.getSceneBank().variation(1).settings.operations[2].depth == 0.55f
               && processor.getSceneBank().variation(1).settings.control.permissions.operationLocks[2]
                      == groovemorph::core::control::lock::depth);
        processor.resetVariation(1);
        expect("reset keeps locked fields", processor.getSceneBank().variation(1).settings.operations[2].depth == 0.55f);
        // Undo is refused while a preview is running.
        processor.beginAudition(0);
        expect("undo refused during audition", !processor.undoSceneEdit());
        processor.revertAudition();
    }

    if (failures == 0)
        std::cout << "PluginProcessorTests: all checks passed\n";
    return failures == 0 ? 0 : 1;
}
