# Active work (updated 2026-10-20)

| Stream | Owner | Branch | State |
|---|---|---|---|
| Beta integration (W1 specs, W2 effects, W3 dsp, W4 loop, W5 presets, W6 euclid, W7 print, W8 easy, W9 release, W10 ux) | beta integration | `claude/beta-integration` | merged on the integration branch; not pushed to main by the audit |
| A1 spec/TODO completeness audit | auditor A1 | `worktree-agent-a517aedeeb3c236ce` | matrix, spec statuses, TODO and doc refresh committed locally, not pushed |
| Other beta audits (realtime/sanitizer, light QA and GUI snapshots) | auditors A3, A4 | their own worktrees | in progress at time of writing |
| Native DAW/device QA | unassigned | issue #19 | open, not done (TODO B-01) |
| First beta-release workflow run and CLAP host check | unassigned | `.github/workflows/beta-release.yml` | not run (TODO B-11) |

Rules: one owner per shared file (see MODULE-OWNERSHIP.md). Do not add a second Scheduler or processor.
`docs/TODO.md` is edited by the completeness auditor only until the audit is merged.
