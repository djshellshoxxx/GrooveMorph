# C01 to C06: deviations of the implementation from the control specs

Written 2026-10-20 against `c83e8b2`. The specs in [docs/control-specs](../control-specs/README.md) remain the normative design; this file lists where the shipped code differs
and who closes each gap. Sources: `docs/coordination/DECISIONS.md`, `docs/alpha/ALPHA-FEATURE-STATUS.md`, code.

| Spec | Deviation | Reason / record | Owner of closing it |
|---|---|---|---|
| All | Ticks are 1/960 of a METER beat, not of a quarter note | DECISIONS (exact for all supported subdivisions) | none, accepted |
| All | Scene control state is optional keys in `groovemorph-scenes v1`, not a v2 header | DECISIONS | none, accepted; W3/W6 append keys only |
| C01 | 32 events per scene (spec 256); protected regions 4 (spec 8) | `maxLaneEvents`, `maxProtectedRegions` in `SceneControl.h` | W3 (capacity 256, 8 regions) |
| C01 | Events saved for a longer phrase wrap into a shorter one without a conflict badge; no split-at-boundary command | ALPHA doc | W10 (conflict badge); split command unowned |
| C01 | No keyboard event creation | A07 | W10 |
| C02 | Distance is a per-field blend (depth linear, slice log, enable flip at 0.5); no richer metric | `blendDistance` | unowned, see F01 |
| C03 | Locks enforced after generation and on every edit path; no lock groups or region names | DECISIONS | unowned, deferred |
| C03 | Per-event locks are a `LaneEvent::locked` flag, not a separate table | DECISIONS | none, accepted |
| C04 | Phase is scene content (`phase.policy`, `phase.offset`), not APVTS parameters | DECISIONS | none, accepted |
| C04 | No per-scene smoothing time; pattern jumps use the engine's fixed 3 ms taps | DECISIONS | W3 |
| C04 | No phase preview diagram | ALPHA doc | unowned |
| C05 | No "Dry" return target; return targets are Previous or A-D | ALPHA doc | W3 ("Dry return") |
| C05 | Return is requested one bar early with NextBar quantization so the Scheduler lands it on the bar line | DECISIONS | none, accepted |
| C06 | Spacing and coverage act only through the explicit Analyze/Reduce action; "effects at once" is live | ALPHA doc | none, accepted |
| C06 | Reduce works on the phrase ring, not each lane's loop ring | DECISIONS | W3 (loop-aware Reduce) |
| Acceptance IDs | Most C01-C06 acceptance IDs have no test by that ID; equivalent tests exist under other names | ALPHA doc | W1 provides the mapping below; renaming tests is not required |

## Acceptance ID mapping (where an equivalent test exists)

| Spec test theme | Existing test |
|---|---|
| C01 window, wrap, cadence, boundary distance | `tests/core/SceneControlTests.cpp` "before last bar inactive" ... "cadence"; PluginProcessorTests "effect engages inside the placement window" |
| C01 events add/overlap/capacity/wrapped | SceneControlTests "add event", "overlap rejected", "wrapped overlap rejected", "capacity bound" |
| C01 legacy recall | SceneControlTests "legacy loads default control", "default has no control keys" |
| C03 permissions | SceneControlTests "permissions restore locked", "manual edit restores the locked field only", "locked event survives a full candidate lane" |
| C04 | `tests/core/PhaseResolverTests.cpp` (all), PluginProcessorTests "preserve scene keeps the transport phase", "restart scene starts its pattern at phrase beat 0" |
| C05 | SceneControlTests "armed", "no request exactly on the bar line", "request during bar 6", "seek back disarms"; PluginProcessorTests "return is sample-accurate at the bar line", "phrase-unit return lands on the phrase boundary" |
| C06 | SceneControlTests "limit keeps 1 and 3", "spacing drops the closer low-priority events", "coverage bound respected", "locked conflict reported, lane untouched" |

Any new test for these specs should be named with its spec ID (for example `C05-T04 ...`) so the next audit does not need this table.
