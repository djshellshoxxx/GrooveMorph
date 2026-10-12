# Specification review and implementation gap audit

**Reviewed:** 2026-10-12  
**Specification:** docs/SPECIFICATION.md and its normative §5.1/§5.2 clarifications  
**Initial code reviewed:** active implementation at dfcabda030f7e1432f4b51baad471b26ddc91f0c  
**Continuation reviewed:** codex/mvp-continuation through 2015e547d4fbbdc950a2458d2924e0e2fe462e23  
**Method:** requirements-to-code/test traceability plus separate architecture, phrase-clock, and timing/state reviews.

## Finding summary

The specification is suitable to guide implementation after the normative edge contracts in §5.1 and the measurement protocol in §5.2. The initial audit found these contract conflicts, and the continuation fixes them where identified:

1. Host sample position plus one tempo cannot preserve musical phase through tempo maps or nonzero bar origins. The clock now accepts host PPQ and last-bar-start PPQ; phrase boundaries use a stable configurable PPQ origin. A sample countdown is a current-tempo estimate and must be refreshed from host state each block.
2. Missing play state was being treated as playing. The resolver now sets false and reports the field missing; free-running sample position is separate from an assertion that host playback is active.
3. State decoding had no size limit and silently retained prior values for malformed data. Restore now caps input at 1 MiB, validates a temporary tree, defaults on invalid input, and publishes a recovery flag displayed by the editor.
4. Capture/analysis feedback was required although both capabilities are deferred. MVP status only describes shipped features.
5. Meter numerator range is aligned at 1–32 across the clock and parameter.

Additional defects found during this continuation: gain smoothing depended on host block size; .gitignore contained C++ header text. Gain now ramps over a fixed 1 ms sample count with a block-partition test, and .gitignore now excludes generated builds and IDE artifacts.

## Current high-priority gaps

| Requirement | Current status |
|---|---|
| Phrase grid and sync | PPQ clock/resolver available; no sync-mode controls, scheduler, subdivision, loop/seek policy or display integration. |
| Four scenes | Not implemented on integrated branch. |
| Five rhythmic transforms and AudioHistory | Not implemented on integrated branch. |
| Transitions and aligned dry/wet | Not implemented; masterIntensity remains output gain and zero is silence. |
| Automation and MIDI | Scalar APVTS values exist; MIDI buffer is ignored; scene/transition mapping absent. |
| Audition, undo, full state | Not implemented beyond scalar parameter state and recovery behavior. |
| UI | Current foundation editor has a slider, timing status and recovery notice; phrase/scene/queue editor absent. |
| Performance/audio QA | No integrated DSP benchmark, click residual, PDC, long-run, or listening evidence. |

A prior green CI run is not product completion. Current-branch CI remains pending. Do not claim host compatibility or audio quality without target-host/device checks.

## Research support and limitations

- JUCE 8.0.7 AudioPlayHead::PositionInfo provides optional PPQ, last-bar-start PPQ, loop and sample-position information; absent timing fields are valid host behavior. Source: https://github.com/juce-framework/JUCE/blob/8.0.7/modules/juce_audio_basics/audio_play_head/juce_AudioPlayHead.h (accessed 2026-10-12).
- The available timing helper receives only the current tempo, not future tempo-map segments. Its returned sample count is accurate only under that local tempo. The future scheduler must query the host position and tempo again each block or use a host-time event mapping; it must not count down using stale samples across tempo changes.
- Current source review does not prove JUCE/host state restore thread behavior, realtime callback allocation freedom, REAPER operation, or physical audio-device behavior.

## Verdict and next sequence

Keep the branch open and continue approved M1–M7 work. Complete the portable scene, history, transform, scheduler/mixer, trigger, state and audition modules; wire UI; measure performance before optimizing; fix QA findings; run current Windows/Linux CI with standalone artifact and launch smoke; then merge PR #2 after all feasible gates pass. Host/device gates must remain visibly pending if those environments are unavailable.
