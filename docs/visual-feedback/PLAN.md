# GrooveMorph visual feedback: research and plan

Status: **PLAN, not implemented.** Written 2026-10-14 against `main` 3ace82d (Alpha 2).
Companion: [concept-sheet.svg](concept-sheet.svg) / [.png](concept-sheet.png): a static mock with illustrative data, not a screenshot.

## 1. Goal and hard constraints

Show the user, accurately and in a way they can understand, **what the plugin is doing to the sound**:
the same information Gross Beat's animation conveys, plus what only GrooveMorph knows, but with a
visual language that does not resemble Gross Beat's. Constraints:

1. **Truthful.** Draw measured engine state, never decorative animation. Planned and measured are drawn differently.
2. **Real-time safe.** The audio callback only writes fixed-size records (no allocation, locks, I/O, logging).
3. **Portable core.** Trace → geometry mapping lives in `src/core/` (no JUCE), unit-testable; JUCE only paints.
4. **Accessible.** Colour never the only cue; every graphic has a numeric/text equivalent; reduced-motion mode.
5. **Fits small screens.** Alpha 2's 1230×700 minimum is a known defect (KNOWN-ISSUES #11); the new layout must work at 1024×768.

## 2. What Gross Beat shows (the information to match)

From the official Image-Line manual and reviews (I have not run the plugin; confirm against the real UI):

