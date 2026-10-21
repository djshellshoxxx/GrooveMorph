# UI-REDESIGN: complete GUI rework (beta.2)

Status: spec for implementation, 2026-10-21. Replaces the fixed 1290x784 layout with a proportional, themed,
single-window "Studio" view. Easy mode stays and is restyled. No audio, parameter, state or scene-key changes.

## 1. Research basis

Sources: Effectrix 2 manual and MusicRadar review, Gross Beat manual, Stutter Edit 2, Glitch 2, Output Movement,
ShaperBox 3, Arturia, Excite reviews; WCAG 2.2; JUCE forum threads on scaling and OpenGL; Benadon (circular vs linear
rhythm plots); Okabe-Ito palette. Items marked unverified by the research agents are treated as guidance only.

Rules adopted:

1. One dominant central editor: the event timeline gets the largest area (Effectrix, Gross Beat, ShaperBox).
2. One colour per effect, always paired with a shape/icon and a text label, same colour everywhere (lane, rack card,
   Euclid, Visual window). Today two palettes exist; the Okabe set in `core/viz/Palette.h` wins.
3. One hero macro surface (Chaos with Surprise) plus Intensity, Dry/Wet, Morph, matching Output Movement's X/Y idea.
4. Scene pads are big, labelled, show sounding/queued state with text, and sit next to the morph control.
5. Easy vs advanced: Easy view stays a separate full-window mode; the Studio view uses progressive disclosure
   (rack and timeline visible, deep settings in tabs).
6. One preset concept (browser in the header, dirty marker), no confusing preset types.
7. Avoid what reviewers criticise: faint grey labels, floating unsectioned controls, hidden randomise targets
   (generator keeps its locks), missing basics (bypass, MIDI learn stay one click away).
8. Linear timeline for editing; ring only as read-only Euclid preview (Benadon).
9. Chance shown as event height plus a dotted top edge, never opacity alone.
10. Type never below 11 px, values 13 to 14, text contrast AA, 2 px focus ring, targets at least 32 px.
11. Layout from `getLocalBounds()`, one animation timer, no allocation in `paint()`, software renderer.

## 2. Window

Default 1180x760, minimum 960x640, maximum 2200x1400 (constants in `ui/theme/Theme.h`). Resizable. Everything is laid
out proportionally from `getLocalBounds()` using FlexBox/Grid or computed rectangles; no absolute y offsets. If the
display is smaller than the default the existing fit logic (setScaleFactor) is kept. Added: UI scale menu
(75, 100, 125, 150, 200 percent) in the settings menu, stored in plugin state child `Ui` as `scale` (append-only key,
default 100; applied with `setScaleFactor`; independent of host DPI).

```
+------------------------------------------------------------------------------------------+
| HEADER 48: GrooveMorph | [<] Preset name * [>] [Save v] | ..... | Play Stop | Easy Visual  |
|                                                                   Bypass  Settings(gear) |
+------------------------------------------------------------------------------------------+
| PERFORMANCE BAND (about 128)                                                             |
| [A scene pad][B][C][D]  Transition v  [Trigger][Cancel]  |  (o)Intensity (o)Dry/Wet     |
| pads >= 112x64, 4 equal                                    (o)Morph [target v] (O)CHAOS  |
|                                                            [Surprise me][Undo][Redo]     |
+------------------------------------------------------------------------------------------+
| TIMING STRIP 28: phrase cursor bar (painted) | timing text | status text                 |
+------------------------------------------------------------------------------------------+
| EFFECTS RACK about 112: Editing [A v] [name] [Copy FX]  Audition: [Preview][Hear][Apply]  |
|  7 cards (equal width): colour bar + icon + name, Enable toggle, Depth, Slice           |
+------------------------------------------------------------------------------------------+
| WORKSPACE (fills the rest, at least 300 px): TabbedComponent, tabs at top                |
|  Pattern | Generate | Scenes | Morph | Limits | Input | Perform | Print | Info           |
|  Pattern = LaneCanvas timeline full width (+ Euclid drawer on the right, 280 px, toggle) |
|  other tabs: two or three column forms using the full width                              |
+------------------------------------------------------------------------------------------+
| FOOTER 24: hint line for hovered control | MIDI note field + status | CC link status      |
+------------------------------------------------------------------------------------------+
```

