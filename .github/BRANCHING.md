# Branch and release policy

- `main` is the protected stable branch. It receives only reviewed changes from
  `release/*` or urgent hotfix pull requests and must stay releasable.
- `develop` is the integration branch. Its Linux/Clang workflow checks the
  protocol-free core, the common backend feature set, and the complete feature
  set on one small CI platform before changes can move to a release branch.
- `release/<major>.<minor>` stabilizes one version line, for example
  `release/2.1`. Only fixes, release documentation, and version metadata belong
  there. Full Windows, Linux, and macOS gates run on these branches.
- `v<major>.<minor>.<patch>` is an immutable release tag cut from the matching
  release branch after it has been merged to `main`.
- `hotfix/<major>.<minor>.<patch>` starts from `main` and merges back into both
  `main` and `develop`.

Direct pushes to `main` and `release/*` should be disabled with repository
rulesets. Required checks are the three platform builds and the release package
consumer gate.