| # | Gross Beat information | Source |
|---|---|---|
| G1 | **Time mapping**: where, within a rolling 2-bar buffer, playback is reading from, as a function of position in the bar (forward, reverse, stutter, scratch, stop) | [Envelope panel](https://www.image-line.com/support/FLHelp/html/plugins/GrossBeat_EnvelopePanel.htm) |
| G2 | **Volume mapping** (gate): 100 % at top, silence at bottom | same |
| G3 | **Playback position**: a green vertical locator scrolling across the beat | same |
| G4 | **Buffer availability**: the "safety line"; time envelopes dipping below it before the buffer is full give silence | same |
| G5 | **Scratch position**: the scratching clock dial | [Hint bar](https://www.image-line.com/fl-studio-learning-content/fl-studio-online-manual/html/plugins/GrossBeat_HintBar.htm) |
| G6 | **Which slot / preset** is active (36 time + 36 volume envelopes), and the **time mix** amount | Envelope panel; [Plugin Boutique/SOS review](https://www.pluginboutique.com/articles/84) |

Its visual *form* (what we must not resemble): rectilinear XY envelope grids, a polyline curve per envelope
(green time / orange volume), a vertical sweep line, a clock dial, a "36 slot" list.

## 3. What GrooveMorph can show that Gross Beat cannot (additional information)

Everything below is already known to the engine or processor (`RhythmEngine` voices expose read position `prevR`,
rate `prevRate`, crossfade `taps`; plus `amounts[]`, `gateGain`; the processor exposes `barInPhrase`, `beatInBar`,
`activeVariation`, `queuedVariation`, `samplesUntilSwitch`, auto-return target, timing validity, source):

- **Five stages in signal order** (Repeat → Reverse → Gate → HalfTime → TapeStop) and which are active, with depth and slice.
- **Phrase context across laps**: placement window, every-N cadence, event lanes, which lap this is, what changed vs last lap.
- **Scenes A–D**: sounding, queued (with countdown to the quantised boundary), morph amount/target, auto-return target and timing.
- **Input vs output change**: level added/removed, gate attenuation (dB), wet/intensity, protected lows / downbeat.
- **Health/limits**: history fill (4 s), reported tail, timing source valid/invalid, bypassed because transport stopped, activity limit.
- **Planned vs actual**: the schedule the model intends vs what the callback really rendered.

## 4. Research findings that drive the design

| Finding | Use here | Source |
|---|---|---|
| Spiral plots reveal periodic structure when the cycle length is set to the period; repeats align radially | Phrase length is the natural period, so each lap is a ring and repeats line up | [Weber, Alexa, Müller, *Visualizing Time-Series on Spirals*](https://vis.pku.edu.cn/course/datavis_f23/material/Visualizing_Time-Series_on_Spirals.pdf); [eagereyes](https://eagereyes.org/blog/2011/spirals) |
| Rhythm as shapes on a circle (necklaces/polygons) makes rhythm structure and symmetry legible | Ring segments per effect event | Toussaint, *The Geometry of Musical Rhythm* ([overview](https://en.wikipedia.org/wiki/The_Geometry_of_Musical_Rhythm)); [Benadon circular plot](https://mtosmt.org/issues/mto.07.13.3/mto.07.13.3.benadon.php) |
| Arc diagrams connect repeated passages so structure shows at a glance | Inspiration for linking a "now" passage to its source in the weave | [Wattenberg, *The Shape of Song*](https://eliterature.org/turbulence/Works/song/method/method.html) |
| Self-similarity / recurrence matrices show where material repeats | Optional later "repeat map" of history (Phase 5) | [FMP notebook](https://www.audiolabs-erlangen.de/resources/MIR/FMP/C4/C4S2_SSM.html) |
| Magnitude → ordered channels (size, value); identity → hue/shape; never encode meaning in colour alone; Okabe-Ito palette | Colour + glyph + texture per effect | [Bertin/Munzner via arXiv 1810.10814](https://arxiv.org/pdf/1810.10814) and palette sources |
| Small multiples with identical scales beat animation for comparison | Chain stations share one scale | same |
| Telemetry to the GUI: fixed-size plain data, read the *latest window* (not drain a queue), avoid heap objects across threads, test inside a real host (data races appeared only in VST3 hosts) | Trace ring design (§7) | [JUCE forum: lock-free queues and visualization](https://forum.juce.com/t/lock-free-queues-and-visualization-of-data/20659), [AbstractFifo/TSan thread](https://forum.juce.com/t/abstractfifo-thread-sanitizer-vst3-audiopluginhost-sad-panda/34571) |

**Gap:** the searches found no precedent for a "time-warp slope diagram" used as a live effect display. The Warp Weave
is therefore a new design built from first principles (a slope/bipartite diagram of *output time → source time*),
not a copy of an existing visualisation. That is a risk as well as an advantage; it needs user testing (§10).

## 4b. Why it will not resemble Gross Beat: anti-resemblance rules

1. No rectangular XY envelope panel, no polyline "curve you edit", no horizontal-time/vertical-level graph as the main view.
2. No single vertical sweeping playhead. Progress is an **ink front**: heard = inked, upcoming = pencil outline.
3. No clock dial. Position is shown by ring laps and bead position.
4. Not green/orange. Palette is Okabe-Ito-derived per effect on a slate background, always paired with a glyph.
5. The hero form is a **thread weave** (many fine curves between two timelines), not one drawn curve.
6. Pre-release check: place a screenshot next to Gross Beat and list shared elements; any shared element is removed.

## 5. The design (see concept sheet)

Four coordinated views plus a ledger. One shared time base, one palette, one set of effect glyphs.

### 5.1 Warp Weave (hero, replaces G1/G2/G3/G4)
Two timelines: **HEARD NOW** (top, one phrase-bar or beat window) and **PLAYED** (bottom, the same span's source).
~160 fine threads, each tying a moment of output to the moment of the input it came from (read lag = `now − r`).
- Live pass-through → threads vertical. **Repeat** → fan-in to one source slice. **Reverse** → threads twist/cross.
  **HalfTime** → threads spread (source advances at ½ rate). **TapeStop** → spread that decelerates to a point.
- **Gate** → threads thin, dim and dot (opacity = measured `gateGain`), so volume mapping (G2) is on the same picture.
- Crossfade taps (3 ms) drawn as a faint second thread so users see *why* a click is absent.
- Safety (G4): source regions older than the filled history are drawn as hatched "no audio yet"; threads into them go to silence.
- Colour = effect; direction/twist = behaviour; opacity = gain; thickness = depth. Ink front separates heard from planned.

### 5.2 Phrase Rings (phrase context; no Gross Beat equivalent)
Concentric rings = laps (newest outermost, brightest). Angle = position in phrase. Arc segments = effect events with the effect
glyph texture (dotted = gate). Shaded wedge = placement window; tick = downbeat. Because rings share angle, a repeated
pattern forms radial stripes and an **evolved lap** visibly breaks the stripe. Event lanes with their own loop length (F06)
draw their own sub-ring. Bead = now.

### 5.3 Chain Stations (per-stage truth; no Gross Beat equivalent)
The real signal path left→right. Each stage: glyph, name, live amount bar (measured, smoothed `amounts`), parameter text
("1/8 · −14 dB"), active/bypassed, locked/limited badges. A thin delta strip under each station = how much that stage
changes the signal *at that stage* (needs per-stage in/out peaks, see §7).

### 5.4 Scene Orbit (scenes/transitions; extends G6)
Four nodes. Filled = sounding. Ring with countdown arc = queued; label shows the quantised boundary and beats remaining.
Filament A→B with bead = morph amount. Dashed return arc = auto-return target and bars remaining. Evolution/gesture
activity shown as a small badge on the node.

### 5.5 Change Ledger (numbers for the picture; accessibility backbone)
"Hearing audio from: 3 beats ago (−720 ms)", reading speed, gate dB, wet/intensity, added energy vs input (dB), history fill,
reported tail, timing source/validity, bar/beat. Everything in the pictures has a line here, usable by screen readers
and for bug reports (copy-as-text button).

### 5.6 Modes and states
- Default: all four views. Compact (≤1024×768): Weave + Stations + Orbit-as-badge; Rings in a tab.
- **Honest idle states:** transport stopped → views grey with "bypassed: transport stopped"; timing invalid → red banner with reason;
  preview/audition → "PREVIEW" frame so users never mistake it for the committed scene.
- Reduced motion: no animation except the ink front; all data still updates at ≤10 Hz.

## 6. Colour, shape, and type
Background slate `#15171d`, panels `#1c1f27`, ink `#ece6d6`. Effects: Repeat `#56b4e9`, Reverse `#e69f00`, Gate `#cc79a7`,
HalfTime `#009e73`, TapeStop `#f0e442`, live/neutral `#b9b4a6` (all Okabe-Ito). Redundant cues per effect:
Repeat = three repeated frames, Reverse = double chevron, Gate = square-wave notch, HalfTime = stretched sine, TapeStop = spreading dots.
Must pass: contrast ≥ 4.5:1 for text, simulated deuteranopia/protanopia/tritanopia and greyscale legibility checks on the snapshot tool output.

## 7. Telemetry architecture (the part that makes it accurate)

New portable module `src/core/trace/`:
```cpp
struct TraceFrame {                // trivially copyable, ~128 bytes, written per render segment (~256 samples)
    std::uint64_t sampleIndex;     // stream time of first sample
    double phraseBeat, beatInBar;  // from the resolved timing
    float  readLagBeats[5];        // per stage: (now − r) in meter beats; NaN = stage reads live
    float  readRate[5];            // per stage; negative = reverse
    float  amount[5];              // smoothed depth actually applied
    float  gateGain;               // measured
    float  inPeak, outPeak;        // block peaks (optionally low/high band)
    std::uint8_t activeScene, queuedScene, flags; // flags: playing, timingValid, preview, crossfading...
    float  samplesUntilSwitchBeats, morph;
};
class TraceRing;                   // single-writer (audio) / single-reader (UI), fixed capacity, per-slot sequence stamps (seqlock)
```
- **Audio side:** `RhythmEngine` gets an optional `TraceSink*` (null = zero cost). It fills a frame from state it already holds.
  Overwrite-oldest; a torn slot is detected by sequence mismatch and skipped by the reader. No allocation, no locks.
- **Bit-exactness test:** audio output with and without a sink must be identical (the sink is read-only).
- **UI side:** a 30 Hz `VBlankAttachment`/`Timer` copies the latest window (≈2 bars) and hands it to pure mapping functions
  `core/viz/` that return geometry (threads, arcs, glyph states) in normalised coordinates; JUCE `Graphics` only draws them.
- **Timing honesty:** frames carry `sampleIndex`; the UI draws at `streamPosition − hostLatencyEstimate` so the ink front is where the
  sound actually is, not where the model is. Planned data comes from the scene model, drawn with a different style.
- Capacity: 4096 frames ≈ 0.5 MB, ~20 s at 48 kHz/256-sample segments. Memory/CPU budgets verified in tests.

## 8. Phases (each is a small, separately reviewable PR; tests first)

| Phase | Deliverable | Acceptance |
|---|---|---|
| P0 | **This plan + concept sheet**; user picks/edits the concept (§10) | User approval |
| P1 | `TraceFrame`/`TraceRing` + `RhythmEngine` sink + tests | Sink on/off bit-exact; ring wrap/torn-read tests; ASan/UBSan/TSan clean; no allocation in callback (instrumented) |
| P2 | `core/viz` mapping: Weave threads from frames | Golden-geometry tests for each effect fixture (repeat/reverse/half-time/tape-stop/gate/crossfade/no-history/seek) |
| P3 | JUCE `WeaveView` + ledger, behind a Visual tab; snapshot tool renders fixtures | Paint ≤ 2 ms at 1290×740 (software renderer); readable at 1024×768; palette/CVD checks |
| P4 | `RingsView`, `StationsView`, `OrbitView` + honest idle/preview states | Each state fixture rendered; keyboard focus/accessible names; reduced motion |
| P5 | Optional: repeat map (self-similarity of history), copy-as-text, persistence of view prefs | User-driven |
| P6 | Native validation in REAPER on Windows (issue #19): visuals vs audible behaviour, CPU with GUI open/closed, standalone vs VST3 | Recorded evidence; no claim without it |

Not planned: waveform/spectrum "eye candy", OpenGL, audio-reactive decoration unrelated to processing.

## 9. Risks
- **Perceived complexity.** Four views could overwhelm. Mitigation: hero first, progressive disclosure, tooltips with plain wording.
- **Weave legibility is unproven.** Needs user trials (§10) before P3 polish.
- **Software-rendered paint cost** at high thread counts; mitigation: cap thread count by pixel width, cache `Path`s, repaint only dirty regions.
- **Host data races** appear only in VST3 hosts (JUCE forum); P6 must test in a real host, not Standalone only.
- **Single-thread assumption** of the ring: one audio thread writer only; asserted in tests.
- **Overlap with another active session** landing features on `main`: P1 touches `RhythmEngine`, so coordinate before starting.

## 10. Decisions needed from you
1. Does the **Warp Weave** concept feel right as the hero, or do you want an alternative direction explored (e.g. rings as hero)?
2. Show **planned** (pencil) future as well as heard past, or heard only?
3. Window target: keep one window with all views, or a separate resizable "Visual" window?
4. OK to start **P1** (touches `RhythmEngine`) now, or wait for the other session's work to settle?
