#include "PluginProcessor.h"
#include "ui/visual/VisualWindow.h"
#include "ui/PrintPanel.h"
#include "PluginEditor.h"

#include <cstdlib>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <thread>
#include <cstring>
#include <iostream>
#include <limits>
#include <string_view>
#include <utility>
#include <vector>

using groovemorph::core::TimingSource;
namespace Missing = groovemorph::core::MissingField;

namespace
{
int failures = 0;

void expect(std::string_view name, bool condition)
{
    // Every check is logged (flushed) so a crash in CI shows the last check reached.
    // ctest only prints this output for a failing test.
    if (!condition)
    {
        std::cerr << name << ": check failed\n";
        ++failures;
    }
    else
    {
        std::cerr << "ok: " << name << '\n' << std::flush;
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
    std::cerr << "start: main entered\n" << std::flush;
    juce::ScopedJuceInitialiser_GUI juceInitialiser;
    std::cerr << "start: juce initialised\n" << std::flush;

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
        oversized.setSize(16 * 1024 * 1024 + 1, true); // limit was 1 MiB before loop persistence
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
        for (const float mode : {1.0f, 2.0f, 11.0f, 12.0f}) // 1x/2x: 6 dB slope, 11/12: protect/lows at 12 dB
        {
            GrooveMorphAudioProcessor split;
            setParameter(split, "masterIntensity", 0.0f);
            setParameter(split, "bandMode", mode > 10.0f ? mode - 10.0f : mode);
            setParameter(split, "bandSlope", mode > 10.0f ? 1.0f : 0.0f);
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
                {
                    buffer.setSample(0, 5, std::nanf(""));
                    buffer.setSample(1, 7, std::numeric_limits<float>::infinity());
                }
                juce::AudioBuffer<float> input(buffer);
                juce::MidiBuffer midi;
                split.processBlock(buffer, midi);
                for (int c = 0; c < 2; ++c)
                    for (int i = 0; i < 256; ++i)
                    {
                        const float in = input.getSample(c, i);
                        const float out = buffer.getSample(c, i);
                        if (block >= 3)
                            finite = finite && std::isfinite(out);
                        if (block == 3 && ((c == 0 && i == 5) || (c == 1 && i == 7)))
                            finite = finite && out == 0.0f;
                        if (block != 3 && std::isfinite(in))
                            exact = exact && std::memcmp(&in, &out, sizeof(float)) == 0;
                    }
            }
            expect("band split at wet 0 preserves finite samples and sanitizes NaN/Inf immediately", exact && finite);
        }

        // The host bypass path also has to sanitize the current block, not only
        // prevent non-finite samples from poisoning state in later blocks.
        {
            GrooveMorphAudioProcessor bypassed;
            bypassed.prepareToPlay(48000.0, 256);
            juce::AudioBuffer<float> buffer(2, 256);
            buffer.clear();
            buffer.setSample(0, 5, std::nanf(""));
            buffer.setSample(1, 7, std::numeric_limits<float>::infinity());
            juce::MidiBuffer midi;
            bypassed.processBlockBypassed(buffer, midi);
            bool finiteAndSanitized = true;
            for (int c = 0; c < 2; ++c)
                for (int i = 0; i < buffer.getNumSamples(); ++i)
                    finiteAndSanitized = finiteAndSanitized && std::isfinite(buffer.getSample(c, i));
            finiteAndSanitized = finiteAndSanitized && buffer.getSample(0, 5) == 0.0f
                                 && buffer.getSample(1, 7) == 0.0f;
            expect("host bypass sanitizes NaN and infinity in the current block", finiteAndSanitized);
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
        // A scene with a +1 beat offset keeps its offset after a seek (Preserve policy re-resolved).
        auto offsetScene = processor.getSceneBank().variation(0).settings;
        offsetScene.control.phase.offsetBeats = 1.0f;
        processor.setVariationSettings(0, offsetScene);
        processor.restartActiveScene();
        sample += 512;
        setTransport(playHead, sample);
        processSilence(processor, 512);
        sample += 90000;
        setTransport(playHead, sample);
        processSilence(processor, 512);
        expect("a seek keeps the active scene's own offset", std::abs(processor.getPhaseShiftBeats() - 1.0) < 1e-9);
        offsetScene.control.phase.offsetBeats = 0.0f;
        processor.setVariationSettings(0, offsetScene);
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
        expect("unreadable scene blob keeps the current scenes", restored.getSceneBank() == source.getSceneBank());
        expect("and reports it", restored.getLastSceneLoadReport().status == scene::LoadStatus::Defaulted);
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

    // ---- W4 / F09: loop audio in the project state, hostile loop blobs, tempo stretch ----
    {
        GrooveMorphAudioProcessor source;
        FakePlayHead playHead;
        source.setPlayHead(&playHead);
        const int blockSize = 480;
        source.prepareToPlay(48000.0, blockSize);
        expect("loop stretch defaults to off", source.parameters.getRawParameterValue("loopStretch")->load() == 0.0f);
        juce::MemoryBlock noLoopState;
        source.getStateInformation(noLoopState);
        {
            const auto xml = juce::AudioProcessor::getXmlFromBinary(noLoopState.getData(), static_cast<int>(noLoopState.getSize()));
            expect("no loop: no LoopAudio child", xml != nullptr && !juce::ValueTree::fromXml(*xml).getChildWithName("LoopAudio").isValid());
        }
        expect("capture arms", source.armLoopCapture(1));
        std::int64_t sample = 5 * blockSize;
        const auto signal = [](std::int64_t i) { return 0.5f * static_cast<float>(std::sin(0.002 * static_cast<double>(i))); };
        while (source.getLoopCaptureState() != groovemorph::core::LoopCapture::State::Done && sample < 96000 * 4)
        {
            juce::AudioBuffer<float> buffer(2, blockSize);
            for (int i = 0; i < blockSize; ++i)
            {
                buffer.setSample(0, i, signal(sample + i));
                buffer.setSample(1, i, signal(sample + i));
            }
            setTransport(playHead, sample);
            juce::MidiBuffer midi;
            source.processBlock(buffer, midi);
            sample += blockSize;
        }
        source.pollLoopCapture();
        expect("W4 loop captured", source.hasLoop());
        source.setSourceMode(2);
        juce::MemoryBlock withLoop;
        source.getStateInformation(withLoop);
        expect("W4 state with a loop is larger than without", withLoop.getSize() > noLoopState.getSize() + 1000);
        expect("W4 loop persistence reports no warning", source.getLoopPersistWarning() == 0);

        // Restore into a processor that is already prepared (same and different sample rate) and one that is not.
        for (const double rate : {48000.0, 44100.0, 0.0})
        {
            GrooveMorphAudioProcessor restored;
            if (rate > 0.0)
                restored.prepareToPlay(rate, blockSize);
            restored.setStateInformation(withLoop.getData(), static_cast<int>(withLoop.getSize()));
            if (rate == 0.0)
                restored.prepareToPlay(48000.0, blockSize);
            const std::string tag = " (rate " + std::to_string(static_cast<int>(rate)) + ")";
            expect("W4 loop restored" + tag, restored.hasLoop() && std::abs(restored.getLoopBeats() - 4.0) < 1e-9);
            expect("W4 loop length restored" + tag, std::abs(restored.getLoopSeconds() - 2.0) < 1e-3);
            expect("W4 loop source mode restored" + tag, restored.getSourceMode() == 2);
            if (rate == 48000.0 || rate == 0.0)
            {
                FakePlayHead restoredHead;
                restored.setPlayHead(&restoredHead);
                setParameter(restored, "masterIntensity", 0.0f);
                bool close = true;
                for (std::int64_t pos = 96000 * 3; pos < 96000 * 3 + 4800; pos += blockSize)
                {
                    juce::AudioBuffer<float> buffer(2, blockSize);
                    buffer.clear();
                    setTransport(restoredHead, pos);
                    juce::MidiBuffer midi;
                    restored.processBlock(buffer, midi);
                    for (int i = 0; i < blockSize; ++i)
                        close = close && std::abs(buffer.getSample(0, i) - signal(96000 + ((pos + i) % 96000))) < 2.0e-3f;
                }
                expect("W4 restored loop plays the saved audio (16-bit quantised)" + tag, close);
                restored.setPlayHead(nullptr);
            }
            std::vector<float> lo, hi;
            expect("W4 restored loop has waveform peaks" + tag, restored.getLoopPeaks(64, lo, hi) == 64 && lo.size() == 64);
        }

        // Old projects (no LoopAudio) load unchanged and keep the session's loop.
        {
            GrooveMorphAudioProcessor keeper;
            keeper.prepareToPlay(48000.0, blockSize);
            keeper.setStateInformation(withLoop.getData(), static_cast<int>(withLoop.getSize()));
            keeper.setStateInformation(noLoopState.getData(), static_cast<int>(noLoopState.getSize()));
            expect("W4 state without LoopAudio leaves the loop alone", keeper.hasLoop());
        }
        // Hostile LoopAudio children: the state still loads, no loop appears, a warning is raised.
        {
            const auto hostile = [&](const juce::String& blobText, int version) {
                const auto xml = juce::AudioProcessor::getXmlFromBinary(noLoopState.getData(), static_cast<int>(noLoopState.getSize()));
                auto tree = juce::ValueTree::fromXml(*xml);
                juce::ValueTree bad("LoopAudio");
                bad.setProperty("v", version, nullptr);
                bad.setProperty("blob", blobText, nullptr);
                tree.appendChild(bad, nullptr);
                juce::MemoryBlock block;
                juce::AudioProcessor::copyXmlToBinary(*tree.createXml(), block);
                GrooveMorphAudioProcessor p;
                p.prepareToPlay(48000.0, blockSize);
                p.setStateInformation(block.getData(), static_cast<int>(block.getSize()));
                return std::make_pair(p.hasLoop(), p.getLoopPersistWarning());
            };
            const auto garbage = hostile("AAAA", 1);
            expect("W4 garbage blob rejected with warning", !garbage.first && garbage.second == 3);
            const auto badVersion = hostile("AAAA", 9);
            expect("W4 unknown version rejected with warning", !badVersion.first && badVersion.second == 3);
            const auto notBase64 = hostile("!!!! not base64 ????", 1);
            expect("W4 non-base64 rejected with warning", !notBase64.first && notBase64.second == 3);
            const auto empty = hostile("", 1);
            expect("W4 empty blob rejected with warning", !empty.first && empty.second == 3);
            const auto realXml = juce::AudioProcessor::getXmlFromBinary(withLoop.getData(), static_cast<int>(withLoop.getSize()));
            const juce::String text = juce::ValueTree::fromXml(*realXml).getChildWithName("LoopAudio").getProperty("blob").toString();
            expect("W4 saved blob present", text.length() > 100);
            const auto truncated = hostile(text.substring(0, text.length() / 2), 1);
            expect("W4 truncated blob rejected", !truncated.first && truncated.second == 3);
            const int mid = text.length() / 2;
            const auto corrupted = hostile(text.replaceSection(mid, 1, text[mid] == 'A' ? "B" : "A"), 1);
            expect("W4 corrupted blob rejected", !corrupted.first && corrupted.second == 3);
        }

        // Stretch to host tempo: a 120 BPM loop at a 100 BPM host is rendered 1.2x longer, off the audio thread.
        {
            setParameter(source, "loopStretch", 1.0f);
            for (int block = 0; block < 4; ++block)
            {
                juce::AudioBuffer<float> buffer(2, blockSize);
                buffer.clear();
                playHead.info.setBpm(100.0);
                playHead.info.setTimeSignature(juce::AudioPlayHead::TimeSignature{4, 4});
                playHead.info.setIsPlaying(true);
                playHead.info.setTimeInSamples(block * blockSize);
                playHead.info.setPpqPosition(block * blockSize / 28800.0);
                playHead.info.setPpqPositionOfLastBarStart(0.0);
                juce::MidiBuffer midi;
                source.processBlock(buffer, midi);
            }
            bool stretched = false;
            for (int tick = 0; tick < 400 && !stretched; ++tick)
            {
                source.updateLoopStretch();
                stretched = std::abs(source.getLoopStretchRatio() - 1.2) < 1.0e-3;
                if (!stretched)
                    std::this_thread::sleep_for(std::chrono::milliseconds(20));
            }
            expect("W4 loop stretched to the host tempo (ratio 1.2)", stretched);
            expect("W4 stretch keeps the original loop for saving", std::abs(source.getLoopSeconds() - 2.0) < 1e-3);
            setParameter(source, "loopStretch", 0.0f);
            source.updateLoopStretch();
            expect("W4 stretch off restores the varispeed buffer", std::abs(source.getLoopStretchRatio() - 1.0) < 1e-9 && source.hasLoop());
        }
        source.setPlayHead(nullptr);
    }

    // ---- N04: print the processed output to a WAV and reuse it as the loop source ----
    {
        GrooveMorphAudioProcessor processor;
        FakePlayHead playHead;
        processor.setPlayHead(&playHead);
        const int blockSize = 480;
        processor.prepareToPlay(48000.0, blockSize);
        setParameter(processor, "masterIntensity", 0.0f); // dry path: output == input exactly
        const auto signal = [](std::int64_t i) { return 0.5f * static_cast<float>(std::sin(0.002 * static_cast<double>(i))); };
        const auto drive = [&](std::int64_t from, std::int64_t to) {
            for (std::int64_t sample = from; sample < to; sample += blockSize)
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
            }
        };
        using PrintState = groovemorph::core::OutputCapture::State;
        expect("print arms", processor.armPrint(1, true, true));
        expect("print cannot arm twice", !processor.armPrint(1, false, false));
        // A2: the loop source is shared, so a running print refuses loop capture and import with a message.
        expect("loop capture refused during a print", !processor.armLoopCapture(1)
                   && processor.captureConflict(GrooveMorphAudioProcessor::CaptureKind::LoopCapture).isNotEmpty());
        expect("import refused during a print", processor.importLoopFile(juce::File("/nonexistent/none.wav"), 2).getErrorMessage().contains("print"));
        drive(5 * blockSize, 96000 * 2 + 4800);
        expect("print still recording into its tail", processor.getPrintState() == PrintState::Recording);
        drive(96000 * 2 + 4800, 96000 * 2 + 4800 + 5 * 48000);
        expect("print done", processor.getPrintState() == PrintState::Done);
        GrooveMorphAudioProcessor::PrintResult result;
        expect("print collected", processor.takePrintResult(result));
        expect("print idle after collect", processor.getPrintState() == PrintState::Idle);
        expect("print body is one bar", result.bodySamples == 96000 && result.bars == 1 && result.audio.getNumChannels() == 2);
        expect("print includes the tail", result.audio.getNumSamples() > result.bodySamples);
        expect("print bpm", std::abs(result.bpm - 120.0) < 1.0e-6);
        expect("wet-only falls back at zero intensity", result.wetOnly && !result.wetOnlyApplied);
        bool exact = true;
        for (int i = 0; i < result.bodySamples; i += 7)
            exact = exact && std::abs(result.audio.getSample(0, i) - signal(96000 + i)) < 1.0e-5f;
        expect("print body equals the processed bar", exact);

        const auto file = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("gm-print-test.wav");
        file.deleteFile();
        expect("print wav written", groovemorph::ui::PrintPanel::writeWav24(file, result.audio, result.sampleRate));
        juce::AudioFormatManager formats;
        formats.registerBasicFormats();
        std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(file));
        expect("print wav reads back as 24-bit stereo", reader != nullptr && reader->bitsPerSample == 24
                                                          && reader->numChannels == 2
                                                          && reader->lengthInSamples == result.audio.getNumSamples());
        if (reader != nullptr)
        {
            juce::AudioBuffer<float> back(2, static_cast<int>(reader->lengthInSamples));
            reader->read(&back, 0, back.getNumSamples(), 0, true, true);
            bool same = true;
            for (int i = 0; i < back.getNumSamples(); i += 11)
                same = same && std::abs(back.getSample(1, i) - result.audio.getSample(1, i)) < 1.0e-6f;
            expect("print wav content survives 24-bit", same);
        }
        reader.reset();
        const double bodySeconds = static_cast<double>(result.bodySamples) / result.sampleRate;
        expect("print becomes the loop source (tail trimmed)",
               processor.importLoopFile(file, 0, 4.0 * 60.0 / bodySeconds, bodySeconds).wasOk() && processor.hasLoop()
                   && std::abs(processor.getLoopSeconds() - 2.0) < 1.0e-3 && std::abs(processor.getLoopBeats() - 4.0) < 1.0e-6);
        file.deleteFile();

        // Aborts: a stopped transport discards a body that is still recording; cancel returns to Idle.
        expect("second print arms", processor.armPrint(2, false, false));
        drive(96000 * 4, 96000 * 4 + 96000 + 2400);
        expect("second print recording", processor.getPrintState() == PrintState::Recording);
        drive(96000 * 8, 96000 * 8 + blockSize); // a jump
        expect("jump aborts the print", processor.getPrintState() == PrintState::Aborted
                                           && processor.getPrintFailure() == groovemorph::core::OutputCapture::Failure::Jumped);
        processor.cancelPrint();
        expect("cancel acknowledges", processor.getPrintState() == PrintState::Idle);
        expect("print rearms", processor.armPrint(16, false, false));
        processor.cancelPrint();
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
        expect("bad bar count rejected", processor.importLoopFile(tmp, 17).failed() && processor.importLoopFile(tmp, -1).failed());
        expect("bad loop tempo rejected", processor.importLoopFile(tmp, 0, 5.0).failed() && processor.importLoopFile(tmp, 0, 900.0).failed());
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
        expect("evolution does not modify the saved scene bank", restored.getSceneBank() == stored);
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

    // VF-T10: the Visual window's trace ring never changes the processed audio, and records only while active.
    {
        GrooveMorphAudioProcessor traced, plain;
        for (auto* p : {&traced, &plain})
        {
            setParameter(*p, "masterIntensity", 1.0f);
            p->prepareToPlay(48000.0, 256);
        }
        FakePlayHead headA, headB;
        traced.setPlayHead(&headA);
        plain.setPlayHead(&headB);
        traced.getTraceRing().setActive(true);
        juce::Random random(99);
        bool identical = true;
        for (int block = 0; block < 120; ++block)
        {
            setTransport(headA, block * 256);
            setTransport(headB, block * 256);
            juce::AudioBuffer<float> a(2, 256);
            for (int c = 0; c < 2; ++c)
                for (int i = 0; i < 256; ++i)
                    a.setSample(c, i, random.nextFloat() * 1.6f - 0.8f);
            juce::AudioBuffer<float> b(a);
            juce::MidiBuffer midiA, midiB;
            traced.processBlock(a, midiA);
            plain.processBlock(b, midiB);
            for (int c = 0; c < 2; ++c)
                identical = identical && std::memcmp(a.getReadPointer(c), b.getReadPointer(c), sizeof(float) * 256) == 0;
        }
        expect("trace ring does not change processed audio", identical);
        expect("active ring recorded frames", traced.getTraceRing().pushedCount() == 120u * 256u / 128u);
        expect("inactive ring recorded nothing", plain.getTraceRing().pushedCount() == 0);
        traced.getTraceRing().setActive(false);
        const auto before = traced.getTraceRing().pushedCount();
        setTransport(headA, 120 * 256);
        processSilence(traced, 256);
        expect("deactivated ring stops recording", traced.getTraceRing().pushedCount() == before);
        traced.setPlayHead(nullptr);
        plain.setPlayHead(nullptr);
    }

    // VF-T08 / VF-T09: the Visual content follows its active flag, renders real data without a display, and
    // leaves the audio untouched.
    {
        juce::ScopedJuceInitialiser_GUI gui;
        GrooveMorphAudioProcessor processor;
        setParameter(processor, "masterIntensity", 1.0f);
        processor.prepareToPlay(48000.0, 256);
        FakePlayHead playHead;
        processor.setPlayHead(&playHead);
        groovemorph::ui::visual::VisualContent content(processor);
        content.setSize(900, 560);
        expect("visual content starts with tracing off", !processor.getTraceRing().active());
        content.refresh();
        expect("no frames yet: ledger says so", content.ledgerText().contains("No trace data yet") || content.ledgerText().contains("transport stopped") || content.ledgerText().contains("Timing invalid"));
        content.setActive(true);
        expect("activating the content activates tracing", processor.getTraceRing().active());
        for (int block = 0; block < 400; ++block)
        {
            setTransport(playHead, block * 256);
            juce::AudioBuffer<float> buffer(2, 256);
            for (int i = 0; i < 256; ++i)
                buffer.setSample(0, i, std::sin(0.03f * static_cast<float>(block * 256 + i)) * 0.5f);
            buffer.copyFrom(1, 0, buffer, 0, 0, 256);
            juce::MidiBuffer midi;
            processor.processBlock(buffer, midi);
        }
        content.refresh();
        const auto text = content.ledgerText();
        expect("ledger has live numbers", text.contains("Hearing audio from:") && text.contains("Position: bar") && text.contains("History:"));
        const auto image = content.createComponentSnapshot(content.getLocalBounds(), true, 1.0f);
        expect("content paints to a non-empty image", image.getWidth() == 900 && image.getHeight() == 560);
        bool drewSomething = false;
        for (int y = 0; y < image.getHeight() && !drewSomething; y += 7)
            for (int x = 0; x < image.getWidth(); x += 7)
                if (image.getPixelAt(x, y) != image.getPixelAt(0, 0)) { drewSomething = true; break; }
        expect("content draws more than a flat colour", drewSomething);
        // VF-06: view preferences, keyboard and accessible names.
        content.setSpanBeats(99.0);
        expect("span is clamped high", content.spanBeats() == 8.0);
        content.setSpanBeats(0.1);
        expect("span is clamped low", content.spanBeats() == 1.0);
        content.setSpanBeats(4.0);
        expect("reduce motion starts off", !content.reduceMotion());
        content.setReduceMotion(true);
        expect("reduce motion can be turned on", content.reduceMotion());
        content.setReduceMotion(false);
        bool escaped = false;
        content.onEscape = [&] { escaped = true; };
        expect("escape is handled", content.keyPressed(juce::KeyPress(juce::KeyPress::escapeKey)) && escaped);
        expect("unrelated keys are not swallowed", !content.keyPressed(juce::KeyPress('x')));
        int named = 0;
        for (int i = 0; i < content.getNumChildComponents(); ++i)
            if (content.getChildComponent(i)->getTitle().isNotEmpty())
                ++named;
        expect("every view has an accessible name", named >= 5);

        content.setActive(false);
        expect("deactivating stops tracing", !processor.getTraceRing().active());
        const auto frames = processor.getTraceRing().pushedCount();
        setTransport(playHead, 400 * 256);
        processSilence(processor, 256);
        expect("no frames while the window is closed", processor.getTraceRing().pushedCount() == frames);
        processor.setPlayHead(nullptr);
    }

    // Internal clock (explicit Sample Clock, no host): tempo changes keep the musical position continuous,
    // Stop holds and rewinds, Play resumes, and an imported loop plays at the right speed.
    {
        juce::ScopedJuceInitialiser_GUI gui;
        auto frames = [](GrooveMorphAudioProcessor& p) {
            std::vector<groovemorph::core::trace::TraceFrame> f(8192);
            f.resize(p.getTraceRing().copyLatest(f.data(), f.size()));
            return f;
        };
        auto run = [](GrooveMorphAudioProcessor& p, int blocks, std::vector<float>* capture = nullptr) {
            for (int b = 0; b < blocks; ++b)
            {
                juce::AudioBuffer<float> buf(2, 256);
                buf.clear();
                juce::MidiBuffer midi;
                p.processBlock(buf, midi);
                if (capture)
                    for (int i = 0; i < 256; ++i) capture->push_back(buf.getSample(0, i));
            }
        };
        {
            GrooveMorphAudioProcessor p;
            setParameter(p, "timingMode", 2.0f); // Sample Clock
            setParameter(p, "manualTempo", 120.0f);
            p.prepareToPlay(48000.0, 256);
            p.getTraceRing().setActive(true);
            run(p, 400);
            const auto before = frames(p).back();
            expect("internal clock decides transport", p.isInternalTransportActive() && p.isInternalPlaying());
            setParameter(p, "manualTempo", 140.0f);
            run(p, 1);
            const auto after = frames(p).back();
            const double expectedStep = 256.0 / 48000.0 * 140.0 / 60.0; // one block at the new tempo
            expect("tempo change does not make the position jump",
                   std::abs((after.beat - before.beat) - (expectedStep + 256.0 / 48000.0 * 120.0 / 60.0)) < 0.02);
            run(p, 400);
            const auto a = frames(p); const auto& x = a[a.size() - 300]; const auto& y = a.back();
            const double bps = (y.beat - x.beat) / (static_cast<double>(y.sample - x.sample) / 48000.0);
            expect("beats per second follow the tempo (140 BPM)", std::abs(bps - 140.0 / 60.0) < 0.01);

            p.stopInternalTransport();
            run(p, 4);
            const auto stopped = frames(p).back();
            expect("stopped: transport reports stopped", !p.isTransportPlaying() && (stopped.flags & groovemorph::core::trace::TimingOk) == 0);
            run(p, 50);
            expect("stopped: position stays at the start", p.getBarInPhrase() == 0.0 && p.getBeatInBar() == 0.0);
            p.startInternalTransport();
            run(p, 100); // about one beat at 140 BPM
            expect("play resumes from the start", p.isTransportPlaying() && p.getBarInPhrase() > 0.0 && p.getBarInPhrase() < 1.0);
        }
        // Host sample counter with automated tempo in Sample Clock mode (no PPQ): position stays continuous.
        {
            GrooveMorphAudioProcessor p;
            setParameter(p, "timingMode", 2.0f); // Sample Clock
            p.prepareToPlay(48000.0, 256);
            p.getTraceRing().setActive(true);
            FakePlayHead host;
            host.info.setIsPlaying(true);
            host.info.setTimeSignature(juce::AudioPlayHead::TimeSignature{4, 4});
            p.setPlayHead(&host);
            std::int64_t pos = 0;
            auto hostRun = [&](double bpm, int blocks) {
                host.info.setBpm(bpm);
                for (int b = 0; b < blocks; ++b)
                {
                    host.info.setTimeInSamples(pos);
                    juce::AudioBuffer<float> buf(2, 256);
                    buf.clear();
                    juce::MidiBuffer midi;
                    p.processBlock(buf, midi);
                    pos += 256;
                }
            };
            hostRun(120.0, 400);
            const auto before = frames(p).back();
            hostRun(140.0, 1);
            const auto after = frames(p).back();
            const double stepAtNewTempo = 256.0 / 48000.0 * 140.0 / 60.0;
            expect("host sample counter: tempo automation does not make the position jump",
                   std::abs((after.beat - before.beat) - stepAtNewTempo) < 0.002);
            hostRun(140.0, 400);
            const auto a = frames(p); const auto& x = a[a.size() - 300]; const auto& y = a.back();
            const double bps = (y.beat - x.beat) / (static_cast<double>(y.sample - x.sample) / 48000.0);
            expect("host sample counter: beats per second follow the automated tempo",
                   std::abs(bps - 140.0 / 60.0) < 0.01);
            p.setPlayHead(nullptr);
        }
        // A 4-bar loop at 140 BPM (ramp 0..0.9), imported with automatic bars at the default 120 BPM.
        const double secs = 16.0 * 60.0 / 140.0;
        const int sr = 48000;
        const int n = static_cast<int>(secs * sr);
        const juce::File wav = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("gm_ramp_test.wav");
        {
            wav.deleteFile();
            juce::WavAudioFormat fmt;
            std::unique_ptr<juce::FileOutputStream> os(wav.createOutputStream());
            std::unique_ptr<juce::AudioFormatWriter> w(fmt.createWriterFor(os.get(), sr, 1, 24, {}, 0));
            if (w)
            {
                os.release();
                juce::AudioBuffer<float> r(1, n);
                for (int i = 0; i < n; ++i) r.setSample(0, i, static_cast<float>(i) / static_cast<float>(n) * 0.9f);
                w->writeFromAudioSampleBuffer(r, 0, n);
            }
        }
        // Plays the imported loop and returns how fast its content advances (1.0 = original speed).
        auto loopSpeed = [&](double tempo, int bars, double nativeBpm, double* impliedBpm = nullptr) {
            GrooveMorphAudioProcessor p;
            setParameter(p, "timingMode", 2.0f);
            setParameter(p, "manualTempo", static_cast<float>(tempo));
            setParameter(p, "masterIntensity", 0.0f);
            p.prepareToPlay(48000.0, 256);
            const auto r = p.importLoopFile(wav, bars, nativeBpm);
            expect("loop import succeeds", r.wasOk());
            if (impliedBpm != nullptr) *impliedBpm = p.getLoopBpm();
            p.setSourceMode(2);
            std::vector<float> out;
            run(p, 400, &out);
            const int a = 20000, z = 90000;
            return ((out[z] - out[a]) / (static_cast<double>(z - a) / 48000.0)) * secs / 0.9;
        };
        {
            double implied = 0.0;
            const double at120 = loopSpeed(120.0, 0, 0.0, &implied);
            expect("auto detects the loop's own tempo (140 BPM)", std::abs(implied - 140.0) < 0.5);
            expect("at 120 BPM the 140 BPM loop plays slower (pitch follows tempo)", std::abs(at120 - 120.0 / 140.0) < 0.02);
            expect("at 140 BPM the loop plays at its original speed", std::abs(loopSpeed(140.0, 0, 0.0) - 1.0) < 0.02);
            expect("explicit loop tempo gives the same", std::abs(loopSpeed(140.0, 0, 140.0) - 1.0) < 0.02);
            expect("explicit bars gives the same for the right bar count", std::abs(loopSpeed(140.0, 4, 0.0) - 1.0) < 0.02);
            expect("the wrong bar count plays too fast, which is why auto/BPM exist", loopSpeed(140.0, 2, 0.0) > 1.9);

            GrooveMorphAudioProcessor p;
            setParameter(p, "timingMode", 2.0f);
            p.prepareToPlay(48000.0, 256);
            expect("match tempo needs a loop", !p.matchTempoToLoop());
            expect("import ok", p.importLoopFile(wav, 0, 0.0).wasOk());
            expect("match tempo succeeds", p.matchTempoToLoop());
            expect("manual tempo is now the loop tempo", std::abs(p.parameters.getRawParameterValue("manualTempo")->load() - 140.0f) < 0.6f);
            p.setSourceMode(2);
            p.stopInternalTransport();
            std::vector<float> out;
            run(p, 40, &out);
            float peak = 0.0f;
            for (float v : out) peak = std::max(peak, std::abs(v));
            expect("a stopped loop source is silent", peak == 0.0f);
            p.startInternalTransport();
            out.clear();
            run(p, 40, &out);
            peak = 0.0f;
            for (float v : out) peak = std::max(peak, std::abs(v));
            expect("a playing loop source makes sound", peak > 0.0f);
        }
        wav.deleteFile();
    }

    // MIDI learn and links: learn on first movement, apply to parameters and to scene effects without undo flooding,
    // persist with the project, clear on reset, respect previews.
    {
        juce::ScopedJuceInitialiser_GUI gui;
        auto sendCc = [](GrooveMorphAudioProcessor& p, int cc, int value) {
            juce::AudioBuffer<float> buf(2, 64);
            buf.clear();
            juce::MidiBuffer midi;
            midi.addEvent(juce::MidiMessage::controllerEvent(1, cc, value), 0);
            p.processBlock(buf, midi);
        };
        GrooveMorphAudioProcessor p;
        p.prepareToPlay(48000.0, 64);
        sendCc(p, 30, 10); // movement before learning must not be taken as the answer
        p.servicePendingControllers();
        p.beginMidiLearn("p:masterIntensity");
        expect("learning", p.getMidiLearnTarget() == "p:masterIntensity");
        p.servicePendingControllers();
        expect("old movement does not complete learning", p.getMidiLinkCc("p:masterIntensity") == -1);
        sendCc(p, 31, 100);
        p.servicePendingControllers();
        expect("first moving controller is linked", p.getMidiLinkCc("p:masterIntensity") == 31 && p.getMidiLearnTarget().isEmpty());
        sendCc(p, 31, 127);
        p.servicePendingControllers();
        expect("linked controller drives the parameter (max)", std::abs(p.parameters.getRawParameterValue("masterIntensity")->load() - 1.0f) < 1.0e-4f);
        sendCc(p, 31, 0);
        p.servicePendingControllers();
        expect("linked controller drives the parameter (min)", p.parameters.getRawParameterValue("masterIntensity")->load() < 1.0e-4f);
        expect("bad target is refused", !p.linkMidi(5, "nonsense") && !p.linkMidi(200, "p:dryWet"));

        p.beginMidiLearn("p:dryWet");
        p.cancelMidiLearn();
        expect("learning can be cancelled", p.getMidiLearnTarget().isEmpty());

        // Scene effect link: depth of effect 1 (ReverseSlice) of the sounding scene, no undo entry per move.
        expect("scene link", p.linkMidi(40, "s:1:depth"));
        const auto before = p.getSceneBank();
        sendCc(p, 40, 64);
        p.servicePendingControllers();
        const auto active = p.getActiveVariation();
        expect("scene depth follows the controller", std::abs(p.getSceneBank().variation(active).settings.operations[1].depth - 64.0f / 127.0f) < 1.0e-3f);
        const bool couldUndoBefore = p.canUndoSceneEdit();
        sendCc(p, 40, 100); p.servicePendingControllers();
        sendCc(p, 40, 20); p.servicePendingControllers();
        expect("controller moves add no undo entries", p.canUndoSceneEdit() == couldUndoBefore);
        expect("scene bank really changed", !(p.getSceneBank() == before));
        p.linkMidi(41, "s:1:slice");
        sendCc(p, 41, 127); p.servicePendingControllers();
        expect("slice maps to the top of its range", std::abs(p.getSceneBank().variation(active).settings.operations[1].sliceBeats - 4.0f) < 1.0e-3f);
        sendCc(p, 41, 0); p.servicePendingControllers();
        expect("slice maps to the bottom of its range", std::abs(p.getSceneBank().variation(active).settings.operations[1].sliceBeats - 0.0625f) < 1.0e-4f);
        p.linkMidi(42, "s:1:enabled");
        sendCc(p, 42, 127); p.servicePendingControllers();
        expect("enable follows the controller", p.getSceneBank().variation(active).settings.operations[1].enabled);
        sendCc(p, 42, 0); p.servicePendingControllers();
        expect("disable follows the controller", !p.getSceneBank().variation(active).settings.operations[1].enabled);

        // A preview owns the scene: controllers do not touch it.
        p.beginAudition(active);
        const auto during = p.getSceneBank();
        sendCc(p, 40, 127); p.servicePendingControllers();
        expect("no controller edits during a preview", p.getSceneBank() == during);
        p.revertAudition();

        // Persistence and reset.
        juce::MemoryBlock state;
        p.getStateInformation(state);
        GrooveMorphAudioProcessor q;
        q.setStateInformation(state.getData(), static_cast<int>(state.getSize()));
        expect("links survive a project save and load", q.getMidiLinkCc("p:masterIntensity") == 31 && q.getMidiLinkCc("s:1:depth") == 40);
        p.resetToDefaults();
        expect("reset clears the links", p.getMidiLinkCc("p:masterIntensity") == -1 && p.getMidiLinkCc("s:1:depth") == -1);
    }

    // Reset to defaults: parameters, scenes, source, loop, transport.
    {
        juce::ScopedJuceInitialiser_GUI gui;
        GrooveMorphAudioProcessor p;
        p.prepareToPlay(48000.0, 256);
        setParameter(p, "masterIntensity", 0.25f);
        setParameter(p, "dryWet", 0.4f);
        setParameter(p, "manualTempo", 175.0f);
        setParameter(p, "phraseBars", 8.0f);
        setParameter(p, "transitionMode", 3.0f);
        setParameter(p, "bypass", 1.0f);
        auto edited = p.getSceneBank().variation(2);
        edited.name = "Mine";
        edited.settings.operations[0] = {true, 0.3f, 2.0f};
        p.setVariationName(2, "Mine");
        p.setVariationSettings(2, edited.settings);
        p.setSourceMode(1);
        p.stopInternalTransport();
        const auto defaultBank = groovemorph::core::scene::SceneBank{};
        p.resetToDefaults();
        expect("reset: intensity", std::abs(p.parameters.getRawParameterValue("masterIntensity")->load() - p.parameters.getParameter("masterIntensity")->convertFrom0to1(p.parameters.getParameter("masterIntensity")->getDefaultValue())) < 1.0e-4f);
        expect("reset: dry/wet", std::abs(p.parameters.getRawParameterValue("dryWet")->load() - p.parameters.getParameter("dryWet")->convertFrom0to1(p.parameters.getParameter("dryWet")->getDefaultValue())) < 1.0e-4f);
        expect("reset: tempo is back to 120", std::abs(p.parameters.getRawParameterValue("manualTempo")->load() - 120.0f) < 0.01f);
        expect("reset: phrase bars", std::abs(p.parameters.getRawParameterValue("phraseBars")->load() - p.parameters.getParameter("phraseBars")->convertFrom0to1(p.parameters.getParameter("phraseBars")->getDefaultValue())) < 1.0e-4f);
        expect("reset: bypass off", p.parameters.getRawParameterValue("bypass")->load() < 0.5f);
        expect("reset: scenes are factory", p.getSceneBank() == defaultBank);
        expect("reset: live input source", p.getSourceMode() == 0 && !p.hasLoop());
        expect("reset: transport plays again", p.isInternalPlaying());
        expect("reset: undo history is empty", !p.canUndoSceneEdit());
    }

    // Every interactive control in the editor (including the inspector tabs) explains itself with a tooltip.
    {
        juce::ScopedJuceInitialiser_GUI gui;
        GrooveMorphAudioProcessor p;
        p.prepareToPlay(48000.0, 256);
        std::unique_ptr<juce::AudioProcessorEditor> editor(p.createEditor());
        editor->setSize(1290, 784);
        std::vector<juce::String> missing, unnamed;
        std::function<void(juce::Component&)> walk = [&](juce::Component& c) {
            for (auto* child : c.getChildren())
            {
                const bool interactive = dynamic_cast<juce::Slider*>(child) != nullptr || dynamic_cast<juce::ComboBox*>(child) != nullptr
                                         || dynamic_cast<juce::Button*>(child) != nullptr || dynamic_cast<juce::TextEditor*>(child) != nullptr;
                // Skip a slider's own text box and other internals that sit inside a control.
                const bool insideControl = dynamic_cast<juce::Slider*>(&c) != nullptr || dynamic_cast<juce::ComboBox*>(&c) != nullptr;
                if (interactive && !insideControl)
                {
                    // U6: screen readers need a name and, where a tooltip exists, a description.
                    if (child->getTitle().trim().isEmpty())
                        unnamed.push_back(juce::String(typeid(*child).name()) + " " + child->getName());
                    auto* client = dynamic_cast<juce::TooltipClient*>(child);
                    const auto tip = client != nullptr ? client->getTooltip() : juce::String();
                    if (tip.trim().length() < 12)
                    {
                        juce::String text = child->getName();
                        if (auto* b = dynamic_cast<juce::Button*>(child)) text << " '" << b->getButtonText() << "'";
                        missing.push_back(juce::String(typeid(*child).name()) + " " + text);
                    }
                }
                walk(*child);
                // Only the current tab is in the tree; walk every tab's content as well.
                if (auto* tabs = dynamic_cast<juce::TabbedComponent*>(child))
                    for (int t = 0; t < tabs->getNumTabs(); ++t)
                        if (auto* content = tabs->getTabContentComponent(t))
                            walk(*content);
            }
        };
        walk(*editor);
        for (const auto& m : missing)
            std::cout << "no tooltip: " << m << "\n";
        expect("every control has a tooltip", missing.empty());
        for (const auto& m : unnamed)
            std::cout << "no accessible name: " << m << "\n";
        expect("UI-AX-1 every control has an accessible name", unnamed.empty());
    }

    // Audit fixes: a loaded loop survives prepareToPlay (host rate/buffer change); 384 kHz sessions keep timing.
    {
        juce::ScopedJuceInitialiser_GUI gui;
        const juce::File wav = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("gm_survive_test.wav");
        {
            wav.deleteFile();
            juce::WavAudioFormat fmt;
            std::unique_ptr<juce::FileOutputStream> os(wav.createOutputStream());
            std::unique_ptr<juce::AudioFormatWriter> w(fmt.createWriterFor(os.get(), 44100, 1, 16, {}, 0));
            if (w)
            {
                os.release();
                juce::AudioBuffer<float> r(1, 44100 * 4);
                for (int i = 0; i < r.getNumSamples(); ++i) r.setSample(0, i, 0.5f * std::sin(0.05f * static_cast<float>(i)));
                w->writeFromAudioSampleBuffer(r, 0, r.getNumSamples());
            }
        }
        GrooveMorphAudioProcessor p;
        setParameter(p, "timingMode", 2.0f);
        p.prepareToPlay(48000.0, 256);
        expect("44.1 kHz file imports into a 48 kHz session", p.importLoopFile(wav, 2, 0.0).wasOk() && p.hasLoop());
        expect("the imported loop keeps its duration after resampling", std::abs(p.getLoopSeconds() - 4.0) < 0.01);
        p.prepareToPlay(96000.0, 512);
        expect("the loop survives a sample rate change", p.hasLoop() && std::abs(p.getLoopSeconds() - 4.0) < 0.01);
        p.prepareToPlay(44100.0, 128);
        expect("and another one", p.hasLoop() && std::abs(p.getLoopSeconds() - 4.0) < 0.01);
        p.clearLoop();
        p.prepareToPlay(48000.0, 256);
        expect("a cleared loop stays cleared", !p.hasLoop());
        wav.deleteFile();

        GrooveMorphAudioProcessor hi;
        setParameter(hi, "timingMode", 2.0f);
        hi.prepareToPlay(384000.0, 512);
        juce::AudioBuffer<float> buf(2, 512);
        buf.clear();
        juce::MidiBuffer midi;
        hi.processBlock(buf, midi);
        expect("timing stays valid at 384 kHz", hi.hasValidTiming() && hi.isTransportPlaying());
    }

    // Right-click menu logic: number parsing, clamping, paste into sliders, combos and toggles.
    {
        juce::ScopedJuceInitialiser_GUI gui;
        using namespace groovemorph::ui;
        double v = 0.0;
        expect("plain number", parseNumber("140", v) && v == 140.0);
        expect("number with unit", parseNumber("  -2.5 BPM ", v) && v == -2.5);
        expect("decimal comma", parseNumber("1,5", v) && v == 1.5);
        expect("leading dot", parseNumber(".5", v) && v == 0.5);
        expect("number inside text", parseNumber("tempo: 128.5 bpm", v) && v == 128.5);
        expect("not a number", !parseNumber("abc", v) && !parseNumber("", v) && !parseNumber("-", v) && !parseNumber(".", v));
        expect("format trims zeros", formatNumber(0.25) == "0.25" && formatNumber(140.0) == "140" && formatNumber(-0.0) == "0" && formatNumber(1.0 / 3.0) == "0.333333");

        MenuSlider slider;
        slider.setRange(20.0, 300.0, 0.01);
        slider.setValue(120.0, juce::dontSendNotification);
        expect("typed value is applied", applyTextToSlider(slider, "140") && slider.getValue() == 140.0);
        expect("too large is clamped", applyTextToSlider(slider, "900") && slider.getValue() == 300.0);
        expect("too small is clamped", applyTextToSlider(slider, "-5") && slider.getValue() == 20.0);
        expect("garbage leaves the value alone", !applyTextToSlider(slider, "hello") && slider.getValue() == 20.0);
        bool committed = false;
        slider.onDragEnd = [&] { committed = true; };
        applyTextToSlider(slider, "100");
        expect("scene sliders get their commit call", committed);

        MenuComboBox combo;
        combo.addItemList({"Immediate", "Next Beat", "Next Bar"}, 1);
        expect("combo by name", applyTextToComboBox(combo, "next bar") && combo.getSelectedId() == 3);
        expect("combo by number", applyTextToComboBox(combo, "2") && combo.getSelectedId() == 2);
        expect("combo rejects unknown", !applyTextToComboBox(combo, "Whenever") && !applyTextToComboBox(combo, "9") && combo.getSelectedId() == 2);

        MenuToggleButton toggle("x");
        expect("toggle on/off words", applyTextToToggle(toggle, "on") && toggle.getToggleState() && applyTextToToggle(toggle, "Off") && !toggle.getToggleState());
        expect("toggle numbers", applyTextToToggle(toggle, "1") && toggle.getToggleState());
        expect("toggle rejects nonsense", !applyTextToToggle(toggle, "maybe") && toggle.getToggleState());

        // Copy from one control, paste into another through the (test) clipboard.
        juce::String clip;
        auto& access = ClipboardAccess::instance();
        auto savedCopy = access.copy;
        auto savedPaste = access.paste;
        access.copy = [&](const juce::String& t) { clip = t; };
        access.paste = [&] { return clip; };
        access.copy(formatNumber(0.75));
        MenuSlider other;
        other.setRange(0.0, 1.0, 0.01);
        expect("paste a copied number into another slider", applyTextToSlider(other, access.paste()) && std::abs(other.getValue() - 0.75) < 1e-9);
        access.copy = savedCopy;
        access.paste = savedPaste;
    }


    // W10 (A04): scene note assignment through the processor is one undo step; a legacy duplicate map loads unchanged.
    {
        namespace scene = groovemorph::core::scene;
        GrooveMorphAudioProcessor p;
        auto map = p.getSceneBank().midiNoteMap();
        const auto original = map;
        expect("A04-08 conflict planned", map.planAssign(0, 62).kind == scene::NoteAssignPlan::Kind::Conflict);
        expect("A04-08 swap applied", map.assign(0, 62, scene::ConflictChoice::Swap));
        p.setMidiNoteMap(map);
        const std::array<int, 4> swapped{62, 61, 60, 63};
        expect("A04-08 map swapped in the bank", p.getSceneBank().midiNoteMap().notes == swapped);
        expect("A04-08 one undo reverts both notes", p.undoSceneEdit() && p.getSceneBank().midiNoteMap() == original);

        auto legacy = original;
        legacy.notes = {60, 60, 62, 63};
        p.setMidiNoteMap(legacy);
        juce::MemoryBlock state;
        p.getStateInformation(state);
        GrooveMorphAudioProcessor q;
        q.setStateInformation(state.getData(), static_cast<int>(state.getSize()));
        expect("A04-08 legacy duplicate map loads unchanged", q.getSceneBank().midiNoteMap() == legacy
                   && q.getLastSceneLoadReport().status == scene::LoadStatus::Ok);
    }

    // W10 (A04): learn picks the biggest mover and reports a full table instead of failing silently.
    {
        juce::ScopedJuceInitialiser_GUI gui;
        auto sendCcs = [](GrooveMorphAudioProcessor& p, std::initializer_list<std::pair<int, int>> events) {
            juce::AudioBuffer<float> buf(2, 64);
            buf.clear();
            juce::MidiBuffer midi;
            int at = 0;
            for (const auto& e : events)
                midi.addEvent(juce::MidiMessage::controllerEvent(1, e.first, e.second), at++);
            p.processBlock(buf, midi);
        };
        GrooveMorphAudioProcessor p;
        p.prepareToPlay(48000.0, 64);
        p.beginMidiLearn("p:dryWet");
        sendCcs(p, {{20, 10}, {21, 5}, {21, 125}});
        p.servicePendingControllers();
        expect("A04-09 the controller that moved most is linked", p.getMidiLinkCc("p:dryWet") == 21);

        GrooveMorphAudioProcessor full;
        full.prepareToPlay(48000.0, 64);
        for (int i = 0; i < 64; ++i)
            full.linkMidi(i, "p:fake" + juce::String(i));
        expect("A04-09 table is full", full.getMidiLinkCount() == 64);
        full.beginMidiLearn("p:dryWet");
        sendCcs(full, {{70, 100}});
        full.servicePendingControllers();
        expect("A04-09 learn on a full table leaves learn mode", full.getMidiLearnTarget().isEmpty()
                   && full.getMidiLinkCc("p:dryWet") == -1);
        expect("A04-09 the full table is reported once", full.consumeMidiLinkNotice() == GrooveMorphAudioProcessor::midiLinkNoticeTableFull
                   && full.consumeMidiLinkNotice() == 0);
        expect("A04-09 manual link on a full table is reported", !full.linkMidi(1, "p:masterIntensity")
                   && full.consumeMidiLinkNotice() == GrooveMorphAudioProcessor::midiLinkNoticeTableFull);
        expect("A04-09 relinking an existing target still works when full", full.linkMidi(100, "p:fake3")
                   && full.getMidiLinkCc("p:fake3") == 100);
    }

    // W10: CC 123 / CC 120 release a held trigger note (return to the previous scene), nothing else changes.
    {
        namespace scene = groovemorph::core::scene;
        for (const int panicCc : {123, 120})
        {
            GrooveMorphAudioProcessor processor;
            processor.prepareToPlay(48000.0, 512);
            FakePlayHead playHead;
            processor.setPlayHead(&playHead);
            setParameter(processor, "transitionMode", 0.0f); // Immediate
            auto map = processor.getSceneBank().midiNoteMap();
            map.noteOffPolicy = scene::NoteOffPolicy::ReturnToPrevious;
            processor.setMidiNoteMap(map);
            std::int64_t sample = 4800;
            auto run = [&](const juce::MidiMessage& message) {
                setTransport(playHead, sample);
                juce::AudioBuffer<float> buffer(2, 512);
                buffer.clear();
                juce::MidiBuffer midi;
                midi.addEvent(message, 10);
                processor.processBlock(buffer, midi);
                sample += 512;
            };
            run(juce::MidiMessage::noteOn(1, 62, 0.8f));
            expect("PANIC-1 held note switched the scene", processor.getActiveVariation() == 2);
            run(juce::MidiMessage::controllerEvent(1, panicCc, 0));
            expect("PANIC-1 panic returns to the previous scene", processor.getActiveVariation() == 0);
            run(juce::MidiMessage::noteOff(1, 62));
            expect("PANIC-1 the late note-off does nothing", processor.getActiveVariation() == 0);
            run(juce::MidiMessage::controllerEvent(1, panicCc, 0));
            expect("PANIC-1 panic with nothing held does nothing", processor.getActiveVariation() == 0);
            processor.setPlayHead(nullptr);
        }
    }

    // W10 (A06): trims have stable ids for every operation, default to no effect, never touch stored scenes.
    {
        namespace control = groovemorph::core::control;
        GrooveMorphAudioProcessor p;
        bool allPresent = true;
        for (std::size_t op = 0; op < groovemorph::core::operationCount; ++op)
            for (std::size_t kind = 0; kind < control::trimKindCount; ++kind)
                allPresent = allPresent && p.parameters.getParameter(
                    juce::String(control::operationTrimId(op, static_cast<control::TrimKind>(kind)))) != nullptr;
        expect("A06-06 a trim exists for every operation and kind", allPresent);
        bool legacyPresent = true;
        for (const char* id : {"masterIntensity", "dryWet", "manualTempo", "manualMeterNumerator", "manualMeterDenominator",
                               "phraseBars", "activeVariation", "transitionMode", "timingMode", "bypass", "morphTarget",
                               "morphAmount", "autoReturnBars", "autoReturnTarget", "autoReturnUnit", "bandMode",
                               "bandCrossover", "bandSlope", "audioTrigger", "audioTriggerSens"})
            legacyPresent = legacyPresent && p.parameters.getParameter(id) != nullptr;
        expect("A06-06 every earlier parameter id still exists", legacyPresent);
        // The skewed slice range may store its default one ulp away from 1; the overlay treats that as no change.
        expect("A06-06 defaults", std::abs(p.parameters.getRawParameterValue("op0Depth")->load() - 1.0f) < 1.0e-4f
                   && std::abs(p.parameters.getRawParameterValue("op0Slice")->load() - 1.0f) < 1.0e-4f
                   && p.parameters.getRawParameterValue("op0Enable")->load() == 0.0f);

        auto render = [](GrooveMorphAudioProcessor& processor, bool moveTrim, bool restoreTrim) {
            setParameter(processor, "masterIntensity", 1.0f);
            setParameter(processor, "activeVariation", 1.0f); // B: gate
            setParameter(processor, "transitionMode", 0.0f);
            processor.prepareToPlay(48000.0, 64);
            FakePlayHead playHead;
            processor.setPlayHead(&playHead);
            std::vector<float> out;
            std::int64_t sample = 0;
            for (int block = 0; block < 300; ++block)
            {
                if (block == 5 && moveTrim)
                    setParameter(processor, "op2Enable", 1.0f); // gate forced off
                if (block == 150 && restoreTrim)
                    setParameter(processor, "op2Enable", 0.0f);
                setTransport(playHead, sample);
                juce::AudioBuffer<float> buffer(2, 64);
                for (int i = 0; i < 64; ++i)
                    buffer.setSample(0, i, 0.5f * std::sin(0.01f * static_cast<float>(sample + i)));
                buffer.copyFrom(1, 0, buffer, 0, 0, 64);
                juce::MidiBuffer midi;
                processor.processBlock(buffer, midi);
                for (int i = 0; i < 64; ++i)
                    out.push_back(buffer.getSample(0, i));
                sample += 64;
            }
            processor.setPlayHead(nullptr);
            return out;
        };
        GrooveMorphAudioProcessor reference, trimmed, untouched;
        const auto base = render(reference, false, false);
        const auto moved = render(trimmed, true, false);
        expect("A06-07 a moved trim changes the sound", base.size() == moved.size() && base != moved);
        const auto sceneBefore = untouched.getSceneBank();
        const auto restoredRun = render(untouched, true, true);
        expect("A06-07 returning the trim to default restores the reference output", [&] {
            // From the block after the restore the output must match the reference run sample for sample
            // (the gate has no memory beyond its beat phase, which the transport fixes).
            const std::size_t from = 200 * 64; // clear of any crossfade after the restore at block 150
            for (std::size_t i = from; i < base.size(); ++i)
                if (base[i] != restoredRun[i])
                    return false;
            return true;
        }());
        expect("A06-07 stored scenes and undo history are untouched", untouched.getSceneBank() == sceneBefore
                   && !untouched.canUndoSceneEdit());

        // A06-08: a state without trim parameters (saved by an earlier build) restores them to the default.
        GrooveMorphAudioProcessor source;
        setParameter(source, "op0Depth", 0.3f);
        auto tree = source.parameters.copyState();
        for (std::size_t op = 0; op < groovemorph::core::operationCount; ++op)
            for (std::size_t kind = 0; kind < control::trimKindCount; ++kind)
                tree.removeChild(tree.getChildWithProperty("id", juce::String(control::operationTrimId(
                                     op, static_cast<control::TrimKind>(kind)))), nullptr);
        juce::MemoryBlock legacy;
        juce::AudioProcessor::copyXmlToBinary(*tree.createXml(), legacy);
        GrooveMorphAudioProcessor restored;
        setParameter(restored, "op0Depth", 0.6f);
        restored.setStateInformation(legacy.getData(), static_cast<int>(legacy.getSize()));
        expect("A06-08 trims absent from an old state return to default", restored.parameters.getRawParameterValue("op0Depth")->load() == 1.0f);
    }

    // W10 (UI-UC-7): twenty slider steps within 500 ms are one undo step.
    {
        GrooveMorphAudioProcessor p;
        expect("UI-UC-7 nothing to undo yet", !p.canUndoSceneEdit());
        const auto original = p.getSceneBank().variation(0).settings;
        for (int i = 1; i <= 20; ++i)
        {
            auto settings = p.getSceneBank().variation(0).settings;
            settings.operations[0].depth = 0.003f + 0.04f * static_cast<float>(i);
            p.setVariationSettings(0, settings);
        }
        expect("UI-UC-7 newest value kept", std::abs(p.getSceneBank().variation(0).settings.operations[0].depth - (0.003f + 0.8f)) < 1.0e-5f);
        expect("UI-UC-7 one undo reverts the run", p.undoSceneEdit() && p.getSceneBank().variation(0).settings.operations[0].depth == original.operations[0].depth
                   && !p.canUndoSceneEdit());
        // A toggle in between starts a new entry.
        auto a = p.getSceneBank().variation(0).settings;
        a.operations[1].depth = 0.31f; p.setVariationSettings(0, a);
        a.operations[1].enabled = !a.operations[1].enabled; p.setVariationSettings(0, a);
        a.operations[1].depth = 0.32f; p.setVariationSettings(0, a);
        int undos = 0;
        while (p.undoSceneEdit())
            ++undos;
        expect("UI-UC-7 a toggle splits the runs", undos == 3);
    }

    // ---- N05: Chaos macro, Easy mode state, Surprise me, Panic (docs/specs/N05-EASY-MODE.md) ----
    {
        // N05-C7: the parameter exists, defaults to 0, spans 0..1 and is appended last.
        GrooveMorphAudioProcessor p;
        auto* chaos = p.parameters.getParameter("chaos");
        expect("N05 chaos parameter exists", chaos != nullptr);
        if (chaos != nullptr)
        {
            expect("N05 chaos is appended after every earlier parameter (only later appends follow it)", p.getParameters().indexOf(chaos) > p.getParameters().indexOf(p.parameters.getParameter("audioTriggerSens")));
            expect("N05 chaos defaults to 0", chaos->getDefaultValue() == 0.0f
                                                  && p.parameters.getRawParameterValue("chaos")->load() == 0.0f);
            setParameter(p, "chaos", 1.0f);
            expect("N05 chaos spans 0..1", std::abs(p.parameters.getRawParameterValue("chaos")->load() - 1.0f) < 1e-6f);
        }

        // N05-E1: Easy mode is ON for a fresh instance, round-trips, and is OFF for state without the Ui child.
        GrooveMorphAudioProcessor a;
        expect("N05 fresh instance opens in Easy mode", a.isEasyMode());
        setParameter(a, "chaos", 0.7f);
        juce::MemoryBlock onState;
        a.getStateInformation(onState);
        GrooveMorphAudioProcessor b;
        b.setEasyMode(false);
        b.setStateInformation(onState.getData(), static_cast<int>(onState.getSize()));
        expect("N05 easy mode on round-trips", b.isEasyMode());
        expect("N05 chaos round-trips", std::abs(b.parameters.getRawParameterValue("chaos")->load() - 0.7f) < 1e-5f);
        a.setEasyMode(false);
        juce::MemoryBlock offState;
        a.getStateInformation(offState);
        GrooveMorphAudioProcessor c;
        c.setStateInformation(offState.getData(), static_cast<int>(offState.getSize()));
        expect("N05 easy mode off round-trips", !c.isEasyMode());

        // Legacy fixture: state saved before N05 has neither the Ui child nor the chaos parameter.
        if (auto xml = juce::AudioProcessor::getXmlFromBinary(onState.getData(), static_cast<int>(onState.getSize())))
        {
            if (auto* ui = xml->getChildByName("Ui"))
                xml->removeChildElement(ui, true);
            for (auto* e = xml->getFirstChildElement(); e != nullptr;)
            {
                auto* following = e->getNextElement();
                if (e->hasTagName("PARAM") && e->getStringAttribute("id") == "chaos")
                    xml->removeChildElement(e, true);
                e = following;
            }
            juce::MemoryBlock legacy;
            juce::AudioProcessor::copyXmlToBinary(*xml, legacy);
            GrooveMorphAudioProcessor d;
            setParameter(d, "chaos", 0.5f);
            d.setEasyMode(true);
            d.setStateInformation(legacy.getData(), static_cast<int>(legacy.getSize()));
            expect("N05 legacy state opens in the full view", !d.isEasyMode());
            expect("N05 legacy state restores chaos to 0", d.parameters.getRawParameterValue("chaos")->load() == 0.0f);
        }
        else
        {
            expect("N05 legacy fixture could be built", false);
        }
    }

    // N05-C8: chaos only changes what is heard, never the stored bank, undo history or saved scenes.
    {
        GrooveMorphAudioProcessor p;
        p.prepareToPlay(48000.0, 512);
        const auto bank = p.getSceneBank();
        setParameter(p, "chaos", 1.0f);
        processSilence(p, 512);
        processSilence(p, 512);
        expect("N05 chaos leaves the stored bank alone", p.getSceneBank() == bank && !p.canUndoSceneEdit());
        juce::MemoryBlock saved;
        p.getStateInformation(saved);
        GrooveMorphAudioProcessor q;
        q.setStateInformation(saved.getData(), static_cast<int>(saved.getSize()));
        expect("N05 chaos is not baked into saved scenes", q.getSceneBank() == bank);
    }

    // N05-S3 and Panic.
    {
        GrooveMorphAudioProcessor p;
        p.prepareToPlay(48000.0, 512);
        FakePlayHead playHead;
        setTransport(playHead, 0);
        p.setPlayHead(&playHead);
        processSilence(p, 512);
        const auto before = p.getSceneBank();
        using Status = GrooveMorphAudioProcessor::SurpriseStatus;
        expect("N05 surprise arms", p.requestSurprise() == Status::Armed);
        expect("N05 surprise previews without committing", p.isSurprisePending() && p.isAuditioning()
                                                              && p.getSceneBank() == before && !p.canUndoSceneEdit());
        expect("N05 a second request is refused while previewing", p.requestSurprise() == Status::Busy);
        p.updateSurprise();
        expect("N05 surprise waits inside the phrase", p.isSurprisePending());
        setTransport(playHead, 4 * 96000 + 1000); // next phrase (4 bars of 96000 samples)
        processSilence(p, 512);
        p.updateSurprise();
        expect("N05 surprise is kept at the phrase boundary", !p.isSurprisePending() && !p.isAuditioning()
                                                                 && !(p.getSceneBank() == before));
        expect("N05 surprise is one undo step", p.canUndoSceneEdit() && p.undoSceneEdit() && p.getSceneBank() == before
                                                    && !p.canUndoSceneEdit());
        expect("N05 surprise can be cancelled", p.requestSurprise() == Status::Armed);
        p.cancelSurprise();
        expect("N05 cancel restores the stored scene", !p.isSurprisePending() && !p.isAuditioning()
                                                          && p.getSceneBank() == before && !p.canUndoSceneEdit());
        p.beginAudition(0);
        expect("N05 surprise is busy during the user's own preview", p.requestSurprise() == Status::Busy);
        p.revertAudition();

        setParameter(p, "chaos", 0.8f);
        setParameter(p, "morphAmount", 0.5f);
        expect("N05 surprise arms again before panic", p.requestSurprise() == Status::Armed);
        p.panicEasy();
        expect("N05 panic zeroes chaos and morph", p.parameters.getRawParameterValue("chaos")->load() == 0.0f
                                                       && p.parameters.getRawParameterValue("morphAmount")->load() == 0.0f);
        expect("N05 panic cancels the pending surprise", !p.isSurprisePending() && !p.isAuditioning()
                                                            && p.getSceneBank() == before);

        // A wholly locked sounding scene has nothing to change.
        auto locked = p.getSceneBank().variation(0).settings;
        for (auto& l : locked.control.permissions.operationLocks)
            l = groovemorph::core::control::lock::all;
        p.setVariationSettings(0, locked);
        const auto lockedBank = p.getSceneBank();
        expect("N05 surprise has nothing to change when everything is locked",
               p.requestSurprise() == Status::NothingToChange && !p.isAuditioning() && p.getSceneBank() == lockedBank);
        p.setPlayHead(nullptr);
    }

    if (failures == 0)
        std::cout << "PluginProcessorTests: all checks passed\n";
    return failures == 0 ? 0 : 1;
}
