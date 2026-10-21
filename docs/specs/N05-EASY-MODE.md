# N05: Easy mode, Chaos macro, Surprise me, first-run guide

Status: specification (written before code, 2026-10-20). Owner: worker W8. Branch base: main `c83e8b2`.

## 1. Objective

Make GrooveMorph usable in the first minute without reading the inspector: one compact screen with
the four scenes, four macro sliders, a one-click "Surprise me", plain-language timing status, an
undo, a panic button and a short first-run guide. Add one host-automatable `chaos` knob that pushes
the sounding scene toward busier results without ever editing a stored scene.

Non-goals: no new DSP, no change to the scene file format, no new effect operations, no new
transport behavior, no persisted "never touch" preference (see 3.2), no localisation, no touch
gestures, no screenshots (UI cannot be built on the authoring machine; CI compiles it).

## 2. Parameters and state

| Item | ID / key | Range | Default | Notes |
|---|---|---|---|---|
| Chaos macro | APVTS float `chaos`, version 1, name "Chaos" | 0..1 | 0 | Appended last in `createParameterLayout`. Never reuse or reorder. |
| Easy mode toggle | state child `Ui`, property `easyMode` (bool) | - | see below | Editor/UI state, not a parameter, not automatable. |
| First-run guide seen | user settings file (`GrooveMorph.settings`), key `easy.guideSeen` | bool | false | Per user, not per project. |

Easy mode default rule (persisted in plugin state):

* A freshly created instance (no state ever set): **ON**.
* `setStateInformation` with state that has **no** `Ui` child (every project saved before N05):
  **OFF**, so existing projects keep opening on the full editor.
* `setStateInformation` with a `Ui` child: its `easyMode` value (missing or malformed property = OFF).
* `getStateInformation` always writes `<Ui easyMode="0|1"/>`. Older builds ignore the unknown child
  (they only read the children they know), and the `chaos` parameter simply does not exist for them.
* `resetToDefaults()` leaves the easy-mode choice alone (it is a view preference, not a setting).
* Legacy state without the `chaos` parameter restores it to its default 0 through the existing
  "parameters absent from the restored state return to default" path, so old projects sound identical.

## 3. Chaos macro (portable core: `src/core/control/ChaosMapper.{h,cpp}`)

```
VariationSettings applyChaos(const VariationSettings& scene, const ChaosContext& ctx) noexcept;
struct ChaosContext { float chaos; uint64_t seed; uint32_t sceneIndex; int64_t phraseTicks;
                      uint32_t neverTouchMask = 0; };
```

Pure, allocation-free, noexcept, bounded loops (5 operations x 4 candidate events). The input scene is
const; the result is a copy used only as the *sounding* scene. Stored scenes, the undo stack, the
saved state and the audition session are never touched.

### 3.1 Mapping (chaos c in [0,1]; NaN, negative or c == 0 return the input unchanged, bit for bit)

All random draws come from SplitMix64 seeded with `seed`, `sceneIndex` and a fixed salt; the draw
count per operation is fixed whether or not the operation is locked, so one lock never shifts another
operation's draws. The stream does not depend on c, which is what makes the result monotone.

For each operation `i` (draws `uE, uD, uS`):

1. **Enable**: a disabled operation becomes enabled when `c >= 0.2 + 0.8 * uE` (all unlocked
   operations are on at c = 1). Enabled operations stay enabled.
2. **Depth** (enabled operations only): `d' = d + (1 - d) * c * (0.5 + 0.5 * uD)`. A non-finite depth is
   read as 0.5. Never decreases with c.
3. **Slice** (enabled operations only): `s' = max(1/16, s * 2^(-2 * c * (0.5 + 0.5 * uS)))` (up to two
   octaves shorter). A non-finite slice is read as 0.25. Never increases with c.
4. **Event density** (only when the scene lane is `explicitEvents`; legacy-uniform lanes already play
   the whole phrase, so density there rises through steps 1 to 3): each operation owns up to 4 candidate
   extra events, resolved once, independent of c, in a fixed order. A candidate is accepted into the
   sequence when it fits in the phrase, does not overlap a protected region, does not overlap an
   original event or an earlier accepted candidate of the same operation, and the lane has capacity
   (32). Candidate `k` of an operation is *included* when `c >= 0.25 + 0.75 * (k + u) / 4` and the
   operation is enabled in the overlay, so the included set only grows with c.

### 3.2 Constraints (enforced by construction, then re-checked with `enforcePermissions`)

* Enable / Depth / Slice lock bits: that field keeps the stored value.
* Events lock bit, per-event `locked` flag: no events are added to that operation and no stored event
  is changed (stored events are never modified at all; chaos only adds).
