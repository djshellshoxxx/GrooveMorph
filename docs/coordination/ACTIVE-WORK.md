# Expansion specification ownership

Branch: spec/groovemorph-expansion-v1. Scope: docs/expansion/** only. No runtime code modified.

Existing PR #2 and #4 involve implementation and integration and are preserved. Agents on those PRs are not presumed aware of this branch; coordinate using GitHub discussion before editing shared docs or rebasing. Shared time/state/interface contract requires one architect owner. Current spec pass is provisional and has not locked file ownership for unseen active workers.

Integration order: resolve MVP clock compatibility -> freeze canonical model/time/state -> feature spec review -> task decomposition -> implementation worktrees -> independent audit -> host release gates.