# GrooveMorph agent skills

This catalog contains **27 skills**: the complete upstream Superpowers skill
directory (15), ten DSP/JUCE-focused Sonic Skills, Caveman, and an opt-in Ultimate
Protocol Simulator. No Antigravity protocol skill is installed.

## Start here

Read root [AGENTS.md](../AGENTS.md), this catalog, and
[using-superpowers](superpowers/using-superpowers/SKILL.md). Load the relevant
skills and their references only when needed. Resolve `superpowers:<name>` to
`skills/superpowers/<name>/SKILL.md`; other names resolve to
`skills/<name>/SKILL.md`. Preserve the Superpowers sibling layout.

Read the applicable project specifications and task ownership before editing.
At this catalog's creation, the developer-ready specification and newer M0 code
were on `claude/awesome-keller-jx2ck0`, which contains the ancestor
`codex/m0-validation` work; `main` still held the earlier scaffold. Verify current
refs and integrate reviewed, passing work instead of regenerating those specs
or reimplementing those modules.

The root `skills/` folder is a file-readable catalog. It does not itself enable
host-native discovery, slash commands, extra sessions, tool access, or model
selection. Use actual host capabilities or read the skill files directly.
Do not execute bundled scripts just to load a skill. Generic examples and skill
instructions remain subordinate to current user authorization, host rules,
repository contracts, actual capabilities, and required verification.

## Ten DSP/JUCE additions

All ten come from [kunitoki/sonic-skills](https://github.com/kunitoki/sonic-skills).
Their complete supporting reference files are preserved.

| Skill | GrooveMorph use |
|---|---|
| [audio-dsp-review](audio-dsp-review/SKILL.md) | Audit callbacks, bounded history/scratch buffers, command queues, and real-time safety. |
| [audio-numerics-review](audio-numerics-review/SKILL.md) | Review finite ranges, NaN/Inf handling, denormals, interpolation, counters, and precision. |
| [audio-performance-debug](audio-performance-debug/SKILL.md) | Measure callback load, worst-case scenes, CPU spikes, memory growth, and optimization results. |
| [audio-artifacts-debug](audio-artifacts-debug/SKILL.md) | Diagnose clicks, discontinuities, aliasing, DC, phase errors, and latency mismatches. |
| [audio-signal-flow-explainer](audio-signal-flow-explainer/SKILL.md) | Trace input, history, transforms, transition mixer, aligned dry path, and output. |
| [audio-math-explainer](audio-math-explainer/SKILL.md) | Explain timing, windowing, rate curves, crossfades, and implementation equations. |
| [dsp-algorithm-guide](dsp-algorithm-guide/SKILL.md) | Implement and verify processing blocks; apply only algorithms within GrooveMorph's approved scope. |
| [juce-guide](juce-guide/SKILL.md) | CMake/JUCE integration, APVTS, editor attachments, state, and packaging. |
| [juce-review](juce-review/SKILL.md) | Review lifecycle, thread boundaries, state, MIDI, parameters, and buffer handling. |
| [vst3-review](vst3-review/SKILL.md) | Review buses, host timing, automation, state, latency, tails, and validator readiness. |

These skills provide review checklists, not proof of API behavior. Check the
repository's pinned JUCE source and official API/format documentation before
relying on a version-sensitive claim. Trace actual callback thread ownership;
do not assume every processor or state callback runs on the message thread.
Use cached atomic parameter pointers in processing, validate atomic/queue
properties and object lifetimes, and keep model destruction outside the callback.
Do not copy generic numerical fixes that change the intended audio contract.

Start DSP/JUCE reviews with `audio-dsp-review` and `audio-numerics-review`, then
add `juce-review` and `vst3-review`. Core/model/DSP modules remain reusable without
JUCE GUI or operating-system dependencies. CLAP is deferred in the approved
repository decisions; availability of generic format advice does not expand scope.

## Token-efficient communication

- [Caveman](caveman/SKILL.md): concise human-readable status and agent handoffs.
  Preserve complete source, comments, documentation, technical facts, and test evidence.
- [Ultimate Protocol Simulator](ultimate-protocol-simulator/SKILL.md): JSON-only
  status only when explicitly requested and permitted by the host. Its active
  file adds a clearly marked GrooveMorph preface, uses CMake/CTest instead of
  Flutter, honors existing authorization, and requires truthful verification.
  [SKILL.upstream.md](ultimate-protocol-simulator/SKILL.upstream.md) preserves
  the exact upstream original. Do not combine JSON-only output with Caveman prose.

Load only the skill needed for a task. Search focused paths, bound logs, reuse
research/test fixtures, and record durable handoffs. Choose the cheapest available
model capable of the required quality; escalation needs evidence. Extra workers
need independent tasks, explicit file ownership, tests, and an integration target.
Never reduce verification or lose technical constraints to save tokens.

## Provenance and licenses

[manifest.json](manifest.json) records pinned source revisions, original paths,
destinations, modifications, SHA-256 hashes, and executable flags for every
vendored file. The ten Sonic Skills, Superpowers, and Caveman are unchanged
upstream copies. Only Ultimate's active metadata and integration preface are adapted.

| Source | Attribution | License |
|---|---|---|
| [obra/superpowers](https://github.com/obra/superpowers) | Jesse Vincent | [MIT](superpowers/LICENSE) |
| [kunitoki/sonic-skills](https://github.com/kunitoki/sonic-skills) | kunitoki / upstream contributors | Unlicense, retained in each copied skill. |
| [JuliusBrussee/caveman](https://github.com/JuliusBrussee/caveman) | Julius Brussee | [Apache 2.0](caveman/LICENSE), [NOTICE](caveman/NOTICE), and retained [historical MIT notice](caveman/LICENSE-MIT). |
| [KINGSTAR-OMEGA/claude-token-optimizer](https://github.com/KINGSTAR-OMEGA/claude-token-optimizer) | KINGSTAR-OMEGA | [MIT](ultimate-protocol-simulator/LICENSE) |

These materials retain their own licenses and do not establish the application's
product license or JUCE distribution rights. Token-saving percentages from
upstream marketing are not independently validated here.

Updates must pin an explicit source commit, preserve resources/notices, document
any adaptations, and refresh manifest hashes. Validate frontmatter, unique names,
supporting paths, copied-file integrity, executable modes, and relevant project
checks before integration. Keep Antigravity protocol skills excluded.
