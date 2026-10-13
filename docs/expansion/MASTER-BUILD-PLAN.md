# Expansion phases and verification gates

The following is the requested sequence; execution must respect task dependencies and not count compilation as functional verification.

| Phase | Entry | Exit evidence |
|---|---|---|
| M0 | Baseline discovery/integration completed at 7f2784a via PR #7; recheck current ownership | Passing Windows/Linux/sanitizer/startup evidence recorded; native host/device acceptance still outstanding |
| M1 | M0 | Scene/time/parameter/state API contract tests and migration fixtures passing |
| M2 | M1 | Quick/Editor UX baseline + F08 history unit tests |
| M3 | M2 | F01 deterministic planner, constraints and preview tests |
| M4 | M3 | F02 semantic morphing and F04 performance triggers integration tests |
| M5 | M4 | F05 bounded evolution and F11 gesture replay fixtures |
| M6 | M4 | F03 protection, F06 lanes, F07 triggers and timing tests |
| M7 | M6 | F09 import/capture and F10 multiband reconstruction measurements |
| M8 | M4 | F12 host mappings, Windows/Linux adapters and CLAP compatibility decision |
| M9 | M7/M8 | Whole-app CMake, schema, UI, DSP and end-to-end integration passes |
| M10 | M9 | Independent serious defect audit, regression tests |
| M11 | M10 | Baseline/optimized CPU, latency and memory benchmarks |
| M12 | M11 | Requirement-to-code-and-test traceability audit |
| M13 | M12 | Target-host DAW, high-load, sustained and malformed-input QA |
| M14 | M13 | Windows x64 VST3/Standalone artifacts and host scan evidence |
| M15 | M13 | Linux x86_64 VST3/Standalone artifacts and host scan evidence |
| M16 | M14/M15 | CLAP Windows/Linux adapter validator and DAW evidence |
| M17 | M16 | Independent final bug/optimization/completeness audits |
| M18 | M17 | Checksums, dependency licenses, provenance, installers and release handoff |

Block downstream acceptance if preceding phase not satisfied. Parallelize only disjoint files/contracts; shared Processor/CMake/StateCodec should have a single current owner. Do not assume multiple coding agents can be provisioned; coordinator records actual active sessions and GitHub PR links.

At each gate record commands, environment, SHA, observed output, defects, remediation and rollback target. Failed integration reverts only its merge without discarding other workers' branches.