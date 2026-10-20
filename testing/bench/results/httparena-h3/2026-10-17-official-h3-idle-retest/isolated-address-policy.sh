#!/usr/bin/env bash
set -euo pipefail
mount --make-rprivate /
mount --bind /var/tmp/cnetmod-h3-official/gai.conf /etc/gai.conf
mkdir -p /var/tmp/cnetmod-h3-official/arena/tmp
exec "$@"
