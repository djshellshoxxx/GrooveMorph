# Easy mode

Easy mode is one screen with the things you need most. A new GrooveMorph opens in it. Projects saved before Easy mode existed keep opening on the full editor.

## What is on the screen

- **Timing line.** Says in plain words what GrooveMorph is following, for example "Following the host transport", "Waiting for host transport" or "Using sample clock". If it says waiting, press play in your host.
- **Scenes A to D.** Big buttons. Green is the scene you hear now, amber is queued. A click queues the scene using the Transition setting (the next bar by default).
- **Intensity and Dry / wet.** The same controls as in the full view. Intensity 0 gives you the original sound.
- **Morph.** Blends the sounding scene toward the next scene. If no morph target is set, Easy mode points it at the next scene for you and the label shows which one.
- **Chaos.** One knob that makes the sounding scene busier: more effects switch on, they get deeper and the slices get shorter. At 0 you hear your scene exactly as stored. Your saved scenes are never changed by Chaos, and locks, protected regions and Never touch settings are respected. It is a normal host parameter, so you can automate it.
- **Surprise me.** Tries a new variation of the sounding scene and plays it right away. It is kept at the next phrase. Press it again for a different one. It never offers a variation with no audible effect. If the scene is fully locked it tells you there is nothing to change.
- **Undo last change.** Cancels a pending Surprise. Otherwise it undoes your last scene edit.
- **Reset / Panic.** Sets Chaos and Morph to 0, cancels pending changes and switches off phrase evolution and gesture playback. It does not touch your scenes, Intensity or the transport. The full factory reset is still in the full view and asks first.
- **Open full view.** Switches to the full editor. The **Easy mode** button next to the phrase length there brings you back.

Easy mode fills the whole editor and scales with it, down to the minimum size.

## First-run guide

The first time Easy mode opens, a short four step card explains the transport, the scenes, Surprise me and the full view. Skip it, or press Done on the last step, and it will not open by itself again (this is remembered per user, not per project). **Show guide** reopens it any time.

## Notes

- Surprise me uses the same generator as the Generator tab with its default settings (Conservative style, full distance), so the same scene seed and press count always give the same result.
- While you have your own preview open in the full view, Surprise me asks you to apply or revert it first.
- Chaos is applied only to what you hear. Saving the project saves the Chaos knob position, not a changed scene.
