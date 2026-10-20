#!/bin/bash
set -euo pipefail
cache=/var/cache/pacman/pkg/compiler-rt-23.1.3-2-x86_64.pkg.tar.zst
base=https://archive.archlinux.org/packages/c/compiler-rt/compiler-rt-23.1.3-2-x86_64.pkg.tar.zst
curl --fail --location --max-time 60 --retry 2 "$base" -o "$cache"
curl --fail --location --max-time 30 --retry 2 "$base.sig" -o "$cache.sig"
pacman -U --needed --noconfirm "$cache"
sha256sum "$cache"
