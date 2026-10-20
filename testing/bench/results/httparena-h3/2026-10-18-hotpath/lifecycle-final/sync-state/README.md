# Synchronous STREAM/write bookkeeping: Linux directed validation

Status: implemented and Linux directed tests passed. The later native A/B completed all 12 rounds with zero reported errors: baseline improved in all three pairs (median paired completion-rate gain +22.91%), while static varied from -26.43% to +23.32% and remains unresolved. See [the A/B evidence](native-ab/README.md). Windows validation, allocation recount and memory acceptance remain pending; this is not an accepted official performance result. The original build/test validation step itself ran no throughput workload.

Native candidate remains external: `/var/tmp/cnetmod-h3-sync-native-1792321403/arena_h3_server`, SHA256 `920f7172c770db8ec2a4665c6db2efe712a01501e58d93d7f3d0314a7d30ec3e`. The prior HANDSHAKE_DONE-corrected baseline is `/var/tmp/cnetmod-h3-handshake-retention-1792320533/arena_h3_server`, SHA256 `298bf8277e7b1edc32f7e1ab1ce64729a52e8884e97e9486a77a56a20f4cabc4`. Its12×200k correctness pass is a separate pre-sync measurement, not a12-round result for this candidate.

`applied.patch` SHA256 `56bb8c089d1431c4189e13624ec978c61374d36fbdbb24ddd4ed7ce28395d5f3` was rebased onto connection.cpp `fe3e3c5299668744963587ed049058dd95d839535aef2c079bfb5fae4042bffc`, preserving the two CID-issuance failure protocol-close paths. Patch-base and final source hashes are archived separately. The full manifest records801 common source/PCM/module-map/archive/config/main-object inputs; it contains hashes only, not copied large PCMs/archives/ELF. The native arena target was clean after validation (`clean.log`).

## Change and unchanged semantics

The1RTT packet decode loop routes STREAM frames directly to an internal synchronous state handler beside the existing ACK state path. Other frame classes retain their visitor/task dispatch; the private STREAM coroutine wrapper still supports those callers. The HANDSHAKE_DONE dispatch/role behavior is preserved.

The connection's private write-command application returns expected synchronously. Stream byte/window/local-close bookkeeping remains in quic_stream.cpp private helpers rather than moving into the large connection. Existing public send and close_local remain lazy task functions. No public synchronous methods or data fields were added. A forward export of the existing quic_connection type supports private friend access without a partition import cycle; the Linux module build accepted it.

Whole prefix+body window checks still precede mutation; shared frames copy their owner before the producer command is destroyed. Empty send preserves its existing local-close behavior. Receive readiness retains its local shared owner across retirement and has no later iterator use after notification. Cancellation remains the write helper's final side effect. Completion publication remains after state-lock release. Explicit posts/fairness quanta, lock waits, socket/pacing waits, flow control, packetization, MTU/GSO, recovery and buffer-pool representation were not changed by this patch.

## Behavior tests and result

Linux Release build completed successfully. All19 selected CTest cases passed,0failed and0skipped, in54.01seconds (`tests.log`). This includes:

- test_quic_stream, test_quic_frame, test_quic_loss_detection, test_quic_early_data, test_quic_packet, test_quic_congestion, test_http3_frame and test_http3_qpack.
- http3_lifecycle and http3_cold_listener_cleanup.
- http3_outbound_pto_close_restart_1 and_2, quic_retire_stream_credit_wakeup, quic_private_outbound_pto and quic_handshake_confirmation.
- http3_early_data_recovery, http3_streaming_cancellation, http_client_http3_end_to_end and http_client_http3_dynamic_qpack_end_to_end.

New assertions cover public send/close task creation and unawaited destruction without state mutation, empty send transitions/error behavior, and weak shared-body ownership through a PTO alias and original ACK. The probe wire packet is separately ACKed before requiring its flight timer to disappear. The E2E small-window case retains8owned-body responses and adds8shared-body responses on the same128-byte stream/512-byte connection window, requiring the exact512-byte body and normal FIN completion. Existing flow-window failure atomicity, receive final-size/gaps, shared loss/discard release, notification/cancel/reuse and lifecycle tests are reused. These test behavior, not private helper/cache implementation shape.

The prior frozen ELF audit proved5qualifying shared-path allocations or4owned-path allocations in the target cluster, despite7source task constructions. That audit describes ELF96333f69; final-candidate allocation elimination and dynamic calls/request have not yet been remeasured. No performance saving or parity with agensio is claimed here. Windows will run only after the final candidate is stable, under the root coordinator's serial window.

