# Static cold round6 failure: complete response FIN, unacknowledged request FIN

This is an instrumented correctness diagnostic, not a throughput ranking. Source run: `/var/tmp/cnetmod-h3-cold-qlog-0578225f3b154de899376961c761b54a`, round6. Binary SHA256 is recorded in summary/manifest; driver and parser snapshots/hashes are archived. Rounds1..5 passed the diagnostic gates. Round6 h2load reports196972done/succeeded,1970132xx and3028failed. The197013 request streams actually emitted by the client all have contiguous request and response ranges with FIN in qlog. Headers/transport FIN are separate from h2load's stream completion accounting.

Target server CID `15b7290e4d4f5dfe` maps to `client.5.0.sqlog` through transport parameter/CID fields. It has138 client-initiated bidi streams SID0..548(step4), all request FINs and response FINs have contiguous0..final-size coverage. The client emitted ACK coverage for every response FIN packet. No STREAM RESET/STOP or CONNECTION_CLOSE frame appears in this connection qlog. The final qlog event is a client pure ACK PN48 at44ms; final metrics bytes_in_flight5776 equals congestion_window5776 and pto_count0. The subsequent server idle close is in server.log, rather than a received QUIC close frame in qlog.

Exactly41 request streams have no server ACK covering any packet carrying their FIN, including every original/retransmitted copy. This equals the whole-run1970132xx minus196972done gap, a strict numeric correspondence. The other97 request FINs are covered. The unacknowledged stream/packet mapping is:

| Request streams | Client packet(s), qlog time | Meaning |
|---|---|---|
|368..424(step4),15streams|PN30 at8ms, retransmitted inPN36 at10ms|Neither copy is covered by any received server ACK|
|448..492(step4),12streams|PN33 at9ms|New requests, not retransmissions of≤31|
|496,1stream|PN34 at9ms|New request|
|500..544(step4),12streams|PN35 at10ms|New requests|
|548,1stream|PN36 at10ms|New request appended alongside15 retransmissions|

ClientPN33..36 are each1444-byte packets containing ACK, STREAM FIN frames with offset0 and PADDING. Last received server ACK is serverPN1177 at10ms and acknowledges only clientPN31. Later response payload/FINs exist for new SID448..548; the138 server retire count cannot be explained solely by requests already carried in≤31. Server packet numbers1312..1323 and1325..1612 are absent from client received packets;1324 was received, and1613/1614 arrive at40ms. ACK carriers for the newer client packets could have been among the missing server packets, but that is an inference: this diagnostic lacks a server per-packet sent trace, so it does not prove which missing packet contained which ACK.

MAX_STREAMS bidi absolute limits seen by this client are remote initial100,150at3ms,200at10ms, repeated200at40ms. Highest opened request ordinal138 is below200; no client STREAMS_BLOCKED frame is present. This excludes exhausted advertised stream-count credit for this connection. It does not exclude congestion blocking caused by the outstanding unacknowledged request packets: four1444-byte packets account for the final5776bytes in flight.

The critical unidirectional streams have complete initial ranges: client controlSID2[0,18), encoderSID6[0,1), decoderSID10[0,1); server controlSID3[0,3), encoderSID7[0,1), decoderSID11[0,1). None has FIN, which is expected while these critical streams stay open. There is no later encoder/decoder instruction payload or gap in these initial ranges. This qlog provides no evidence of missing QPACK bytes/dynamic-insert blocking; observed2xx headers also imply decoded response headers. Qlog records lengths, not plaintext payload, so stream-role naming uses the peer's standard creation order and should not be presented as a byte-level QPACK decode.

The evidence separates two questions: why request ACK evidence stops at31 despite all responses arriving, and why the client emits no PTO probe after44ms despite outstanding flight. Qlog alone does not resolve the client timer integration. Existing server code constructs ACK from only its pending packet-number set and clears that set after packet construction; a lost ACK carrier can remove that historical evidence from future ACKs. ACK-history retention and a separate pending/dirty bit are under review; simply retaining a nonempty pending set would cause repeated ACK-only construction. No fix or causality claim is validated by this archive.

The target raw qlog and detailed auditJSON are archived here. Other63 qlogs, the137MB-class whole-round qlog summary, request TSV and diagnostic ELF remain at external paths with SHA256/size entries in manifest.json. Avoid committing those large artifacts. All analysis was offline and limited to the target connection; no new build or network workload was run by this agent.


## Follow-up: missing HANDSHAKE_DONE and application PTO

A complete scan of target client.5.0.sqlog finds no HANDSHAKE_DONE frame. A separate healthy-connection comparison, client.0.0.sqlog (15,383,619bytes;76,821records; last event349ms), also contains none. The local ngtcp2 qlog implementation explicitly serializes HANDSHAKE_DONE in ngtcp2_qlog.c625/861, so this is not an omitted schema field. Current source search across src/protocol/quic finds only the frame enum/struct and codec implementation, with no connection-side enqueue/send reference. This supports a general missing server send, rather than a proven isolated lost-DONE carrier.

The server must emit HANDSHAKE_DONE after completing TLS ([RFC9001§4.1.2](https://www.rfc-editor.org/rfc/rfc9001.html#section-4.1.2)); it must retain/retransmit the frame until acknowledged ([RFC9000§13.3](https://www.rfc-editor.org/rfc/rfc9000.html#section-13.3)). The RFC also permits optional client handshake confirmation from a1RTT ACK; absence of DONE alone therefore does not universally prove every client is unconfirmed. The pinned ngtcp2 implementation's confirmation flag/PTO gating must be checked together. The independent client-source reviewer identified a branch suppressing application PTO while handshake confirmation is absent; its confirmation write sites remain the closing check. No client PTO timer event exists in this qlog, and only1RTT PN30 is marked lost;33..36 never are.

This adds a strong explanation candidate for why lost request ACKs did not lead to client PTO repair. It does not replace the proven missing-ACK evidence above, and neither fix is validated by this diagnostic archive.


Independent source review subsequently closed the flag condition for pinned ngtcp2v1.25: client HANDSHAKE_CONFIRMED is set in recv_handshake_done (ngtcp2_conn.c8671); the other write at10918 is guarded by server role.1RTT ACK paths9164/9638 set SERVER_ADDR_VERIFIED only; this implementation does not choose the optional1RTT-ACK handshake confirmation. Its loss-timer path13417/13436 suppresses application PTO while unconfirmed. Thus this client-source condition and the absent DONE frame jointly explain the missing application PTO. These source line numbers refer to the reviewer's exact pinned v1.25 raw source, not web-rendered line numbers. End-to-end causal confirmation still requires a controlled repair/loss regression.
