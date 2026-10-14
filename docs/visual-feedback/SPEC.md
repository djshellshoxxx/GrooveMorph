# Visual feedback: developer specification (v1)

Normative companion to [PLAN.md](PLAN.md) (rationale, research, concept sheet). Decisions recorded 2026-10-14:
**Warp Weave is the hero view; planned (future) is shown as well as heard (past); the visualisation lives in a separate,
optional, openable/closeable Visual window with additional information.** Baseline: `main` 3ace82d.
Requirement IDs: `VF-xx` (traced in [INTEGRATION-PLAN.md](INTEGRATION-PLAN.md)).

## 1. Scope and invariants

- VF-01 Audio output is **bit-identical** with tracing active, inactive, or absent. The sink is read-only.
- VF-02 Callback work added by tracing is bounded: one 96-byte frame per 128-sample hop, no allocation, locks, I/O, logging, host calls.
- VF-03 `src/core/trace/` and `src/core/viz/` are portable (C++20, standard library only, no JUCE). JUCE code lives in `src/ui/visual/`.
- VF-04 Nothing visual is saved in project/scene state. View preferences are editor-local (JUCE `PropertiesFile`/editor prefs).
- VF-05 Closing the Visual window deactivates tracing (zero overhead when closed).
- VF-06 Every graphic has a text equivalent in the Ledger; reduced-motion honoured; colour never the only cue.
- VF-07 No Gross Beat likeness: rules in PLAN §4b are acceptance criteria for the final UI review.

## 2. Trace data model (`src/core/trace/TraceFrame.h`)

```cpp
namespace groovemorph::core::trace {
inline constexpr std::int64_t hopSamples = 128;       // frame every 128 engine samples (absolute engine clock)
inline constexpr std::size_t  ringCapacity = 8192;    // power of two; ~21.8 s at 48 kHz
inline constexpr std::size_t  historyStages = 4;      // slots: Repeat=0, ReverseSlice=1, HalfTime=2, TapeStop=3
inline constexpr std::size_t  stageCount = 5;         // OperationType order

enum FrameFlags : std::uint32_t {
    TimingOk = 1u << 0,      // transport playing and timing valid
    Crossfading = 1u << 1,   // any history voice has a live crossfade tap
};

struct TraceFrame {                        // trivially copyable; sizeof == 96; multiple of 8
    std::int64_t  sample;                  // engine clock `now` of the LAST sample of the hop window
    std::uint32_t epoch;                   // increments on reset()/resetHistory(); older epochs are discarded
    std::uint32_t flags;                   // FrameFlags
    double        beat;                    // meter-beat position of `sample` (0 if !TimingOk)
    float         samplesPerBeat;          // 0 if !TimingOk
    float         intensity;               // smoothed master intensity 0..1
    float         amount[stageCount];      // smoothed applied depth per op (OperationType order)
    float         lagSamples[historyStages];// t - r for each history voice; 0 when voice inactive
    float         rate[historyStages];     // read rate (-1 reverse, 0.5 half, 1.0 live...); 1 when inactive
    float         gateGain;                // measured gate gain 0..1
    float         inPeak, outPeak;         // max |sample| over channels and over the hop window
};
static_assert(std::is_trivially_copyable_v<TraceFrame> && sizeof(TraceFrame) == 96);
}
```
Invariants: all floats finite (engine sanitises: non-finite → 0, rate → 1); `lagSamples` in `[0, historyCapacity)`;
`amount`, `intensity`, `gateGain`, peaks in `[0, +inf)`, amounts/intensity/gate ≤ 1.

## 3. TraceRing (`src/core/trace/TraceRing.h/.cpp`)

