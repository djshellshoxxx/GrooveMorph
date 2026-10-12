# Visual help, numbered arrows and screenshot capture

The images included here are **annotated vector reconstructions from `src/PluginEditor.cpp`**. They are not claimed to be screenshots. All arrows target controls using the editor's default 820 × 720 bounds. This is a useful intermediate reference for readers and a reproducible checklist for real screenshot capture.

- [Full editor control map](images/editor-overview.svg)
- [Transformation row detail](images/operations-detail.svg)
- [Timing and scheduling detail](images/timing-detail.svg)
- [Control-by-control descriptions](CONTROLS.md)

## Required native screenshot set

Capture these from a verified build at the default 820 × 720 window size (plus a second resized view), with the exact tested commit and version documented:

1. **Full editor overview:** all controls visible. Annotate top controls 1–8, status and progress 9–10, variation controls 11–13, rows 14, preview/history 15–16, MIDI 17–18. Use high-contrast arrow callouts *outside* the controls wherever possible.
2. **Timing selector open:** Automatic, Host PPQ and Sample Clock labels visible. Add arrows to Timing method, Transition, manual tempo/meter, phrase bars, timing diagnostics.
3. **Operation edit detail:** checkboxes and Depth/Slice rows visible. Arrows specifically identify Repeat, Reverse, Gate, Half-time, Tape stop, Depth and Slice.
4. **Queued variation state:** one `[active]`, another `[queued]`, status countdown or waiting condition, Trigger queued and Cancel queued all visible.
5. **Preview comparison:** show Hear stored/edited, Apply preview, Revert preview and edited variation selector; capture status when preview is active.
6. **MIDI mapping:** edit selector, note-number slider, return-on-note-off checkbox, and MIDI event status.

Save lossless PNG files in `help/images/screenshots/` and annotate copies in `help/images/annotated/`. Keep originals for comparison. Add a `SCREENSHOT-MANIFEST.md` table with filename, platform, tested commit SHA, editor dimensions, host/version, image hash, and whether arrows were independently checked against the UI.

## Capture methodology

- Use a running verified Windows or Linux GrooveMorph VST3 in REAPER (and optionally Standalone for a separate timing example).
- Avoid fabricated, AI-rendered, design-mockup or source-code-only images labeled as screenshots.
- Confirm annotation arrows terminate inside the **actual clickable control bounds**, not their descriptive labels or nearby sliders.
- Capture all controls without private host/session data. For drop-down captures keep the menu open and show currently selected mode.
- Review against `src/PluginEditor.cpp`, ideally with a second reviewer, after every UI layout change.
- Update image links in help pages to reference genuine screenshot files once they are present and verified.

## Current documentation state

Guides and arrowed reference diagrams are committed on a documentation-only branch. **Native screenshots remain outstanding** because no actual window capture was available to verify in this environment. Do not mark the screenshot deliverable complete until the PNG capture set exists.
