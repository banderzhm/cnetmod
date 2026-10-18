# DATAGRAM congestion admission: Linux evidence

Production fix includes unreliable DATAGRAM packets in the existing packet-size congestion admission check. If rejected, already-dequeued DATAGRAM frames are discarded as allowed by RFC 9221 section 5.4; reliable frames retain existing rollback. The PTO and CONNECTION_CLOSE exceptions remain intact. No congestion setter, fabricated ACK, unreliable retransmission, timer suspension, allocator change, or default MTU change is used.

## Identity and scope

- HEAD before change: 51d443d223f4b0855d4b71025aff751a02ea6418; branch fix/http3-arena-performance.
- Changed: src/protocol/quic/quic_connection.cpp, testing/http3/CMakeLists.txt; added quic_datagram_cc_server.cpp and quic_datagram_cc.py under testing/http3.
- Source manifest: ../source-fixed/source-sha256.json, 1318 files. SHA256 ba14e2a7a08d43f2809dcad7387545132b5449181bac22f21ee6d0b5f8f576ee. Includes new fixture files; excludes historical bench/results and protected h3-interop-results.json. source-identity.json lists each changed file hash.
- Frozen fixed arena: /var/tmp/cnetmod-datagram-cc-51d443d2/baseline-fixed/arena_h3_server. SHA256 fc6e50f5935ab7bde6b3767a459fc36efc4b2e666239c0f5099de812db7061c3.
- allocator-proof.json SHA256 f344b7837dd13592e869238fdbf3b3fa6131fe8986b357f501c73aa9d3f0d0cb; OFF verified, no preload/audit, libc malloc binding. All seven hashed backing files are independently copied under frozen baseline allocator-backing, preserving relative paths. Proof's binary field records the build origin; frozen_binary and backing_root identify immutable copies.
- Original 5bf arena remains /var/tmp/cnetmod-datagram-cc-51d443d2/arena_h3_server.before. Not overwritten.

## Controls and results

All final controls use the same final fixture, stock unmodified aioquic 1.7.0 (/var/tmp/cnetmod-h3-peer/bin/python), and independent mount/network namespaces with loopback MTU 65536. The proxy independently authenticates both directions and forwards the real command STREAM before holding subsequent client UDP, including real ACKs. Held original datagrams are later released without modification. Server sends through the public async_send_datagram API. Packet sizes remain <=1200.

- stable-old-admission-negative: default regression exits 1 for the precise congestion assertion. 64 pure-DATAGRAM packets / 72448 UDP bytes exceed conservative Reno window bound 17447 bytes. Burst accepts64 in314us, before the sampled recovery deadline30481us; five real client datagrams held. No proxy errors. This is a real wire overflow, not a timeout or setup failure.
- stable-old-overflow-ack-recovery: explicit old-behavior control proves the same overflow and then fresh DATAGRAM progress after real ACK release. No old-ID retransmission.
- stable-old-pto: reliable STREAM content repeats at a distinct PN with real ACKs held, then completes exact262144 bytes + FIN after release. This preserves existing PTO exemption.
- stable-new-admission-01..10: ten passes; each sends13 packets /14716 UDP bytes <=17447; remaining accepted unreliable data is discarded. Fresh marker after ACK release and no discarded-ID retransmission are checked.
- stable-new-pto and stable-new-all: pass; exact262144 reliable bytes + FIN, real PTO repeat and ACK release recovery. See result and authenticated frame ledgers.
- Stable standalone CTest:2/2 pass,0skip; Linux selected full suite39/39 pass,0skip,103.42s. No six-peer rerun or performance claim in this change.
- Old final fixture ELF SHA eb21801ca5e91c6b863bc71283cdcbe992df79dc0e554836326f2ecc750b3cc6; fixed final fixture SHA af0a87a07e46c15974bfe96feffed32227d0d6f4820213565ec92bbe427d81cd.

Old production was built from the exact saved original connection.cpp into an external object/archive; current final fixture object was linked to that archive. The worktree and normal build library retained the fix. old-isolated-commands.json records compile/link commands. No HEAD/index reset.

## Preserved setup failures and limits

The initial unwired-target configure/build failure is retained externally (prewire logs), not treated as negative evidence. Earlier new-ctest.log includes one setup failure and one PTO pass. post-new-admission-09 proves a fixture assumption error: elapsed62us, sampled deadline1024003us, sampled6us, first_is_due=false. loss_detection.cppm's initial RTT333ms plus4*(333/2)ms plus25ms ACK delay yields a legitimate1024ms PTO. The fixture's former1s timer classification rejected it despite finishing before that deadline. Final warmup requires a deadline>10s with configured idle30s; burst requires a deadline<5s and still strictly finishes before that same actual deadline. A post_awaitable after request completion lets real receive-owner unwind. Both old/new final controls use these identical changes. No overflow oracle was relaxed.

The server's accept loop can log Transport endpoint is not connected after the client closes following completed assertions; such cleanup logs/failure.txt are retained verbatim. They are not the admission negative oracle. Setup/report failures during exercise remain fatal. Success results have no error field and proxy_errors=[]; main explicitly resets status to failed on exceptions, including post-assertion failures.

This is a one-path Reno public-API correctness regression, not a benchmark or an assertion that all QUIC semantics have been exhaustively verified. No certificates, keys, ELF, archives or redundant full source snapshots are included in this evidence directory. Independent external snapshots retain binaries/backing files for subsequent controlled A/B. Windows acceptance is coordinated separately by root.