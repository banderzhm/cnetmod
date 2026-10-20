# CID initialization error closes the connection

The two controlled fixtures use the same current modules and runtime. The negative removes only the two new queue_protocol_close calls. Both set cid_length=0 before the handshake, triggering the existing CID issuance validation after TLS completion. This is a local loopback error-handling regression, not a throughput test.

Both verified invocations exited zero against their distinct expected outcomes. The negative completed TLS but emitted no server CLOSE during the three-second observation (below its six-second idle timeout); handshake confirmation remained false. The positive completed TLS and emitted CONNECTION_CLOSE with INTERNAL_ERROR=1 and triggering CRYPTO frame=6. No application request is sent in either case.

The retained fixture stderr reports "expected stream zero" when its unrelated stream waiter is unwound at connection teardown. That is outside the observed wire outcome; these results are not described as clean application-stream tests or zero-log-error tests.

The builder initially omitted aioquic transmit with wait_connected=False, then an archive-path comparison in a private mount namespace selected the wrong control archive. Those attempts are invalid controls, retained externally under /var/tmp/cnetmod-h3-cid-failure-controls-final and the cnetmod-h3-cid-failure-* earlier logs. The final run uses corrected linking, exactly one external QUIC archive argument, extracted archive member hashes, and mold --trace proving the archive/member actually used. ThinLTO maps contain merged symbols and are retained externally as layout evidence, not input identity proof. A CRLF shell invocation failed before either fixture launched; the verified runner is LF-normalized.

Production source SHA256 at verification: quic_connection.cpp 7681a46f45ee44af9866d6454788ec972f57018e550ae642f96fa1051f7b6cb7; interface 94f6ccaaef8520332d2d88e77ae67ba6dc5480bf99c00f04921a898914038e54. No production or module inputs changed during the external controls. No ELF/PCM or private certificate is copied here.