Heights: header 48, band 128, strip 28, rack 112, footer 24, gaps 12 each = about 400, workspace gets the rest
(360 at the 760 default, 240 at minimum height 640 where the Euclid drawer collapses).

## 3. Design system (`src/ui/theme/`)

- `Theme.h` (exists): colours, effect colours, spacing, type scale, geometry limits. No raw hex elsewhere in UI files.
- `GrooveLookAndFeel.{h,cpp}` (LookAndFeel_V4 subclass, applied once to the editor and inherited by children; the
  Visual window and PopupMenus use it too):
  - Rotary knob: 270 degree arc, 4 px rounded value arc in accent (hero knob arc in the chaos colour `accent`,
    other knobs `textDim` track plus `text` value arc), centre value text 13 px, label under in 12 px.
    Hero (Chaos) is 1.3x the size of the others. Min knob diameter 56 px, hero 72 px.
  - Linear slider: 4 px track, 16 px round thumb, filled part in accent. Min height 28.
  - Buttons: `surfaceRaised` with 1 px `stroke`, radius 6, hover `surfaceHover`, on-state accent with
    `textOnAccent` text, 2 px `focusRing` when keyboard focused. Toggle buttons draw a visible check/dot.
  - ComboBox, PopupMenu, TextEditor, Label, TabbedButtonBar (flat tabs, selected = accent underline 3 px),
    ScrollBar (8 px thin), TooltipWindow (surfaceRaised, 12 px), AlertWindow all themed.
  - Default font from `Theme::font`. Fonts are the system sans (no embedding in this release).
- Effect glyphs: reuse `ui/visual/Glyphs.cpp` `drawGlyph` for rack cards and lane labels (shape redundancy).
- Rendering: software renderer. `setBufferedToImage(true)` on static panels. One 30 Hz `Timer` in the editor for
  live visuals (keep the existing 10 Hz state timer and add repaint of only the dirty rectangles: playhead column,
  phrase cursor, scene pads). No OpenGL.

## 4. Components and ownership

New files live in `src/ui/shell/`; each is a small Component with a plain public API. `PluginEditor` only composes them.

| Component | File | Contents (all existing wiring preserved) |
|---|---|---|
| `HeaderBar` | shell/HeaderBar | wordmark, `PresetBar` (restyled, in the header), Play, Stop, Easy mode, Visual, Bypass (attachment `bypass`), settings gear menu: Demo loop, Reset to defaults, UI scale, Show first-run guide, Open help, Reduce motion, tooltips on/off (UI-only, user settings file) |
| `PerformanceBand` | shell/PerformanceBand | 4 `ScenePad` buttons (name, A..D, sounding/queued text and colour, shortcut hint 1..4), Transition combo (attachment), Trigger, Cancel, 4 rotary `MenuSlider` knobs Intensity, Dry/Wet, Morph, Chaos (hero) with attachments and MIDI-learn menu, Morph target combo (attachment `morphTarget`), Surprise me, Undo, Redo |
| `TimingStrip` | shell/TimingStrip | painted phrase cursor from `phraseSlider` data, timing text, status text, tooltip area |
| `EffectsRack` | shell/EffectsRack | Edit-scene combo, name editor, Copy FX, Audition group (Preview, Hear stored/edited, Apply, Revert), 7 `EffectCard` (glyph, colour bar, name, Enable toggle, Depth, Slice MenuSliders with the existing double-click returns and MIDI `s:` targets) |
| `FooterBar` | shell/FooterBar | hint line (hovered component tooltip), MIDI note slider and return-on-note-off toggle, MIDI status, timing warnings |

Manual tempo, Manual beats per bar, Beat unit, Timing method and Phrase length (old main-column controls) move to a
**Timing** popover opened from a "Timing..." button on the TimingStrip. The popover content is a `TimingPanel`
(shell/TimingPanel) with the same five controls, attachments and names (the "Timing method" combo keeps its name).

`InspectorPanel` keeps its public API (constructor, `refresh()`, `showInspectorTab`) and tab classes. It is now
the workspace (no fixed 470 px column). Each tab is re-laid out for a wide area:

- Pattern: LaneCanvas fills the tab; event detail controls in a right column 260 px (hidden below 1100 px wide and
  shown as a bottom row instead); Euclid drawer 280 px right, toggled by the existing "Euclid / chance" button.
  LaneCanvas gets: taller lanes (min 40 px), lane label column 120 px with glyph, chance height plus dotted top,
  2 px white playhead, loop waveform strip (WaveformView) above the ruler when a loop is loaded, bar/beat/sub ruler
  with at least 11 px labels, effect colours from Theme.
