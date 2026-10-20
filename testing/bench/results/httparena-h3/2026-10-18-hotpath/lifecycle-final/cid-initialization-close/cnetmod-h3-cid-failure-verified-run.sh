set -euo pipefail
ip link set lo up
/var/tmp/cnetmod-h3-peer/bin/python /mnt/c/Users/25007/.codex/tmp/cnetmod-h3-cid-failure-run.py /var/tmp/cnetmod-h3-cid-failure-controls-final/without-cid-close/quic_retire_stream_server-corrected --expect-close no --output /var/tmp/cnetmod-h3-cid-failure-verified-negative.json > /var/tmp/cnetmod-h3-cid-failure-verified-negative.log 2>&1
/var/tmp/cnetmod-h3-peer/bin/python /mnt/c/Users/25007/.codex/tmp/cnetmod-h3-cid-failure-run.py /var/tmp/cnetmod-h3-cid-failure-controls-final/with-cid-close/quic_retire_stream_server-corrected --expect-close yes --output /var/tmp/cnetmod-h3-cid-failure-verified-positive.json > /var/tmp/cnetmod-h3-cid-failure-verified-positive.log 2>&1
