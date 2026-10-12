# Mandatory audio real-time contract

No allocation/deallocation, blocking mutexes, I/O, logging, UI calls, unbounded work, FFT planning, destructor of large scene snapshots, or worker waits in the audio callback. Acquire parameters through pre-cached atomic accesses; allocate scratch on prepareToPlay, account for host blocks larger than preparation size by bounded chunking. Precompute plans off-thread and cap per-sample/event work.

## Fail closed for plan input, dry-safe for audio
Malformed plan: discard and retain last valid plan. Queue full: deterministic documented source priority with overflow counter; never block. Late background plan: hold last stable scene and expose status. Unsupported sidechain: disable audio-trigger source, preserve manual/MIDI action. Invalid transport: document fallback clock epoch and prevent phantom quantized transitions. Invalid floating-point DSP sample: clamp/sanitize, count incident off callback and avoid propagating NaN/Inf. Bypass aligns delay, crossfades without allocating. Lifecycle restarts invalidate stale plan generations.

## Verification gate
Use allocation instrumentation under processBlock, TSAN-compatible stress harness for mailbox, ASAN/UBSAN on portable core, deterministic fixture of 1/16/32/64/256/1024/2048 sample blocks, 44.1/48/96/192 kHz, hot parameter automation and scene publication, empty MIDI, dense MIDI, seeks and rapid loop wraps. Define measurable CPU threshold after baseline capture, not by invented figures. Record worst-case max callback duration and audio glitches under target host.