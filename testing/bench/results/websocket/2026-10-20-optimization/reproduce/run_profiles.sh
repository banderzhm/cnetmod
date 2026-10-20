#!/bin/bash
set -euo pipefail
scripts=/mnt/c/Users/25007/.codex/worktrees/3aad/cnetmod/build-ws-evidence
entry=$1
shift
for profile in "$@"; do
  bash "$scripts/official_batch.sh" "$entry" "$profile"
done
