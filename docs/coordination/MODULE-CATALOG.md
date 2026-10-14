# Module catalog (alpha additions)

| Module | Interface | Depends on |
|---|---|---|
| `core/control/SceneControl.h` | `SceneControl{placement,lane,permissions,activity}`, `sanitize`, `addEvent/replaceEvent/removeEvent`, `convertLegacyToEvents` | std only |
| `core/control/PlacementEvaluator.h` | `evaluate(control, GridContext, positionBeats, enabledMask) -> Targets{active,depthScale,ticksUntilChange}`; `isActive` | SceneControl |
| `core/control/VariationBlend.h` | `blendDistance` (C02), `morph` (F02), `enforcePermissions` (C03) | OperationParams |
| `core/control/AutoReturn.h` | `arm`, `poll`, `disarm` (C05) | std only |
| `core/variation/CandidateBuilder.h` | `buildCandidate(source, GeneratorSettings) -> Candidate` (planner -> distance -> locks -> sanitize) | VariationPlanner, VariationBlend |
| `core/dsp/DemoSource.h` | `render(...)` stateless drum loop | std only |
| `ui/InspectorPanel.h` | tabbed inspector, driven through `SceneEditAccess` callbacks | processor, core |
| `tools/EditorSnapshot.cpp` | dev tool: renders the real editor to PNG | plugin target |

All `core/` modules are portable and covered by `tests/core/*Tests.cpp`; processor behaviour by `tests/PluginProcessorTests.cpp`.
