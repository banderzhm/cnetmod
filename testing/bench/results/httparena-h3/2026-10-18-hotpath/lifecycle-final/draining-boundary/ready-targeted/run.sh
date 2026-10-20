#!/bin/bash
set -euo pipefail
ip link set lo up
cd /mnt/c/Users/25007/.codex/worktrees/2c3c/cnetmod
for workers in 1 2; do
 /var/tmp/cnetmod-h3-peer/bin/python testing/http3/http3_outbound_pto.py /var/tmp/cnetmod-h3-arena-build/bin/h3_interop_server --workers "$workers" --restart --output "/var/tmp/cnetmod-h3-boundary-positive-all-$workers.json" > "/var/tmp/cnetmod-h3-boundary-positive-all-$workers.log" 2>&1
done
/var/tmp/cnetmod-h3-peer/bin/python testing/http3/quic_retire_stream.py /var/tmp/cnetmod-h3-arena-build/bin/quic_retire_stream_server --mode all --output /var/tmp/cnetmod-h3-boundary-positive-credit.json > /var/tmp/cnetmod-h3-boundary-positive-credit.log 2>&1
/var/tmp/cnetmod-h3-peer/bin/python testing/http3/quic_retire_stream.py /var/tmp/cnetmod-h3-arena-build/bin/quic_retire_stream_server --scenario private-pto --output /var/tmp/cnetmod-h3-boundary-positive-private.json > /var/tmp/cnetmod-h3-boundary-positive-private.log 2>&1
