# Combined Linux system-allocator validation

Branch fix/http3-arena-performance, HEAD d0d92079 plus archived working changes. Arena SHA2565bf46aeeee227934742969972dfa3e4705d5fb87e66289c9109f41d89f353470. All2209 input hashes unchanged after verification.

37/37 serial CTest cases pass, zero skips,106.40seconds. All six peer directions pass. Includes dynamic ACK-validated MDS and the permanent large-response-after-loss fixture. That directed fixture explicitly uses a process-local large-packet AEAD test-peer adapter; stock aioquic interop is separately covered by the six-peer suite. Test namespace is private --mount --net with loopback up.

Allocator audit verifies CNETMOD_USE_MIMALLOC=OFF, no override object/link dependency, and actual malloc/new runtime binding to system libraries. Benchmark defaults remain1472-byte send discovery/receive ceilings and GSO disabled; new env options are explicit diagnostics. No throughput ranking is inferred from correctness tests. Windows and performance validation are separate.
