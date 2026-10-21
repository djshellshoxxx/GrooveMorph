# W3-TIMER-EVOLUTION: message-thread timer safety and phrase-exact evolution steps

Status: SPEC (2026-10-20, base `c83e8b2`). Closes KNOWN-ISSUES #4 (processor timer) and the "Evolution steps land up to ~50 ms after the phrase boundary" note (F05 section 2).

## 1. Problems

1. The 20 Hz `juce::Timer` is started in the constructor only when a MessageManager exists at that moment, and stopped in the destructor on whatever thread destroys the plugin. A host that creates the plugin off the message
   thread gets no services; `Timer::stopTimer()` from another thread races the timer queue, and an in-flight callback can run on a processor that is being destroyed (use-after-free).
2. Evolution steps are decided on the timer thread, so a step is audible up to one timer period plus one block after the phrase boundary.

## 2. Timer design

`ServiceTimer` replaces the `private juce::Timer` base of the processor. State object `ServiceTimer::Core` is shared (`std::shared_ptr`) between the processor and queued messages:

- Lazy start: `ensureServices()` (any thread, idempotent, lock-free fast path) is called from `prepareToPlay`, `createEditor`, and the message-thread service entry points
  (`beginMidiLearn`, `armLoopCapture`, `setEvolution`, gesture setters, `setMidiMacrosEnabled`). The constructor no longer starts anything.
- Start thread: on the message thread it starts the Timer directly; on any other thread it posts `MessageManager::callAsync` (a message holding the shared Core). If no MessageManager exists nothing is started and
  `ensureServices()` retries on its next call (the message may later exist). Failure to post is not an error.
- Callback: `timerCallback` runs on the message thread, takes `core->mutex`, and calls the owner only while `core->owner != nullptr`.
- Destruction: `~Processor` calls `core->detach()` from ANY thread: it locks the mutex (waits for an in-flight tick), sets `owner = nullptr`, and then stops the Timer on the message thread
  (directly if already there, else via `callAsync` holding the Core). No call can reach the processor after `detach()` returns; the Core lives until its last queued message runs.
  A tick can never run the destructor of its own processor on another thread while it executes.
- The timer interval stays 20 Hz. Idle cost: the callback returns immediately when nothing is armed (cheap checks already in each service).

## 3. Evolution step timing

Message thread decides nothing about WHEN. Per publish it prepares the NEXT step's candidate and the audio thread swaps it in at the exact phrase boundary.

- `PublishedScene` gains `EvolutionPlan { bool active; std::uint8_t scene; std::uint8_t every; std::uint32_t nextStep; VariationSettings next; }` (`next` is the evolved settings of step `nextStep`, built with
  `buildCandidate(stored, candidate = nextStep - 1)` under the same lock rules; `active` is false while auditioning or when evolution is off). Snapshot version semantics unchanged.
- Audio thread (`process`): evolution state `evoPhraseSeen` (int64, -1 = unknown). At every render segment start with a playing, valid timing and an active plan:
  `phrase = floor(positionInPhrases + 1e-9)`. The segment loop cuts the segment at the next phrase boundary (sample = ceil of the remaining phrase fraction times samples per phrase; only when it lies strictly inside the segment).
  When `phrase != evoPhraseSeen`: if `evoPhraseSeen >= 0` and `phrase mod every == 0` the audio thread copies `plan.next` into `audioScene.snapshot.variations[scene]` and publishes `evolutionAppliedStep = plan.nextStep` (relaxed atomic);
  in all cases `evoPhraseSeen = phrase`. A new plan version (`setEvolution`, edit) resets `evoPhraseSeen` to the current phrase without applying (no step inside the first phrase after enabling, same as before).
- Seeks: a backward or forward jump (host discontinuity) resets `evoPhraseSeen` like a new plan; a step is applied only when the phrase index advances to a new qualifying phrase while playing.
- Message thread (`updateEvolution`, timer and tests): if `evolutionAppliedStep > evolutionStep` it sets `evolutionStep` and republishes (which builds the following step). `evolutionStep` therefore follows the audio thread;
  `getEvolutionStep()` keeps its meaning. `publishSceneLocked` uses `max(evolutionStep, applied)` so an edit made between the swap and the timer never reverts a step.
- Accuracy: the swap lands on the first sample of the phrase (segment cut), subject to the engine's usual 3 ms ramps; the SpinLock try-lock cannot delay it because the audio thread applies from its own copy.
  Residual limits (documented): the message thread needs one timer period to build the NEXT step, so a publish that invalidates the plan (an edit) while a boundary is less than ~50 ms away may apply the previous plan's step
  or skip one step; plans are rebuilt ahead of time at every publish, so normally the next step is ready a full phrase early.

## 4. Tests (`tests/W3ProcessorTests.cpp`, `tests/PluginProcessorTests.cpp` existing F05 cases unchanged)

| ID | Check |
|---|---|
| W3-TM-T01 | Constructing and destroying a processor on a non-message thread (std::thread) does not crash, starts no timer, and `ensureServices()` from that thread is safe. |
| W3-TM-T02 | Destruction while the owner is mid-tick (tick body blocked by a latch) waits for the tick and never calls the freed owner afterwards (Core-level test with a fake owner). |
| W3-TM-T03 | `ensureServices()` is idempotent (100 calls start one timer), and `detach()` twice is safe. |
| W3-TM-T04 | Existing F05 tests pass: no step in the first phrase, step at the boundary, again next phrase, stored scene untouched. |
| W3-TM-T05 | The step lands exactly on the boundary sample: process 1-sample blocks around the boundary; the evolved settings (observed through the rendered output of a scene whose evolution changes the active effect) start at the first sample of the phrase, not later. Repeated for blocks 64/257/1024 (block-split independent). |
| W3-TM-T06 | Seek backwards across a boundary neither skips nor double-fires a step (F05-T10). |
| W3-TM-T07 | Auditioning suppresses the plan; leaving the audition does not apply a stale step. |
| W3-TM-T08 | Edit between swap and timer tick does not revert the step. |

## 5. Non-goals

A dedicated thread, sub-block timing of gesture playback and MIDI-learn application (still 20 Hz), UI changes.
