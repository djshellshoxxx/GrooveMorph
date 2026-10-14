# Six musical controls: developer specification package

Authored 2026-10-13. Specification package. C01-C06 are now partly implemented; see docs/alpha/ALPHA-FEATURE-STATUS.md for what exists and where it deviates.
Baseline: main 015658dc4471be9816d0e2cbcccd8f5d72ed9223; canonical runtime 7f2784a.
The user's requested five controls are phrase placement, variation distance,
change permissions, transition phase and automatic return. The sixth is effect
activity limits. Activity limits make generated scenes controllable without reducing
all effect depths. These are product design decisions, not claims of market novelty.

| ID / stable module id | Specification | Responsibility | Depends on |
|---|---|---|---|
| C01 / phrase-placement | [C01](features/C01-PHRASE-PLACEMENT.md) | Musical grid, bounded events, placement windows | Shared contracts |
| C03 / change-permissions | [C03](features/C03-CHANGE-PERMISSIONS.md) | Field/event/time-region mutation permissions | C01 |
| C06 / effect-activity-limits | [C06](features/C06-EFFECT-ACTIVITY-LIMITS.md) | Deterministic activity validation and pruning | C01, C03 |
| C02 / variation-distance | [C02](features/C02-VARIATION-DISTANCE.md) | Seeded candidate planning and explicit preview/apply | C01, C03, C06 |
| C04 / transition-phase | [C04](features/C04-TRANSITION-PHASE.md) | Destination phase and boundary ramps | C01, existing Scheduler |
| C05 / automatic-return | [C05](features/C05-AUTOMATIC-RETURN.md) | One temporary fill and deterministic return | C04, existing Scheduler |

Read [shared contracts](SHARED-CONTRACTS.md), [parameter manifest](PARAMETERS.json),
[integration/tasks/QA](INTEGRATION-PLAN.md) and [research](RESEARCH.md) before any
feature file. Those documents are normative dependencies of every feature spec.
Each feature defines control ranges, defaults, GUI, chain position, lifecycle,
state, negative cases, tests and integration ownership. No spec authorizes a
second competing Scheduler, DSP chain or incompatible public parameter rewrite.

This package provides an actionable design for these six controls. It does not
mark the separate twelve-feature PR #6 package READY or implement the features.
Implementation must resolve existing Variation-host feedback and MIDI mapping
bugs and verify the API/state/GUI plan in source and tests as it is integrated.
