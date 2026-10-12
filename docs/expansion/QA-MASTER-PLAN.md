# QA matrix and release blockers

Testing tiers: pure time/model/property tests; planner fuzz tests; deterministic offline DSP impulse/ramp/sine/drum render; sanitizer lifecycle; JUCE headless processor state; host integration scan/automation/reload; UI accessibility; platform installers.

Every feature test has stable ID Fxx-Tnn, fixture hash, input/state seed, expected measurable invariants, required platform, command and evidence SHA. Test imports with truncated/oversized/corrupt data; automation storms; sidechain absent; controller absent; missing file; multiple plugin instances; repeated instantiate/unload; sample-rate and block size switches; 4/4, 7/8 and tempo ramps; host seek/loop. Compare rendered output partitioned in different host buffers, with documented exceptions for unavoidable rounding.

Numerical performance/quality acceptance values require pilot measurement and explicit signed decision gate. Regressions that cause glitch, host crash, lost user state, invalid host automation, unbounded callback, resource leak or missing release artifact block release.