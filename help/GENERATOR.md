# Seeded generator and candidate gallery

The **Gen** tab creates repeatable variations of the stored scene. Set a seed,
style and distance, then choose **Generate preview** for one candidate or
**Generate 3 options** to fill the gallery. Each option is generated from the
same stored scene and the next candidate number. Select an option to audition
it through the current input; only one option is heard at a time.

Generated candidates remain previews. Use **Apply preview** to keep the selected
candidate as one undoable scene edit, or **Revert preview** to return to the
stored scene. Generate another candidate or gallery to replace the current
unapplied preview.

Turn on **Generate event patterns** to include seeded phrase events in the
candidate. It replaces unlocked events in that candidate, so existing lanes
remain unchanged when this option is off. Events snap to the scene's subdivision. Generated events avoid
protected regions and respect operation event locks, per-event locks, placement
locks and **Never touch** operations. Use the **Place** tab to refine an event
pattern after applying it.

The 24 most recent candidate records are saved in the user's application-data
directory on this computer. Each record includes its source scene snapshot and
generator settings so it can be recalled after the editor closes. This history
is not part of the DAW project and is not transferred to another computer.
