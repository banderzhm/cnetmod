#!/bin/bash
set -euo pipefail
repo=/mnt/c/Users/25007/.codex/worktrees/3aad/cnetmod
bash "$repo/build-ws-evidence/build_batch.sh"
python "$repo/build-ws-evidence/stage_batch.py" cnetmod-ws-reviewed
python "$repo/build-ws-evidence/stage_reviewed16.py"
bash "$repo/build-ws-evidence/gate_batch.sh" cnetmod-ws-reviewed16-b64
bash "$repo/build-ws-evidence/run_profiles.sh" cnetmod-ws-reviewed16-b64 echo-ws-pipeline echo-ws-limited echo-ws
