// N05 Easy mode services of the processor (Surprise me, Panic). Kept in their own file so the shared
// PluginProcessor.cpp only receives small additive hooks. See docs/specs/N05-EASY-MODE.md.
#include "PluginProcessor.h"
#include "core/variation/Surprise.h"

#include <algorithm>
#include <utility>

namespace core = groovemorph::core;
namespace scene = groovemorph::core::scene;
namespace variation = groovemorph::core::variation;

GrooveMorphAudioProcessor::SurpriseStatus GrooveMorphAudioProcessor::requestSurprise()
{
    const std::lock_guard<std::mutex> lock(modelMutex);
    if (audition.isActive())
        return SurpriseStatus::Busy; // a preview (this one or the user's own) must be applied or reverted first
    const auto index = std::min<std::size_t>(activeVariation.load(std::memory_order_relaxed),
                                             core::variationCount - 1);
    const auto bank = undoStack.current();
    const auto result = variation::buildSurprise(bank.variation(index).settings, bank.seed(), surpriseAttempt);
    if (!result.ok)
        return SurpriseStatus::NothingToChange;
    surpriseAttempt = result.candidate + 1; // the next press starts after this candidate
    audition.begin(bank, index);
    auto edited = audition.edited();
    edited.settings = result.settings;
    audition.setEdited(std::move(edited));
    surprisePending = true;
    surprisePhrase = phraseIndexStatus.load(std::memory_order_relaxed);
    publishSceneLocked();
    return SurpriseStatus::Armed;
}

void GrooveMorphAudioProcessor::cancelSurprise()
{
    const std::lock_guard<std::mutex> lock(modelMutex);
    if (!surprisePending)
        return;
    surprisePending = false;
    if (audition.isActive())
    {
        audition.revert();
        publishSceneLocked();
    }
}

bool GrooveMorphAudioProcessor::isSurprisePending() const
{
    const std::lock_guard<std::mutex> lock(modelMutex);
    return surprisePending && audition.isActive();
}

void GrooveMorphAudioProcessor::updateSurprise()
{
    const std::lock_guard<std::mutex> lock(modelMutex);
    if (!surprisePending)
        return;
    if (!audition.isActive())
    {
        surprisePending = false; // applied or reverted from the full view, or state was replaced
        return;
    }
    // Wait for the next phrase boundary while there is a running, usable transport; otherwise there is
    // nothing to wait for and the change is kept right away.
    const bool waiting = transportPlaying.load(std::memory_order_relaxed) && timingValid.load(std::memory_order_relaxed);
    if (waiting && phraseIndexStatus.load(std::memory_order_relaxed) == surprisePhrase)
        return;
    surprisePending = false;
    auto bank = undoStack.current();
    if (audition.commit(bank))
        commitBankLocked(bank); // one undoable edit
    else
        publishSceneLocked();
}

void GrooveMorphAudioProcessor::panicEasy()
{
    cancelSurprise();
    if (isAuditioning())
        revertAudition();
    cancelQueuedSwitch();
    for (const char* id : {chaosId, morphAmountId})
        if (auto* parameter = parameters.getParameter(id))
        {
            parameter->beginChangeGesture();
            parameter->setValueNotifyingHost(parameter->convertTo0to1(0.0f));
            parameter->endChangeGesture();
        }
    const auto evolution = getEvolutionSettings();
    if (evolution.enabled)
    {
        std::uint64_t seed;
        {
            const std::lock_guard<std::mutex> lock(modelMutex);
            seed = evolutionSettings.seed;
        }
        setEvolution(false, evolution.scene, evolution.everyPhrases, evolution.distance, evolution.style, seed);
    }
    setGestureRecording(false);
    setGesturePlaying(false);
}
