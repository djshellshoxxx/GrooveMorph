# N02 Preset browser (factory and user presets)

Owner: W5. Status: implemented; core tests pass locally. JUCE/UI code is written against JUCE 8.0.7 and is not built locally.

## 1. Objective

One-click recall of a whole scene bank (four scenes A-D) from a built-in factory set and from per-user `.gmpreset` files, from a header bar in the editor and from the DAW's preset menu (host programs). Loading is one undoable scene-bank action and never interrupts audio.

## 2. Data model (portable, `src/core/preset/`, no JUCE)

`Preset`: `name`, `category`, `author`, `tags` (0..8), `params` (0..8 optional host-parameter values), `sceneText` (a `SceneSerializer` text bank, never a fork of that format), `factory` flag (not serialized).

Limits: name 1..48 chars, category 0..24, author 0..48, tag 1..24 chars. All strings printable ASCII (others become `_`), no commas in tags, tags lower-cased, trimmed, de-duplicated. File size cap 128 KiB. User preset count cap 512.

Optional parameters (`params`): normalised 0..1 host values for a fixed whitelist: `masterIntensity`, `dryWet`, `morphTarget`, `morphAmount`, `bandMode`, `bandCrossover`, `bandSlope`. Anything else is ignored on parse. Absent = leave the current value alone. Timing, transport, auto-return and trigger parameters are never part of a preset.

What a preset applies: the four scenes (names, operation settings, placement, lane, permissions, activity, phase). It does NOT replace the MIDI note map, the active scene, or the seed of the current bank.

## 3. File format `.gmpreset` (v1)

```
groovemorph-preset v1
name=<text>
category=<text>
author=<text>
tags=a,b,c
param.<id>=<float 0..1>
scenes
<SceneSerializer text, "groovemorph-scenes v1" ...>
```

* LF or CRLF accepted. The first line must be `groovemorph-preset v1`; any other header or version is rejected with a message, never a crash.
* Metadata ends at the line `scenes`; everything after it is the scene blob.
* Unknown metadata keys are ignored. Duplicate keys: first wins.
* The blob is parsed by `SceneSerializer::deserialize`; `Defaulted` (unreadable) rejects the preset, `Recovered` is accepted with notes. The stored blob is always the re-serialized canonical text of the parsed bank, so hostile lines never survive an import.
* Empty name after sanitising rejects the file. Non-finite floats are dropped.
* Parsing never throws and is bounded by the size cap, for any byte sequence.

## 4. Factory presets

Deterministic table in code (`FactoryPresets.cpp`), at least 24 presets (plus the first entry `Init`, the factory default bank, so a host that selects program 0 is harmless) in: Init, Stutter, Halftime, Gate Chops, Tape Stops, Builds & Fills, Subtle Groove, Experimental. Each is a valid four-scene bank using only the five existing operations (Repeat, ReverseSlice, Gate, HalfTime, TapeStop). Names are unique (case-insensitive). Explicit lane events end within the first 3 beats of the phrase (2880 ticks) so they are valid for any meter and phrase length; fills use the placement `LastBar` window instead. Factory presets are immutable: they cannot be overwritten, renamed, deleted or shadowed by a user preset of the same name.

## 5. User presets (processor layer)

Directory: `juce::File::getSpecialLocation(userApplicationDataDirectory)/djshellshoxxx/GrooveMorph/Presets`. File name = `sanitizeFileStem(name) + ".gmpreset"` (ASCII letters/digits/space/`_-.()`; no separators, no `..`, Windows reserved names suffixed, max 48 chars). Operations: save (new), overwrite (same name, explicit), rename, delete, import (any `.gmpreset`, validated and re-serialized into the user directory, never copied raw), export (serialized to a chosen path). Writes go to a temp file then replace. Files over the cap are skipped. A name matching a factory preset (case-insensitive) is refused. Unreadable files are skipped in the list.

## 6. Host integration

`getNumPrograms` = factory count + user count. `getProgramName(i)`, `setCurrentProgram(i)`, `getCurrentProgram()` index the combined list (factory first, then user sorted by name). `changeProgramName` is a no-op. The current preset name is stored as the optional property `presetName` on the existing scene child of the plugin state (older states load unchanged); restoring state re-selects the label only, it does not re-apply the preset.

Loading (`applyPreset`): message thread, under the model lock, refused while a preview/audition is active; builds the new bank from the current bank plus the preset scenes and commits it with `commitBankLocked` (one undo entry, then the normal snapshot publish, so the audio thread's existing handoff and crossfades apply and the callback gains no new locking). Optional params are then written with begin/end change gestures. A result equal to the current bank leaves the undo stack untouched (existing no-op rule).

"Modified": the UI marks the preset modified when the current bank's four scenes differ from the loaded preset's scenes (names and settings).

## 7. UI (`src/ui/PresetBar.{h,cpp}`)

Header bar: Previous, Next, a name button opening a popup menu grouped by category (with a search box and tag filter), a `*` modified marker, Save (overwrite a user preset; on a factory preset acts as Save As), Save As (name, category, tags prompt), Delete (user only, confirm), Import, Export. One `PresetBar` member in `PluginEditor.cpp`, laid out in the top strip, refreshed from the existing 10 Hz timer.

## 8. Acceptance tests (`tests/core/PresetTests.cpp`)

| ID | Check |
|---|---|
| N02-T01 | At least 24 factory presets, unique names (case-insensitive), all 8 categories present (Init plus the seven musical ones) |
| N02-T02 | Every factory scene text loads through `SceneSerializer` with status Ok and no notes |
| N02-T03 | Every factory bank: sanitize changes nothing, finite values, depth 0..1, slice 1/16..4, lock bits within `lock::all`, lane count <= 32, no same-operation overlap, events end <= 2880 ticks, name <= 32, each scene does something |
| N02-T04 | Factory table is deterministic |
| N02-T05 | Preset serialize/parse round trip exact; re-serialize identical |
| N02-T06 | CRLF, unknown keys, duplicate keys, long fields truncated, non-ASCII replaced |
| N02-T07 | Rejects: empty, wrong header, newer version, no `scenes` marker, empty name, unreadable blob, oversize |
| N02-T08 | Hostile/fuzz: truncation at every length, seeded random byte flips, NUL bytes, huge lines, 10k metadata lines: no crash, rejects or yields a valid sanitized preset |
| N02-T09 | Param whitelist, clamping, non-finite dropped, unknown ignored, max 8 |
| N02-T10 | `applyToBank` replaces the four scenes, keeps active/seed/MIDI map |
| N02-T11 | `sanitizeFileStem`: no separators or `..`, reserved names, empty, length cap, unicode |
| N02-T12 | Library: factory protected, user add/overwrite/rename/delete, case-insensitive duplicates, cap, ordering |
| N02-T13 | Library filter by search, tag and category |
| N02-T14 | Legacy fixture: a plain v1 scene text without control keys, wrapped in a preset, loads with inert control state |
| N02-T15 | Preset load through `UndoStack` is one step; undo restores the original |

Not covered by local automated tests (no JUCE locally): host program mapping, application-data file I/O, the PresetBar component, the state property round trip, and glitch-free handoff (relies on the same snapshot publish every other edit uses).

## 9. Non-goals

Cloud/sharing, morphing between presets, per-preset loops or audio, MIDI program-change mapping, audition on hover, storing the MIDI map or seed.
