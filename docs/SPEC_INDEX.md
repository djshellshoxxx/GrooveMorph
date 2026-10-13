# Specification and work index

Updated 2026-10-13. Main contains the integrated runtime, not the former M0 shell.

| Document | Purpose | Current status |
|---|---|---|
| [Developer specification](SPECIFICATION.md) | Original requirements, behavior and release contract plus implementation overlay | Authoring gaps remain; numeric acceptance targets unverified |
| [Feature audit](feature-audit/FEATURE_AUDIT_2026-10-13.md) | Delivered/partial/absent features, reproduced workflow gaps, branch blockers | Audited main 61d5156; runtime 7f2784a |
| [TODO](TODO.md) | Priorities, dependencies and acceptance checks | Active work ledger |
| [Traceability](TRACEABILITY.md) | Requirements to code/tests | Integrated MVP; DAW/device acceptance outstanding |
| [Timing handoff](TIMING_INTEGRATION_HANDOFF.md) | Canonical clock semantics and exact CI/build/merge evidence | Integration complete |
| [Integration audit](INTEGRATION_AUDIT.md) | Integration defects, discrepancies and verification limits | Historical integration review; feature audit is current inventory |
| [Research/progress](../RESEARCH_NOTES.md) | Approved direction and implementation progress | Updated; original research date retained |
| [Completion prompt](COMPLETION_PROMPT.md) | Autonomous engineering continuation instructions | Use current TODO and ownership, preserve canonical runtime |
| [User help](../help/README.md) | Actual controls and workflows | Guides/diagrams merged; native screenshots missing |

## Six requested musical controls

[Six-control specification package](control-specs/README.md) defines phrase placement,
variation distance, change permissions, transition phase, automatic return and
effect activity limits, with shared state/automation/GUI contracts and integration QA.
These are specifications; no runtime implementation is claimed.

## Expansion specifications

The separate [PR #6](https://github.com/djshellshoxxx/GrooveMorph/pull/6) workspace
contains F01–F12 and shared contracts. Its marker is INCOMPLETE and specifications
are drafts. It is not merged into main. [PR #9](https://github.com/djshellshoxxx/GrooveMorph/pull/9)
contains an unmerged planner core, not full F01. Freeze current interface/state/task
contracts and test fixtures before treating the expansion package as implementation-ready.
Do not classify a stored seed, undo stack or short history ring as a completed
candidate generator, variation-history browser or full capture workflow.
