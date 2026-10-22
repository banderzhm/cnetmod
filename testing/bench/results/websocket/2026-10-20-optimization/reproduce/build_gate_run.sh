#!/bin/bash
set -euo pipefail
repo=/mnt/c/Users/25007/.codex/worktrees/3aad/cnetmod
name=$1
bash "$repo/build-ws-evidence/build_batch.sh"
python "$repo/build-ws-evidence/stage_batch.py" "$name"
bash "$repo/build-ws-evidence/gate_batch.sh" "$name"
bash "$repo/build-ws-evidence/run_profiles.sh" "$name" echo-ws-pipeline echo-ws-limited echo-ws
