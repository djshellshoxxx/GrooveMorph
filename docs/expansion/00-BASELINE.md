# Expansion specification baseline (updated 2026-10-13)

Package status: INCOMPLETE. Expansion direction is approved; developer contracts,
fixtures and independent review are not complete. This workspace is specification
work, not evidence that F01–F12 are delivered.

## Verified canonical baseline

Runtime commit: `7f2784a0571f2a6ffb93ea261c386cd89cdde6af`.
Main integration merge: `aaa8dae587d6439061dfeef3794cccd14ddefcfe` (PR #7).
Audited main: `61d5156e2fef01fa83193581c93cb8e2357049a4` (2026-10-13).
PRs #2/#4/#7 are merged/reconciled. PPQ versus sample Scheduler integration is
resolved; do not transplant the old M0 processor or alternate SceneScheduler.

Actual runtime: Claude SceneBank/RhythmEngine/state/undo/MIDI implementation,
one Scheduler and Codex PPQ boundary calculations. Stable timingMode values are
0 Automatic (PPQ preferred, sample fallback), 1 Host PPQ (strict musical timing,
beat/bar requires credible anchor), 2 Sample Clock (ignore PPQ phase; explicitly
allow standalone free-run without play state; stopped host remains stopped).
Host seeks invalidate history in O(1); pending boundaries refresh each segment.

The processor exposes ten global APVTS parameters. Operation settings are scene
state; SceneSnapshot is a fixed settings array without event lanes. Phrase origin
and subdivision editing are absent. Existing seed storage is not a generator,
undo is not candidate history, and four-second history is not full phrase capture.
Use the current main docs/TODO.md, feature audit and timing handoff as evidence.

Windows/Linux builds, portable sanitizers and Windows startup passed at 7f2784a.
All eight matching local Linux executables passed on 2026-10-13. REAPER/device,
performance, callback instrumentation and native screenshot acceptance remain
unverified. Original feature contracts must preserve these limits.

## Branch work and remaining gate

PR #9 has an isolated deterministic planner with no processor/editor workflow.
Linux PRNG-state reference binding fails; assertion checks disappear in Release.
Do not mark F01 complete from that core. PR #6 remains draft; baseline pinning
resolves its integration discovery blocker, not schema/API/task/research readiness.
All twelve feature specs remain DRAFT and the specification commit pin remains
null until the package is reviewed/frozen.

Source precedence: actual verified runtime/state IDs, approved requirements, then
proposed expansion contracts. Preserve original unmet phrase-authoring requirements.
Complete exact types, state migration, parameter manifests, negative/numeric tests,
RT budgets and reviewer evidence before READY_FOR_IMPLEMENTATION.
