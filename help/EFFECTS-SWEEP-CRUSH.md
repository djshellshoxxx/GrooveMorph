# Filter sweep and Bit crush

Two effects that change the tone instead of the timing. They sit after Tape stop in the chain and use the same three controls as every effect: Enable, Depth and Slice.

## Filter sweep
A DJ-style low-pass filter with a little resonance. Across each slice the cutoff glides from bright (about 20 kHz) down to dark (about 200 Hz). On the next slice it glides back up, so the sweep never jumps. The Slice is the length of one glide. Depth mixes the filtered sound with the dry sound.

## Bit crush
Grit that builds up. At the start of each slice the sound is nearly clean; by the end it is heavily reduced in sample rate and bit depth. At the next slice it resets. Depth mixes the crushed sound with the dry sound.

## Good to know
- Both follow the transport. When the transport stops they fade out within a few milliseconds.
- They add no delay and use no extra memory.
- Older saved projects open with both effects switched off.
- In the Visual window they show in the stations and rings, but not in the weave or the pencil (planned) view, because they do not move audio in time.
- Not auditioned by ear yet; the numbers are checked by automated tests only.
