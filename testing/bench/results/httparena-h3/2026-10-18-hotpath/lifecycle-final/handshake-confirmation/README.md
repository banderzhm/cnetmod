Directed handshake-confirmation evidence, 2026-10-18.

Negative: frozen boundary-negative raw fixture, no HANDSHAKE_DONE production change. TLS completed=true, confirmed=false, zero DONE packets, explicit confirmation timeout.
Positive: current library queues reliable server HANDSHAKE_DONE. PN0 containing DONE was dropped, no client ingress after that drop was allowed until confirmation. Server PTO produced DONE PN1/2 at ~1040ms. Client then confirmed, first request STREAM PN8 was dropped, client application PTO count reached 1, PN9 retransmitted at ~29ms, response FIN and request FIN ACK completed. No decode/socket error.

Both directed invocations used fresh true-loopback network and mount namespaces and the existing gai.conf wrapper, identical raw QUIC fixture parameters (6s idle). Test confirmation budget was 1.5s. Exit status: negative 1 (expected), positive 0.

Source snapshot notes: production connection and raw fixture are exactly the directed build inputs. Python snapshot includes the subsequently prepared 3s Windows confirmation budget and illegal-client-DONE assertion; those two additions have NOT yet been executed at this archive stage. The archived positive JSON documents precisely the assertions already executed. No performance conclusion is drawn from these tests.

Full broad correctness, client-role protocol-error regression and native retention follow separately. Existing ACK history behavior is unchanged. Public TLS handshake completion is distinct from confirmed state.

Follow-up role-validation: both observer and private drivers passed the expanded script. Each received two retransmitted DONE packets, then completed client application PTO. An actual client-sent DONE was rejected with transport PROTOCOL_VIOLATION 0xA and triggering frame type 0x1E. The supplemental validated Python snapshot is separate from the earlier preparation-stage source.