Single audio-thread writer, single UI reader, overwrite-oldest, fixed capacity, **formally race-free**: each slot is
`std::array<std::atomic<std::uint64_t>, 12>` (the frame's 96 bytes as words, `std::memcpy`) plus `std::atomic<std::uint64_t> seq`.

- `void setActive(bool) noexcept` / `bool active() const noexcept` (relaxed atomic; default false).
- Writer `void push(const TraceFrame&) noexcept`: `i = writeCount++`; `seq.store(2*i+1, relaxed)`; `atomic_thread_fence(release)`;
  store 12 words relaxed; `seq.store(2*i+2, release)`; publish `writeCount` with release.
- Reader `std::size_t copyLatest(TraceFrame* out, std::size_t maxFrames) const noexcept`: acquire `writeCount`; for the newest
  `min(maxFrames, capacity-1)` indices (oldest→newest): `s1 = seq.load(acquire)`; require `s1 == 2*i+2`; copy words (relaxed);
  fence(acquire); require `seq.load(relaxed) == s1`; else skip that frame (torn or overwritten). Returns count written in ascending order.
- Allocation only in the constructor (`std::vector<Slot>(ringCapacity)`); `static_assert(std::atomic<std::uint64_t>::is_always_lock_free)`.
- Writer-only misuse (second writer) is a contract violation, documented and tested by single-writer stress.

## 4. Engine sink (`src/core/dsp/RhythmEngine.*`)

- `void setTraceSink(trace::TraceRing* sink) noexcept` (not realtime-safe to call concurrently with `process`; called from `prepareToPlay`/editor-open via the processor's existing safe handoff, see INTEGRATION-PLAN S3).
- In `process()`: members `inAcc`, `outAcc` (peak accumulators). Per sample: `inAcc = max(inAcc, |dry|)` over channels, after writing `data[n]` `outAcc = max(outAcc, |y|)`.
  At the end of the per-sample iteration, if `sink && sink->active() && (now % hopSamples == hopSamples-1)`: build a frame from current state
  (`now`, `epoch`, `beat`, `spb`, `smoothedIntensity`, `amounts`, per-voice `now - prevR` when `valid && amount>0` else 0, `prevRate`, `gateGain`, `inAcc`, `outAcc`),
  `push`, reset accumulators. Frame cadence is on the **absolute engine clock** so it is identical for every block split (VF-01 test).
- `epoch` increments in `reset()` and `resetHistory()`. Frame emission never alters engine state.

## 5. Net-path composition (`src/core/viz/PathResolver.h/.cpp`, pure)

Chain order: Repeat → Reverse → Gate → HalfTime → TapeStop, then master mix `y = dry + I·(x − dry)`.
For an output engine-time `t` and the frames window `W` (one epoch, ascending):

```
tau = t; weight = 1; for stage in [Mix, TapeStop, HalfTime, Gate, Reverse, Repeat]:
  f = interpolate(W, tau)               // linear between neighbouring frames; nullopt -> NoData
  Mix:    a = f.intensity;   if a < 0.5 {return {src=t, weight=1 (live)}}  weight *= max(a, 1-a)
  History stage k: a = f.amount[op]; if a >= 0.5 { tau -= f.lagSamples[slot]; weight *= a } else weight *= (1-a)
  Gate:   a = f.amount[Gate]; weight *= (1 - a + a * f.gateGain)
result: {sourceSample = tau, weight (0..1), path flags: which stages were wet, crossfading}
```
- Output: `ResolvedThread { double sourceSample; float weight; std::uint8_t wetMask; bool noData; }`.
- Edge cases: `tau` before the first frame or in a different epoch → `noData=true` (drawn hatched, VF "no audio yet");
  `TimingOk` false → weight 0, `wetMask=0`; `tau` never increases (clamp `lag>=0`). Deterministic for identical inputs.
- Beat conversion: `beat(tau) = f.beat + (tau - f.sample) / f.samplesPerBeat` using the frame at `tau`.

## 6. Planned future (`src/core/viz/PlannedPath.h/.cpp`)

The per-op read-position maths currently inline in `RhythmEngine::process` is extracted to pure functions
`StageGeometry::readOffset(op, positionInCell, len) -> {lag, rate}` used by **both** the engine and the planner (single source of truth;
refactor must keep all existing engine tests and the VF-01 test bit-exact). `PlannedPath` evaluates the *committed* scene snapshot
(`VariationSettings`, placement mask, queued switch boundary) forward in beats using the same function and returns
`ResolvedThread` with `planned=true`. Planned threads are drawn as pencil outline and are marked **tentative** across a queued scene
boundary, an auto-return, an audio trigger or a gesture/evolution step (shown as boundary markers, not predicted).

## 7. Weave geometry (`src/core/viz/WeaveLayout.h/.cpp`, pure)

Input: `WeaveParams { double viewStartBeat; double spanBeats /*1..8, default 4*/; double leadBeats /*2*/; int threads /*16..256*/; double nowBeat; }`,
frames window, planned source. Output: `std::vector<Thread>` (preallocated by the caller, size ≤256) with
`{ float xTop, xBottom /*0..1 over [start-lead, start+span]*/; float opacity; float width; std::uint8_t effectMask, primaryEffect; bool planned, noData, gateClosed; }`.
- `xTop = (beatOut - (start - lead)) / (span + lead)`; `xBottom` likewise from `beat(sourceSample)`; same scale on both rows.
- `opacity = clamp(0.18 + 0.62*weight, 0.05, 0.85)`; planned ×0.6; `gateClosed = gateGain<0.25` → dotted.
- `primaryEffect` = the last wet stage in chain order that moved the thread (priority: TapeStop, HalfTime, Reverse, Repeat, none→Live); `effectMask` keeps all.
- Thread count adapts: `min(threadsParam, pixelWidth/3)`; geometry unit-tested without rendering.
- **Ink front**: heard/planned boundary at `nowBeat`; heard = inked, planned = pencil. Not a sweeping line.

## 8. Other views (data derivation)

| View | Inputs | Derived values |
|---|---|---|
| Phrase Rings | processor status (`meterNumerator`, `phraseBars`, bar/beat), scene placement/events, trace flags | lap ring = last N laps; arc per event; "lap vs previous lap" diff mask (events/depth/slice/scene differ); window wedge; downbeat tick; per-lane loop sub-ring |
| Chain Stations | newest frame, scene settings | per stage: amount bar (`amount`), text (slice, dB from `gateGain`, rate), active/bypassed/locked/limited, delta strip = `20·log10(out/in)` per stage if per-stage peaks available (Phase P5; v1 shows overall) |
| Scene Orbit | processor: `activeVariation`, `queuedVariation`, `samplesUntilSwitch`, morph target/amount, auto-return target/remaining | queued countdown beats = `samplesUntilSwitch/spb`; arcs |
| Change Ledger | newest frame + processor status | see §9 list; "copy as text" |

Additional information shown only in the Visual window: per-effect deep numbers, threads inspector (hover a thread → "output beat 3.25 ← source 1.75, Reverse + Repeat, 62 %"),
history ruler with fill, lap diff list, trace health (frames/s, dropped/torn count), timing source, bypass reason.

## 9. Ledger fields (exact text)

`Hearing audio from` (`beats ago`, ms), `Reading speed` (×, direction), `Gate` (dB, % of bar closed), `Wet / intensity` (%),
`Added vs input` (dB, `20·log10(RMS(out)/RMS(in))` over the last 100 ms of frames, `—` below −90 dB), `History` (`s / 4.0 s`), `Reported tail` (s),
`Timing` (`Host PPQ|Sample Clock|Manual`, valid/invalid + reason), `Position` (bar/beat), `Scene` (sounding/queued/preview), `Trace health`.
Idle states: transport stopped → "Bypassed: transport stopped"; invalid timing → reason string; preview → "PREVIEW: not the committed scene".

## 10. Visual window (`src/ui/visual/`)

- `VisualWindow : juce::DocumentWindow`, owned by the editor, created lazily by a **Visual** button in the main header; `closeButtonPressed` hides and calls `ring.setActive(false)`.
  Resizable, min 720×480, default 1100×680, not always-on-top, position/size/tab remembered in editor prefs (not scene state).
  Editor destruction closes it. If a host refuses a secondary desktop window, fall back to an in-editor overlay panel (setting `visualInline`).
- Refresh: 30 Hz `juce::VBlankAttachment` (fallback `Timer` 30 Hz); data copy via `copyLatest(buffer, 2048)` into a preallocated vector; paint ≤ 2 ms at 1100×680.
- Components: `WeaveView`, `RingsView`, `StationsView`, `OrbitView`, `LedgerView`; layout: Weave top-left (≥55 % area), Ledger right, Rings/Stations/Orbit bottom row; below 900 px width Rings becomes a tab.
- Accessibility: `AccessibilityHandler` per view with the ledger text as value; keyboard: Tab between views, arrows move the hover-thread cursor, `C` copy-as-text.
- Reduced motion: `juce::Desktop::getInstance().isReduceMotionEnabled()` (else a setting): no easing; ink front steps at 10 Hz.

## 11. Palette and glyphs

Slate `#15171d`, panel `#1c1f27`, ink `#ece6d6`; Repeat `#56b4e9`, Reverse `#e69f00`, Gate `#cc79a7`, HalfTime `#009e73`, TapeStop `#f0e442`, live `#b9b4a6`, no-data hatch `#4a4f5e`.
Glyphs: Repeat 3 frames, Reverse double chevron, Gate square-wave notch, HalfTime stretched sine, TapeStop spreading dots (vector `Path`s in `src/ui/visual/Glyphs.cpp`).
Checks: text contrast ≥ 4.5:1; CVD simulation + greyscale snapshot review; hatch/dots/dash carry meaning without colour.

## 12. Tests (all precede implementation; IDs used in the integration plan)

- VF-T01 ring: capacity wrap, ordering, overwrite, torn-read rejection, epoch discard; two-thread stress (TSan in CI where available).
- VF-T02 engine bit-exactness: for 8 fixtures × block sizes {1,7,64,128,513} × sink {null, inactive, active}: output bytes identical; frames identical across block splits.
- VF-T03 frame truth: repeat/reverse/half-time/tape-stop/gate fixtures: `lagSamples`/`rate`/`gateGain` equal analytic values within 1e-3 samples; reset/seek → new epoch.
- VF-T04 PathResolver: single stage each; composed chains (Repeat+Reverse, Reverse+HalfTime, all five); dominant-branch rule at amount 0.49/0.5/0.51; NoData at window start/epoch change; monotonic `tau`.
- VF-T05 StageGeometry equals engine read positions sample-for-sample (property test over random settings).
- VF-T06 WeaveLayout: golden geometry for the six canonical bars (live, repeat, reverse, half-time, tape-stop, gate), thread cap, shared-scale invariant.
- VF-T07 Ledger text formatting, units, idle/invalid/preview strings; copy-as-text golden.
- VF-T08 UI smoke: each view paints for fixtures without assertion; snapshot tool output stored for review; paint-time budget check (non-gating warning on CI, gating locally).
- VF-T09 Window lifecycle: open/close/destroy order, ring active flag follows window, no tracing when closed (frame count 0).
- VF-T10 Processor integration: Visual open/closed does not change processor output (existing PluginProcessorTests extended).

## 13. Performance and memory budgets

Callback: tracing adds ≤ 1 % of a 64-sample block time (measured by an offline benchmark, recorded not asserted); ring 786 KB;
UI copy ≤ 2048 frames × 96 B = 192 KB per refresh; layout ≤ 0.3 ms; paint ≤ 2 ms (software renderer). Record measured values; do not claim unmeasured ones.

## 14. Non-goals / deferred

Per-stage in/out peaks (delta strips) → P5; repeat/self-similarity map → P5; spectrum/waveform decoration; OpenGL; persisting view state in projects; CLAP.
