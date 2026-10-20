set -euo pipefail
ip link set lo up
for variant in expired-held expired-released; do
 expected=false
 if [[ $variant == expired-released ]]; then expected=true; fi
 /var/tmp/cnetmod-h3-peer/bin/python /mnt/c/Users/25007/.codex/tmp/cnetmod-h3-expired-weak-probe-run.py /var/tmp/cnetmod-h3-expired-weak-proof-final2/$variant/h3_interop_server-4s --expect-expired $expected --output /var/tmp/cnetmod-h3-expired-weak-proof-4s-$variant.json > /var/tmp/cnetmod-h3-expired-weak-proof-4s-$variant.log 2>&1
done
