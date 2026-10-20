# 1-RTT packet protection failure regression

Executed 2026-10-18 in the assigned serial Linux window. The old implementation
ran **one** negative case (`observer/seal`) and failed the regression as expected.
The new implementation ran **six** positive cases (observer/private receive,
each with AEAD, HP, and AEAD plus a zero reported error) and passed all six.
No additional old HP/private/zero-error cases were run.

The old negative actually encountered BoringSSL `openssl:503316583` while
building the second response packet, with one previous packet already sealed.
It nevertheless reported successful acceptance, stayed open, and continued to
four response candidates / three committed packets. This is a true triggered
negative, not a readiness, linking, or handshake failure. Its driver exited 1
at the expected assertion (`status == error`); raw report and server log remain.

Every new case returned an error in 0ms, closed the connection, and returned the
same terminal error to the subsequent write. There were exactly two response
packet candidates, one prepared before the failure, and one actual failure.
The client received zero response bytes: the prepared prefix in this failed
batch was not sent. Error categories/values were respectively openssl:503316583,
generic:22, and generic:5. The third case triggers a real AEAD failure and then
synthetically replaces only its reported error code with zero to test the local
nonzero fallback. It is not described as a naturally empty OpenSSL error queue.

Internal queue-acceptance completion timing remains unchanged. This fixture
tests a public writer still awaiting its own flush, whose existing final
terminal-error check wins. It does not retract any already returned successful
public call, promise peer delivery, or broaden close concurrency guarantees.

Old and new use their own complete coherent PCM/runtime/archive sets. The old
source and build are privately mounted at their original recorded paths; no
module paths are rewritten and Clang validation is enabled. Only external
objects and an external copied QUIC archive are changed. Both complete input
snapshots report unchanged. For each link, the sole QUIC archive argument is
the external absolute path; its extracted connection member equals the injected
object hash and differs from the original member, and mold's actual input trace
lists that external archive/member. ThinLTO maps are output-layout evidence and
can omit input names. Compressed maps preserve their original content; original
uncompressed SHA is recorded in link-proof.json.

Binary hashes, executed scope and cleanup are in summary.json. No fixture
process remained when the exclusive window was released. Original Linux
artifacts remain under `/var/tmp/cnetmod-h3-crypto-{old,new}-real-fault` and
`/var/tmp/cnetmod-h3-crypto-{old,new}-proof`; this archive omits binaries, object
files, static archives and ephemeral certificate keys while retaining their
hashes, exact commands, injected sources and proof logs.

This fixes an independent error-propagation defect. No evidence links healthy
1472-byte traffic to these protection failures. The formerly observed stalled
connections were addressed separately by handshake confirmation.
