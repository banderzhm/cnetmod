#!/bin/bash
set -euo pipefail
scripts=/mnt/c/Users/25007/.codex/worktrees/3aad/cnetmod/build-ws-evidence
mkdir -p "/root/cnetmod-ws-20261020/results/$1"
bash "$scripts/validate_one.sh" "$1" > "/root/cnetmod-ws-20261020/results/$1/validation.log" 2>&1
for profile in echo-ws echo-ws-pipeline echo-ws-limited; do
  bash "$scripts/official_one.sh" "$1" "$profile"
done
