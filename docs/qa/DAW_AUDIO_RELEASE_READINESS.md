# GrooveMorph DAW compatibility, audio-quality and release-readiness audit

Baseline reviewed: main `5e9a99f08c5ce75a768b9c0486eba253e9621010` (2026-10-13 UTC).
Scope: source-level review of JUCE host adapter, portable DSP, state, UI, CMake,
existing tests and Github Actions. This is a verification ledger, NOT a certification.
Update the baseline and attach run links when closing a gate.

## Current decision

**Engineering build: YES. DAW-qualified public beta / production use: NOT YET.**
Windows/Linux VST3 and Standalone compiled and existing core/processor tests,
portable ASan/UBSan and a Windows five-second Standalone startup check passed
on main at the baseline: Actions runs 38488338777, 38488338555, 38488338610.
The startup test does not play or capture audio. No REAPER (or any DAW) host
session, pluginval, VST3 SDK validator or measurement-based listening verdict
had been recorded at this baseline.

## DAW / VST3 compatibility review

| Gate | Code or evidence | Acceptance | Baseline |
|---|---|---|---|
| Format and platform | CMakeLists.txt JUCE 8.0.7, VST3 and Standalone | Win x64/Linux x64 builds | PASS CI |
| Main buses | PluginProcessor.cpp isBusesLayoutSupported() | Mono->mono, stereo->stereo; reject mismatched layouts in native hosts | HEADLESS TEST ONLY |
| Parameter identity | PluginProcessor.cpp createParameterLayout | Stable IDs, host read/write, automation and gestures over save/reopen | PARTIAL |
| State/preset recall | getStateInformation/setStateInformation | DAW project reopen, full bank/MIDI map, old state, corruption recovery, multiple instances | HEADLESS TEST ONLY |
| Transport | TransportResolver, Scheduler, process | Tempo/meter change, seeks, loops, stopped/offline playback, odd meters, PPQ-less hosts | HEADLESS TEST ONLY |
| MIDI | timestamped processBlock inputs and note mapping | REAPER sends to audio FX; real MIDI note-on/off, collision warning | HEADLESS TEST ONLY |
| Bypass/PDC/tail | reported zero latency, smoothed dry bypass, reported zero tail | Native PDC, freeze/render and effect-tail behavior validated | OPEN |
| Plugin/GUI lifetime | createEditor and 10 Hz status timer | Scanner opens/closes editor repeatedly; resize, scale, focus, unload/reload no deadlocks | OPEN |
| VST3 independent validator | .github/workflows/pluginval.yml | pluginval strictness 5 or better on real VST3 bundle (GUI separately) | NEW GATE, RESULTS PENDING |
| Steinberg SDK validator | VST3 SDK validator | Strict contract/parameter/bus/process checks | OPEN |
| Named hosts | REAPER Windows/Linux; secondary Bitwig, Cubase/Ableton/FL Studio as available | Scan, load, save/reopen, automation, rendering, note input, GUI; capture host/build/version | OPEN |

Potential contract issue: `getTailLengthSeconds()` currently returns zero while
repeat/reverse/rate engines can read prior samples for up to their bounded history
window. Verify the actual post-input audible tail and choose a bounded host tail
report from measured fixtures before changing it. `getLatencySamples() == 0`
is plausible for causal lookbehind, but remains subject to impulse/PDC checks.

A03 remains: buttons/MIDI can select a sounding variation while the host-facing
Variation parameter is stale. This can confuse recorded automation and host display.
Do not notify the host from the audio callback. Specify requested versus playing
state and use a safe off-audio feedback strategy before closure.

## Audio quality and DSP review

Portable `RhythmEngine` has preallocated four-second/channel histories,
fixed operation order, causal sampling, three-millisecond history-voice crossfades,
two-millisecond gate ramps, intensity smoothing and bounded ring reads.
`PluginProcessor` adds a ten-millisecond bypass ramp. The tests verify bit-exact
dry null at intensity zero, deterministic restart, finite output, seek reset,
selected operation fixtures and bounded transitions. These are meaningful
functional checks, not calibrated audio-quality measurements.

**Missing objective evidence:**
- Offline null on fully wet-inactive scene at 44.1/48/96/192 kHz, mono/stereo;
  dry/wet = 0, effect intensity = 0, and bypass steady state.
