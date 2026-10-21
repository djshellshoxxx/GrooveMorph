# W10-MIDI-PANIC-RATES: stuck held note after All-Notes-Off, and a documented rate table

Status: implemented by W10.

## Held note after panic

`MidiTriggerState` remembers the note that triggered the current scene (`heldNote`) so that "Return on
MIDI note-off" can go back. Hosts and controllers send MIDI "All Notes Off" (CC 123) and "All Sound Off"
(CC 120) on stop, panic and device reconnect instead of individual note-offs, so the hold stayed set until
`reset()`: the next ordinary note-off was ignored for the wrong note and a return never happened.

Behavior:
- CC 123 and CC 120 on any channel call `MidiTriggerState::releaseAll(map)`, which is exactly the
  note-off of the held note: with `ReturnToPrevious` the previous scene is requested through the normal
  scheduler (with the current transition mode), with `Ignore` the hold is just cleared.
- Nothing held: no-op. A late note-off of the same note after the panic is ignored (the hold is gone), so
  the return cannot fire twice.
- The next note-on captures the "previous" scene afresh.
- The CC still goes to the MIDI-link mailbox (a user may link CC 120/123 to something; unchanged) and is
  not counted as a note event. `midiMacros` CC 20-22 handling is untouched.
- A transport stop does not release the note (unchanged; only the MIDI messages do).

Real-time: two integer compares per CC event, no allocation.

## Rate table (documentation deliverable)

| Path | Rate / latency | Thread | Notes |
|---|---|---|---|
| CC events into the mailbox (`CcInbox::push`) | every event, sample-accurate order within the block | audio | atomic stores, per-CC value and travel counter |
| Mailbox collect + apply linked values (`applyMidiLinks`) | 20 Hz timer (about 50 ms) | message | host-parameter writes and `s:` scene writes; a fast sweep is sampled at 20 Hz; a pulse is seen (travel) but its intermediate values are not applied |
| MIDI learn decision | same timer tick; one tick decides | message | largest travel in the tick wins |
| MIDI macros (CC 20/21/22) | 20 Hz timer | message | |
| Program change / note triggers | block start/segment, sample-accurate | audio | split at the MIDI event |
| Host Variation realignment, loop-capture poll, gesture playback, evolution step | 20 Hz timer | message | evolution steps land up to about 50 ms after the phrase boundary; gesture curves have about 20 Hz resolution |
| Editor refresh | 10 Hz | message | status labels, enable states |
| Right-click menu badge poller | 4 Hz | message | |
| Visual window refresh | 30 Hz (10 Hz in Reduce motion) | message | pictures and trace |
| Visual window accessibility descriptions | at most 4 Hz, 0 Hz when unchanged | message | see W10-UI-ISSUES U7 |
| Processor timer start | once, in the constructor, only if a MessageManager exists | | a plugin created off the message thread does not run these services |

Why the timer stays at 20 Hz: every item above ends in a host notification or a model-lock edit that must
not run on the audio thread; 20 Hz keeps those bounded. The only user-visible loss it caused (a fast
pulse that returns to its start value between two ticks) is fixed by the travel counter. A faster tick, a
CC ring buffer, or audio-thread application are future options and would be new specs.

## Acceptance tests

| ID | Check | Where |
|---|---|---|
| A04-05 | releaseAll returns to previous with ReturnToPrevious, clears silently with Ignore, no-op when nothing held, late note-off ignored, fresh capture afterwards | `MidiNoteAssignTests` |
| PANIC-1 | processor: note-on 61 then CC 123 returns to the previous scene; CC 120 too; Ignore policy keeps the scene | `PluginProcessorTests` (CI only) |
| RATES-1 | the table above matches the timer constants in code (`startTimerHz(20)`, editor 10, poller 4, visual 30/10) | review (no runtime test) |
