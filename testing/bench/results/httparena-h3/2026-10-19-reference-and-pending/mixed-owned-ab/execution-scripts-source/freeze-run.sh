#!/bin/bash
set -euo pipefail
[[ "$(readlink /proc/self/ns/mnt)" != "$(readlink /proc/1/ns/mnt)" ]]
if [[ ! /tmp/cnetmod-h3-arena-build -ef /var/tmp/cnetmod-h3-arena-build ]]; then
 mkdir -p /tmp/cnetmod-h3-arena-build
 mount --bind /var/tmp/cnetmod-h3-arena-build /tmp/cnetmod-h3-arena-build
fi
python /mnt/c/Users/25007/.codex/tmp/cnetmod-h3-owned-fast-execution-fbaeb04a21614732b24a7fdae850fcd0/owned-fast-freeze.py --execute --baseline-variant /var/tmp/cnetmod-h3-mixed-prefix-candidate-4a739b77e7584ead85cbf02304fb77ff/candidate-variant.json --output /var/tmp/cnetmod-h3-owned-fast-candidate-fbaeb04a21614732b24a7fdae850fcd0 --prepared-source-snapshot /mnt/c/Users/25007/.codex/tmp/cnetmod-h3-owned-fast-execution-fbaeb04a21614732b24a7fdae850fcd0/source-snapshot > /var/tmp/cnetmod-h3-owned-fast-fbaeb04a21614732b24a7fdae850fcd0/freeze.log 2>&1
cat /var/tmp/cnetmod-h3-owned-fast-fbaeb04a21614732b24a7fdae850fcd0/freeze.log