- Impulse, low-frequency sinusoid, swept sine, white/pink noise, drum/transient and
  silence-after-signal fixtures per operation, across 16/64/127/256/1024-sample
  block sizes; include sample-accurate MIDI transitions and tempo/seek loops.
- Time-domain max sample step and short-window RMS around slice boundary,
  note trigger, history invalidation, scene switch, bypass and automation ramp.
  Compare against input/same-energy reference; capture before declaring a click.
- Finite/non-denormal and maximum absolute output peak; multichannel agreement
  only for deliberately identical stereo inputs; measure changed-gain correlation.
- High-frequency alias/sideband energy for resampling-based HalfTime/TapeStop
  across 44.1, 48, 96 kHz. Acceptable alias levels require an explicit target.
- CPU callback p50/p95/p99/worst duration as % of buffer time, malloc/free and
  lock/heap instrumentation, memory/voice boundedness, long-run soak and teardown.
- Host freeze, offline bounce vs real-time render comparison, editor-open CPU
  overhead, and listening A/B at matched loudness on recorded musical fixtures.

Do not use generic THD as a pass/fail measure for intentionally time-warped output.
Establish a reference signal and bounded click/alias thresholds for each mode.
The short causal reverse is previous-slice reversal rather than noncausal
future-audio reconstruction; half-time is rate change, not pitch-preserving stretch.

## Real-time / production engineering

- Audio-thread `pullScene()` uses a try-lock and fixed snapshot, avoiding a wait;
  confirm copy size and instrument worst-case callback under concurrent edits.
- DSP storage allocates during prepare, not the process loop. `resetHistory()`
  invalidates in O(1) on seeks. Test invalid and extreme samplerates/block sizes.
- Host state save/restore and model changes use mutex/XML/strings off the DSP
  path in intended use. Some hosts may call state methods from threads other than
  the message thread; test concurrency assumptions explicitly.
- `MidiBuffer` iterator processing and scheduler splits are bounded by supplied
  MIDI event count; fuzz dense events and oversized host buffers to measure CPU.
- Release inventory absent: product license, JUCE distribution terms, any bundled
  dependencies, versions/build SHA, signing, consistent Win/Linux VST3 layout,
  clean-machine installation/removal, artifact reproducibility, support contacts.
- The CI Standalone EXE is an engineering artifact, not a signed installer.
  Linux portability against target distributions/glibc needs deployment testing.

## Native REAPER acceptance checklist (Windows and Linux separately)

Record date, OS/build, REAPER version, architecture, plugin SHA, interface/audio
driver, samplerate/buffer, plugins path and screenshots/log links.

1. Fresh scan, instantiate on stereo track and mono track, close/reopen editor,
   resize at 100/150/200%, save and reopen project with four distinct scenes.
2. Confirm MIDI note routing to FX with note 60–63, one duplicate-note warning,
   alternate note-off policy and exact sample-offset switching.
3. Automate dry/wet, intensity, bypass, transition, timing and Variation
   repeatedly while recording; verify requested-vs-active behavior separately.
4. Change 4/4 -> 7/8, tempo, loop points, seek backward, stop, restart,
   sample-clock and PPQ-only timing; compare status and recorded outputs.
5. Render/freeze track offline, re-import output and compare to real-time loop;
   check tails and expected zero algorithmic latency.
6. Stress load/unload, multi-instance, high sample rates, small block sizes,
   silent input, MIDI floods and GUI opening during playback. Capture metrics.
7. Run pluginval strictness 5+ and the SDK VST3 validator; archive raw logs
   and triage all failures before designating a beta release.

## Priority and status

P0: native REAPER acceptance, pluginval/VST3 checks, actual audio fixtures,
real-time performance instrumentation, licensing and versioned packaging.
P1: A03 Variation host feedback, tail reporting check, sample-accurate
automation/multichannel/host edge cases, usable scene copy/undo.
P2: accessible scalable editor, preset browser and advanced sound-design modules.

No native DAW/listening/installer/CPU-timing work was executed by this document.
The new pluginval workflow is the first independent executable host-contract gate;
its outcome must be recorded after CI, and failure must remain a release blocker.

Independent validator references:
- https://github.com/Tracktion/pluginval
- https://github.com/Tracktion/pluginval/blob/develop/docs/Adding%20pluginval%20to%20CI.md
- https://steinbergmedia.github.io/vst3_dev_portal/pages/What%2Bis%2Bthe%2BVST%2B3%2BSDK/Validator.html