* `placementLocked`: no events are added (the lane is placement content).
* Protected regions: no added event overlaps one.
* `neverTouchMask` (bit i = operation i): that operation is left exactly as stored, including its events.
  The processor passes 0 today because Never touch is a per-run Generator checkbox with no persisted
  state; the parameter exists so the portable contract is complete.
* Output is sanitized (`scene::sanitize`): depth 0..1, slice 1/16..4, at most 32 events, no
  same-operation overlap.

### 3.3 Where it is applied (deviation from "at snapshot publication")

Evolution replaces a scene at snapshot publication because it changes once per phrase. Chaos is
host-automatable and can move every block, so publishing from the 20 Hz message timer would add up to
50 ms of lag and flood the snapshot handoff. Chaos is therefore applied in
`GrooveMorphAudioProcessor::effectiveSettings()` (the same audio-thread overlay point as Morph), after
the morph blend, on the published snapshot copy. It is the same idea (a transient overlay of the
sounding scene; the snapshot, bank, undo stack and saved state are untouched) and keeps automation
block accurate. The audio thread reads `chaos` from a cached atomic, calls the pure function,
takes no lock and allocates nothing. At chaos 0 the function is not called, so today's code path is
bit-identical.

## 4. Easy view (`src/ui/EasyView.{h,cpp}`, layout in `src/core/easy/EasyLayout.{h,cpp}`)

Layout (top to bottom): header (title "GrooveMorph Easy", "Show guide", "Open full view"), timing
status line (plain language), four large scene buttons with state ("Sounding", "Queued", or empty),
a 2x2 grid of macro sliders (Intensity, Dry / wet, Morph, Chaos), an action row ("Surprise me",
"Undo last change", "Reset / Panic") and a one-line message area.

* The view fills the editor bounds, so it scales exactly like the editor (the editor's existing
  `setScaleFactor` fit and `setResizeLimits(1230, 700, 1900, 1100)`). `computeEasyLayout(w, h)` is pure and
  tested at the minimum size, the maximum size, and a small 640x400 case: every rectangle inside the bounds,
  none overlapping, every button at least 44 px high.
* Sliders are `MenuSlider`s attached to the host parameters `masterIntensity`, `dryWet`, `morphAmount`
  and `chaos`, so right-click, MIDI learn and automation work as in the full view.
* Morph needs a target scene; when the Morph slider is above 0 and the target is Off (or equals the
  sounding scene), the view points it at the next scene (A to B, ..., D to A) and says so in a label.
* Timing text (`describeEasyTiming`, pure, tested): "Audio is not running yet", "No usable timing...",
  "Waiting for host transport", "Following the host transport", "Using sample clock", "Using the manual
  tempo (host sends no timing)".
* Scene buttons call `requestVariation` (Transition setting applies) and first cancel a pending Surprise.
* **Undo last change**: cancels a pending Surprise when there is one, otherwise `undoSceneEdit()`.
  Enabled only when one of those can act. Host-parameter moves are not scene edits and are undone by the host.
* **Reset / Panic** (no confirmation, not destructive): cancel Surprise and the audition, cancel any queued
  scene change, chaos to 0, morph amount to 0, evolution off, gesture playback/recording off. Stored scenes,
  intensity and the transport are untouched. Factory reset stays in the full editor behind its confirmation.
* **Open full view**: `setEasyMode(false)`. The full editor has an "Easy mode" button that sets it on.
* Every new control has `setTitle` (accessible name), `setDescription`/`setHelpText` and a tooltip.
  Scene buttons describe their state in the accessible description; the status label carries its text as
  its accessible title.

## 5. Surprise me (processor API in `src/PluginProcessorEasy.cpp`, portable part `src/core/variation/Surprise.{h,cpp}`)

```
SurpriseResult buildSurprise(const VariationSettings& stored, uint64_t sceneSeed, uint32_t attempt) noexcept;
```

* Reuses `buildCandidate` with the Generator defaults (`GeneratorSettings{}`: Conservative style,
  distance 1), `seed` derived from the scene seed, `generateEventPatterns` left at its default (false)
  and `protectedOperations = 0`. Locks are enforced by `buildCandidate`.
* Tries up to 32 consecutive candidate numbers starting from `attempt` and takes the first that (a)
  differs from the stored scene in at least one operation and (b) is **audible**: at least one enabled
  operation with depth >= 0.10. If the stored scene is wholly locked or nothing qualifies, the result is
  `ok = false`, the stored scene is not touched and the UI says "Nothing to change (locked or already as
  busy as it gets)". This is the "never leaves the plugin silent" rule: a candidate that would turn every
  effect off is never offered, and the preview plays through the existing engine with the dry sound intact.
