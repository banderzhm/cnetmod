#!/bin/bash
set -euo pipefail
repo=/mnt/c/Users/25007/.codex/worktrees/3aad/cnetmod
root=/root/cnetmod-ws-20261020
{
 pacman -Q clang compiler-rt
 sha256sum /var/cache/pacman/pkg/compiler-rt-23.1.3-2-x86_64.pkg.tar.zst
 echo https://archive.archlinux.org/packages/c/compiler-rt/compiler-rt-23.1.3-2-x86_64.pkg.tar.zst
} > "$root/optimization/asan/runtime-repair.log"
bash "$repo/build-ws-evidence/asan.sh"
cd "$repo/testing/bench"
python -m unittest test_run_ws_arena test_analyze_ws_official test_compare_ws_target > "$root/optimization/python-tests.log" 2>&1
python "$repo/tools/generate_agents.py" --check > "$root/optimization/agents-check.log" 2>&1