- Generate, Scenes, Morph, Limits, Input, Perform, Print, Info: 2 or 3 column forms (Grid), groups with a 12 px
  title label and `surface` card background, controls at least 32 px high. Input hosts the WaveformView full width.
- Info tab: remove "alpha" wording; strings updated; tab count comments fixed.

Easy mode (`EasyView`, `FirstRunGuide`): restyled with Theme and `GrooveLookAndFeel` (same layout engine and tests),
scene pads and Chaos hero emphasised, text sizes from Theme, Surprise button uses `accent`.

Visual window: colours via Theme (already the same palette), fonts through Theme, its controls get the LookAndFeel
(and the right-click menu is out of scope).

PrintPanel, PresetBar, WaveformView, EuclidPanel, MenuSlider/ControlMenu: Theme colours and sizes only.

## 5. Behaviour and accessibility

- Keep ALL shortcuts: Space, 1..4, T, Esc, lane keys. Add: Ctrl/Cmd+Z undo, Ctrl/Cmd+Shift+Z or Ctrl+Y redo
  (scene edits), F1 opens the help text popover, Tab order follows visual order, every interactive child has
  `setWantsKeyboardFocus(true)`, the editor itself wants focus so shortcuts work (fix known gap).
- Every interactive component has a tooltip of at least 12 characters and an accessibility title (UI-AX-1 test walks
  all of them, including the new shell components and popover).
- Minimum 32 px targets (56 px scene pads), contrast AA, 2 px focus ring.
- Knob interaction: vertical drag 200 px full range, Shift fine, double-click default, wheel only while hovered,
  right-click menu (existing) kept, value popup while dragging.
- Stale content fixes: event limit text 256, "alpha" strings, tab count comment, `PluginProcessor.h:114` comment.

## 6. Constraints (hard)

- No processor, parameter ID, scene key or state-schema change except the single new UI key `scale`.
- Keep every control and processor call listed in the inventory; nothing is dropped, only moved (verification: see 7).
- Keep test-visible API: `GrooveMorphAudioProcessorEditor` creation and `showInspectorTab(int)`, `VisualContent` API,
  `PresetBar::createBrowserContent()`, `PrintPanel::writeWav24`, `MenuSlider/MenuComboBox/MenuToggleButton`,
  `EditorSnapshot` surface. Tests that find controls as direct children of the editor ("Timing method" combo,
  "Preview" and "Revert preview" buttons, "Gate" toggle) must be updated to search recursively.
- Everything compiles with JUCE 8.0.7 on Windows MSVC, Linux GCC, Apple clang. On Apple clang never use
  `juce::jmin/jmax/jlimit` with `std::size_t`; use `std::min/clamp`.
- No allocation or parsing in `paint()`; build Paths in `resized()`.
- Windows standalone first: the release job must go green on Windows even if macOS/Linux packaging follows later.

## 7. Acceptance

| ID | Check |
|---|---|
| UR-01 | Editor opens at 1180x760, resizes 960x640 to 2200x1400 with nothing overlapping or clipped (EditorSnapshot PNGs at 960x640, 1180x760, 1600x1000, 2200x1400 reviewed) |
| UR-02 | Every control in the old inventory exists in the new UI (completeness script lists control names before/after; none missing) |
| UR-03 | No raw `0xff` hex colours in `src/ui/**` and `src/PluginEditor.cpp` except in Theme.h and the LookAndFeel (grep check) |
| UR-04 | One palette: lane colours, rack, Euclid, Visual window use `Theme::effectColour` |
| UR-05 | UI-AX-1 test passes for all new components (tooltip >= 12 chars, title) |
| UR-06 | Existing tests pass; editor-walking tests updated to recursive search |
| UR-07 | UI scale persisted and applied; 100 percent default; legacy state loads |
| UR-08 | Ctrl/Cmd+Z and F1 work when the editor has focus |
| UR-09 | CI ui-snapshots job renders PNGs of Studio at 3 sizes, Easy, each tab; artifact uploaded |
| UR-10 | Windows standalone builds, packages and launches (smoke job) |

Not verified by this spec: real mouse/keyboard feel, DAW host scaling, screen reader pass (issue #19).
