# Serial real-failure regression

Preparation only. Execute after lifecycle explicitly releases compilation and
network ownership. These commands compile external objects and link external
binaries, without Ninja or edits to repository/build artifacts. Each build uses
its own complete PCM/archive set; the old snapshot also binds its saved source
tree over the original source path in a private mount namespace. LLVM's archiver
replaces exactly one member in an external archive copy, including its LTO index.

Use fresh output names. Both paths below are Linux paths in archlinux WSL.

```sh
python /mnt/c/Users/25007/.codex/tmp/cnetmod-quic-packet-failure-proposal/build_fault.py \
  --build /var/tmp/cnetmod-h3-crypto-old-sync/build \
  --frozen-source /var/tmp/cnetmod-h3-crypto-old-sync/src \
  --frozen-manifest /var/tmp/cnetmod-h3-crypto-old-sync/manifest.json \
  --expect-connection-sha 573261cc8dd2711b4f715df82f4f31695dcb28927e89484464016670153c845a \
  --expect-interface-sha e97704fc5324e6a2a94356e416d12e99b49db037f53639b75dce82acdc1df6fa \
  --out /var/tmp/cnetmod-h3-crypto-old-real-fault

python /mnt/c/Users/25007/.codex/tmp/cnetmod-quic-packet-failure-proposal/build_fault.py \
  --build /var/tmp/cnetmod-h3-arena-build \
  --expect-connection-sha 7681a46f45ee44af9866d6454788ec972f57018e550ae642f96fa1051f7b6cb7 \
  --expect-interface-sha 94f6ccaaef8520332d2d88e77ae67ba6dc5480bf99c00f04921a898914038e54 \
  --out /var/tmp/cnetmod-h3-crypto-new-real-fault
```

Run the Python harness with the same aioquic interpreter as lifecycle's passing
raw-QUIC regressions. Direct numeric IPv4 endpoints avoid libc localhost address
ordering; this fixture does not use h2load or large UDP packets. Every process
belongs to the current harness and is terminated/waited in `finally`.

```sh
PYTHON /mnt/c/Users/25007/.codex/tmp/cnetmod-quic-packet-failure-proposal/quic_packet_failure.py \
  --binary /var/tmp/cnetmod-h3-crypto-old-real-fault/quic_packet_failure_server \
  --helpers /mnt/c/Users/25007/.codex/worktrees/2c3c/cnetmod/testing/http3 \
  --mode observer --fault seal --out /var/tmp/cnetmod-h3-crypto-old-proof

PYTHON /mnt/c/Users/25007/.codex/tmp/cnetmod-quic-packet-failure-proposal/quic_packet_failure.py \
  --binary /var/tmp/cnetmod-h3-crypto-new-real-fault/quic_packet_failure_server \
  --helpers /mnt/c/Users/25007/.codex/worktrees/2c3c/cnetmod/testing/http3 \
  --mode both --fault all --out /var/tmp/cnetmod-h3-crypto-new-proof
```

Old negative must fail because it accepts/discards or fails to terminate after
the counted actual crypto error, not because of compilation/readiness/handshake
failure. Preserve its raw `result`, logs, command and exit status. New positive
must pass six cases: two receive modes times AEAD, HP and AEAD with zero reported
error. Require two stream-0 packet candidates, one packet prepared before the
fault, one observed actual failure, no bytes from that unsent batch, a nonzero
terminal error before 500ms, and a subsequent write error. Internal acceptance
completion timing is unchanged; this tests the writer still awaiting its own
flush, whose public API already prioritizes a terminal error. It does not assert
that earlier successful public writes can be retroactively revoked.

For each build inspect `integrity.json` (`unchanged: true`), input snapshots,
compile/link command JSON, `archive-member-proof.json` (extracted replacement
member equals injected object and differs from the original), `link-proof.json`
(external archive and connection member appear in the final linker input trace),
and binary hash in `outputs.json`. A path alias cannot silently retain the
original library: the sole `libcnetmod_quic.a` argument must be replaced exactly.
The map is retained and hashed as output-layout evidence; ThinLTO can replace
input names with synthesized objects, so map input-name presence is not required.
The third case
explicitly uses a synthetic zero reporting code after a real AEAD failure; the
first two preserve and assert actual library error categories/values.
