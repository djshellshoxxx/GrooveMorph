# GrooveMorph — Research Phase Notes

Status: research kickoff notes for the next agent
Date: 2026-10-10
Repo: djshellshoxxx/GrooveMorph

## Current state

- Repo exists and is effectively empty at the time of this note.
- The project is still in pre-implementation planning.
- The primary artifact is the specification package `GrooveMorph-Specification.md` and its research-backed recommendations.
- The next agent should treat this as a research and feasibility phase, not implementation.

## Objective of the next research pass

Validate the assumptions in the specification and reduce design risk before code or architecture commitments.

The highest-value questions are:

1. Is JUCE + CMake the best path for the intended plugin/standalone architecture?
2. Is CLAP support viable with the selected JUCE version and adapter path?
3. Which host(s) and OS targets should be treated as the minimum valid reference matrix?
4. What is the real-world latency risk for phrase-based repeat/reverse/gate operations?
5. How much of the product can be delivered in a bounded, low-latency MVP without full phrase capture?
6. What competitor workflows are genuinely adjacent, and how do we avoid false differentiation?

## Research tasks to complete

### 1) Framework and SDK feasibility

Research questions:
- JUCE licensing and commercial fit for the owner
- Whether JUCE is the right tool for VST3 + standalone first
- Whether CLAP is worth the added complexity in the initial build/test cycle
- Whether a non-JUCE alternative should be compared before lock-in

Sources to check:
- JUCE feature documentation
- JUCE licensing terms
- CLAP repository and validator docs
- Optional comparison with iPlug2 / DPF if needed

Key outputs:
- A recommended build stack and framework decision
- Explicit CLAP decision: include now or defer to a later milestone
- A concise licensing note for the owner

### 2) Host matrix and plugin validation

Research questions:
- Which DAW/host should be the primary validation target?
- Which host should be used for Linux and Windows development?
- How much validation is actually needed before beta?

Recommended starting point:
- REAPER on Windows x64 and Linux x86_64 as the default validation hosts
- VST3 first, CLAP second only if version/adapter support is proven
- Standalone as a development and debugging target

Tasks:
- Confirm REAPER documentation and current support for VST3/CLAP on both OSes
- Check whether the project should support other hosts immediately or wait for owner input
- Record the host test matrix to be used in later milestones

### 3) Latency, capture, and real-time safety

This is the most important engineering risk in the spec.

Research questions:
- Can bounded short-window repeat/reverse/gate operations be low latency without full phrase capture?
- What is the practical latency if we add longer phrase capture or reverse playback?
- What is the safe design boundary for a first release?

Tasks:
- Review the real-time constraints for short history buffers and bounded look-behind
- Validate whether phrase capture is a future mode rather than an MVP requirement
- Capture a written recommendation for the “no full capture in MVP” decision
- Write down the measurable latency policy and how it should be reported to hosts

### 4) DSP and algorithm feasibility

The specification proposes a bounded set of operations:
- repeat/stutter
- reverse slice
- gate
- half-time feel
- tape-stop-like rate ramp

Research questions:
- Are these operations feasible in a bounded real-time implementation?
- Which algorithms are simplest and safest for the first release?
- What should be deferred because of pitch/latency complexity?

Tasks:
- Review common time-scaling and slicing approaches
- Evaluate whether pitch-preserving stretch is required or should be deferred
- Identify any operation that should be excluded from the MVP because of unacceptable latency or implementation risk

### 5) Competitive product review and positioning

The spec already includes a market comparison, but it needs a sharper product-positioning assessment.

Tasks:
- Confirm whether the phrase-variation + transition workflow is meaningfully different from current products
- Check if there are already products with strong similar workflow semantics
- Establish a rule: do not market novelty without user testing

Goal:
- Make sure the product differentiator is real enough to justify build effort before coding begins

### 6) Product/owner decision list

The next agent should explicitly confirm or record these before any implementation milestone:

- Owner target OSes and architectures
- Plugin formats to support in the first release
- Whether CLAP is required immediately or optional
- Whether ripping full phrase capture into the MVP is allowed
- Whether pitch-preserving stretch is required
- Whether a standalone application is a validation tool only or a real product surface
- Whether the project name, pricing, and distribution model are still open

## Recommended next-agent checklist

The next agent should do the following in order:

1. Confirm repo state and any existing files beyond the specification
2. Validate JUCE/CLAP/framework direction against current docs and license constraints
3. Define the minimum viable host/platform matrix
4. Perform a latency and real-time safety review on the proposed operations
5. Narrow the feature set to the lowest-risk MVP
6. Write a short recommendation memo with a final approved stack and target scope
7. Stop before coding unless all P0 risks are resolved or clearly accepted by the owner

## Notes for implementation planning

The spec intentionally does not lock in code, operating system assumptions, DAW support, or format support too early.

The most important “no-go” rule:
- Do not implement full phrase capture or long-form reverse/re-render as part of the MVP without a documented latency decision and owner approval.

The most important “yes-go” rule:
- Keep the first release centered on bounded short-slice operations, phrase variation, and explicit transition policy.

## Decision gate before coding

Before any implementation milestone, the next agent should answer these five questions clearly:

1. What is the actual build target stack and framework license decision?
2. Which host(s) and OSes are the release validation targets?
3. Are we committing to VST3 only in the first build, or VST3 + CLAP?
4. Are we deliberately excluding full phrase capture from MVP?
5. Has the owner accepted the proposed performance/latency quality thresholds, or do we need a fresh benchmark plan?

If any of these remain open, the next agent should continue researching instead of beginning implementation.

## Useful references already identified

- Steinberg VST3 API docs and FAQ
- JUCE feature and licensing docs
- CLAP repo and validator docs
- REAPER support docs
- Competitor product docs: Gross Beat, ShaperBox 3, Looperator, Effectrix 2, Stutter Edit, Infiltrator 2
- Research references on onset detection and time-scale modification

## Suggested output for the next agent

The next agent should leave a concise summary in the repository after finishing research, ideally in the form of:

- a short decision memo,
- a final recommended stack,
- a reduced MVP scope, and
- any open items still waiting on owner confirmation.

This repo is not yet code-heavy, so documentation and clear decision records are the product of the current phase.

---

This note is intentionally written to support a handoff to the next research agent without requiring an implementation decision yet.
