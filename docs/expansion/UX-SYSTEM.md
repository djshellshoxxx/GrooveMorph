# Shared UX system

All three views project the same canonical committed scene and ephemeral audition state. No duplicated model. The UI controller listens to atomic/queued status updates and submits versioned edit commands to the message thread. The audio callback neither paints UI nor modifies persistent editor state.

Quick Mode: top bar BPM source/phrase grid/active scene; scene chooser; Generate button with style and intensity; Preview and Commit. Editor Mode: expandable scene strip, lane/event canvas, operation inspector, manually protected ranges, history A/B branch graph, morph pairs, evolution stages and gesture timeline. Performance Mode: large scene pads, quantization, Hold Fill, Return Base, panic dry, active/queued status, controller learn.

Navigation: Tab cycles logical focus; Space triggers focused control only when host keyboard policy permits; Escape cancels modal preview. High DPI and user font scaling keep hit targets usable. Every indicator provides text + color; no color-only state. Disabled feature shows reason (no sidechain, unavailable adapter, missing file). Do not advertise an unavailable feature as a working control. Every workflow must be tested with mouse and keyboard and host automation updates.
