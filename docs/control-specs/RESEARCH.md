# Research and evidence (checked 2026-10-13)

Primary sources only. Research informs contracts; it does not prove uniqueness,
performance or implementation completion. The design decisions below are ours.

| Source | Relevant observation | Decision in this package |
|---|---|---|
| [JUCE APVTS reference](https://docs.juce.com/master/classjuce_1_1AudioProcessorValueTreeState.html) | Raw values support realtime scalar reads; state-copy operations use synchronization | Cache parameter pointers; copy/restore models outside processing |
| [VST3 parameter/automation reference](https://steinbergmedia.github.io/vst3_dev_portal/pages/Technical%2BDocumentation/Parameters%2BAutomation/Index.html) | Host automation delivers offset-bearing parameter queues; UI edits require proper host gestures | Separate musical-event sample timing from adapter-observed scalar timing; use stable discrete/continuous identities |
| [Ableton launch/legato/follow-action manual](https://www.ableton.com/en/manual/launching-clips/) | Launch timing, phase continuity and subsequent actions are separate concepts | C04 phase differs from quantization; C05 duration differs from return-boundary quantization |
| [Ableton Follow Action controls](https://help.ableton.com/hc/en-us/articles/360019101360-Updates-to-Follow-Actions-in-Live-11) | Follow behavior can be exposed in expandable clip controls | Use contextual Return inspector and visible current-fill status |

Version-specific source inspection: pinned JUCE 8.0.7 under
build/_deps/juce-src/modules/juce_audio_plugin_client/juce_audio_plugin_client_VST3.cpp,
processParameterChanges (approximately lines3617–3660), applies the final queue point
for ordinary parameters before processBlock; MIDI-emulation paths handle offsets
separately. APVTS raw atomics therefore do not supply every automation sample offset
to this processor. Exact source:
https://github.com/juce-framework/JUCE/blob/8.0.7/modules/juce_audio_plugin_client/juce_audio_plugin_client_VST3.cpp
Master JUCE online docs are conceptual support; pinned source determines shipped behavior.

Repository source inspected: Scheduler request/manual/cancel semantics;
TransportResolver PPQ/sample fallback; RhythmEngine phase equations and stage-input
history; processor APVTS IDs, MIDI splits and audition/state; editor layouts. These
justify extending canonical modules instead of adding a rival scheduling/DSP engine.
Causal source reads, bounded masks and deferred generation are engineering decisions.
A scheduling/control proposal is not proof of inaudible transitions: fixture and
listening acceptance remains required. No novelty/patent claim is made.

Cross-project independence: this specification belongs only to GrooveMorph and
introduces no PatchTrace or other repository integration.
