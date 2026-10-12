# GrooveMorph expansion — specification workspace

Status: INCOMPLETE. Specification-only changes. No runtime features are approved for implementation until contract review and the M0/MVP integration baseline is reconciled.

## Verified initial repository inspection (2026-10-12)
- Repository: https://github.com/djshellshoxxx/GrooveMorph ; default branch main.
- Main README still states M0 spike. Main CMakeLists.txt declares C++20, JUCE 8.0.7, VST3 and separately named Standalone target.
- Inspected source on claude/awesome-keller-jx2ck0 implements APVTS, SceneBank state, engine processing, MIDI note triggers, variation scheduler, fallback transport and dry/wet.
- Open PR #2: MVP continuation. Open PR #4: integration of compatible Codex fixes, with unresolved PPQ timing / scheduler API conflict.
- docs/SPECIFICATION.md and docs/TRACEABILITY.md exist on the Claude branch; these are pre-existing MVP materials, not proof that expansion is implemented.
- Only selected files inspected. SHA of documentation/specification baseline has not been authenticated as final integration head. Branches, all test results and CI conclusions require further verification.

## Index
Feature specifications F01–F12 in features/; shared canonical interfaces in INTERFACE-CATALOG.md; release sequence in MASTER-BUILD-PLAN.md; safety, persistence, readiness and coordination artifacts alongside them.

## Source-of-truth and conflict precedence
Existing verified runtime behavior > verified existing public parameter/state contracts > approved MVP specification > this provisional expansion. No expansion document authorizes replacing unresolved MVP logic. Make discrepancies visible, then resolve with code and tests.

## Expansion delivery gate
Every F feature: stable requirements, exact type interfaces finalized from current source, negative tests, DSP measurements and platform acceptance. Human/agent reviewers must record evidence; speculative tests do not count.
