# Presets

A preset is a saved set of all four scenes (A-D): the effects, depths, slice lengths, names, placement, event lanes, locks and limits of every scene. GrooveMorph ships with factory presets and lets you keep your own.

The preset bar is the strip along the bottom of the left panel.

## Loading a preset

- Press `<` or `>` to step through the list, or click the name to open the browser.
- In the browser, type in the search box to match names, categories, tags and authors, or pick a tag in the tag menu. Presets are grouped by category. Click one to load it.
- Your DAW's own preset menu shows the same list (factory first, then your presets), so you can step through them from the plugin header in the host.

Loading replaces the four scenes only. Your MIDI note map, the scene that is sounding, the random seed, timing and transport are left alone. A few presets also set Dry/Wet, band split or morph values; those follow the preset when it loads and are ordinary controls afterwards.

Loading is one Undo step. Press Undo to get your previous scenes back. The change reaches the audio the same way every other scene edit does, so it does not click. You cannot load a preset while a preview is active; apply or revert the preview first.

A `*` after the name means you have changed the scenes since loading the preset.

## Saving your own

- **Save As** asks for a name, a category and tags (comma separated) and saves the four scenes, together with the current Intensity, Dry/Wet, morph and band settings, as a user preset.
- **Save** writes over the loaded user preset. With a factory preset loaded (or none) it behaves like Save As.
- **Rename** and **Delete** work on the loaded user preset.
- Factory presets can not be overwritten, renamed or deleted, and a user preset can not take a factory name.

## Sharing

**Export** writes the loaded preset to a `.gmpreset` file. **Import** reads one into your user presets (if the name is taken, a number is added). Imported files are checked and rewritten, so damaged or oversized files are refused with a message instead of loading.

User presets live in:

- Windows: `%APPDATA%\djshellshoxxx\GrooveMorph\Presets`
- macOS: `~/Library/djshellshoxxx/GrooveMorph/Presets`
- Linux: `~/.config/djshellshoxxx/GrooveMorph/Presets`

You can also drop `.gmpreset` files in that folder; they show up the next time the plugin opens.

## Factory categories

Init (the default four scenes), Stutter, Halftime, Gate Chops, Tape Stops, Builds & Fills, Subtle Groove and Experimental. Fills use the last bar of the phrase; the event presets sit inside the first three beats, so they work at any meter and phrase length.

Which preset is loaded is saved with the project, but the scenes themselves are saved as they are, so a preset you later change or delete will not change an old project.