* Processor flow (message thread, `modelMutex`): refuse (`Busy`) while a preview already exists.
  Otherwise `beginAudition(activeScene)` + `setAuditionEdit(candidate)`; the candidate is heard immediately
  (preview) and a pending flag with the current phrase index is stored. The 20 Hz timer calls
  `updateSurprise()`: when the phrase index changes, or when the transport is not playing / timing is not
  usable (nothing to wait for), it calls the audition commit, which is one undoable edit. `cancelSurprise()`
  reverts the audition. The attempt counter advances on every request so a second press gives a different
  result; the sequence is reproducible from the scene seed and the press count.
* Opening the full inspector while a Surprise is pending shows the normal preview controls
  (Apply / Revert); applying or reverting there also ends the pending state.

## 6. First-run guide (`src/ui/FirstRunGuide.{h,cpp}`, steps in `src/core/easy/FirstRunSteps.{h,cpp}`)

Dismissible inline card over the Easy view, four steps: 1 set the transport, 2 pick a scene, 3 press
Surprise me, 4 open the full view. Buttons: Back, Next (last step: "Done"), Skip. Dismissing by any route
stores `easy.guideSeen = true` in the user settings file (same folder and name as the Visual window
preferences: application "GrooveMorph", suffix ".settings", user scope). It shows only in Easy view and
only while not seen; "Show guide" in the header reopens it without clearing the stored flag. The card has
an accessible title and each button has a title and description. If the settings file cannot be created
the guide still works for the session.

## 7. Real-time and threading

* Audio thread: reads one cached atomic (`chaos`); at 0 does nothing new. Above 0 it calls `applyChaos`
  (fixed loops, no allocation, no lock). The snapshot handoff is unchanged.
* Message thread: everything else. Easy mode is an `std::atomic<bool>` used by the message thread and
  the state calls. Surprise state is guarded by `modelMutex` like evolution.
* No host parameter is written from the audio thread by this feature. Plugin-originated parameter writes
  (morph retarget, panic) are bracketed with begin/endChangeGesture.

## 8. Acceptance tests

| ID | Statement | Test |
|---|---|---|
| N05-C1 | `applyChaos` is deterministic for equal inputs and changes with seed or scene index | `ChaosMapperTests` |
| N05-C2 | chaos 0 (and NaN, negative) returns the scene bit-identical; input never mutated | `ChaosMapperTests` |
| N05-C3 | Activity is monotone in chaos: enabled count, sum of enabled depths, sum of 1/slice, event count never decrease over a 101-step sweep, for several seeds and scenes | `ChaosMapperTests` |
| N05-C4 | Locks respected: enable/depth/slice/events bits, per-event locks, placement lock, protected regions, never-touch mask; `enforcePermissions` restores nothing | `ChaosMapperTests` |
| N05-C5 | Output stays valid (ranges, <= 32 events, no same-operation overlap, sanitize is a no-op); stored events kept | `ChaosMapperTests` |
| N05-C6 | Chaos 1 enables every unlocked operation and never lengthens a slice | `ChaosMapperTests` |
| N05-C7 | `chaos` parameter exists, default 0, range 0..1, is the last parameter; legacy state restores it to 0 | `PluginProcessorTests` (CI only) |
| N05-C8 | With chaos > 0 the stored bank, undo depth and saved scene state are unchanged | `PluginProcessorTests` (CI only) |
| N05-S1 | `buildSurprise` deterministic; different attempts differ; result audible and different from stored | `SurpriseTests` |
| N05-S2 | Surprise respects locks, returns `ok = false` when everything is locked | `SurpriseTests` |
| N05-S3 | Processor: request arms a preview without changing the bank; commit at the next phrase boundary is one undo step; cancel restores; Busy while previewing | `PluginProcessorTests` (CI only) |
| N05-E1 | Easy mode default ON fresh, OFF for state without `Ui`, round-trips both values | `PluginProcessorTests` (CI only) |
| N05-E2 | Layout fits and does not overlap at 1230x700, 1900x1100 and 640x400; buttons >= 44 px | `EasyViewTests` |
| N05-E3 | Plain-language timing text for each timing situation | `EasyViewTests` |
| N05-E4 | First-run steps: exactly 4, ordered as specified, navigation clamps, dismiss semantics | `EasyViewTests` |
| N05-U1 | Accessible names on every new control; Easy view shows/hides with the toggle; guide persists "seen" | Not automated: JUCE UI cannot be built on the authoring machine. Covered by code review and CI compilation only |

`PluginProcessorTests` additions are written against JUCE 8.0.7 and compile only in CI. Local evidence is
limited to the portable core tests.
