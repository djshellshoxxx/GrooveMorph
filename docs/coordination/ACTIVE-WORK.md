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

## Continuation 2026-10-22

Owner: Codex, branch `codex/midi-policy-completion-20261022`, base `bdb9cb5`.
Owns MIDI link core, processor adapters and menu/notice wiring for A04-T07..T09,
plus full-table deserialization regression. No other active implementation PR
was found (open PRs #6, #37 and #40 are documentation/specification work).
Portable Release verification is recorded in `docs/qa/MIDI_POLICY_2026-10-22.md`.
Soft pickup, native host checks, licence decision and other B items remain open.
