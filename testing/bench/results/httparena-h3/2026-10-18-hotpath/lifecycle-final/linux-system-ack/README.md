# Linux system-allocator regression after ACK validation

Branch fix/http3-arena-performance, HEAD d0d92079 plus archived dirty source diff. CNETMOD_USE_MIMALLOC=OFF. Rebuilt arena SHA256 4cf852610eaa366ac5ed64907645f0d39f3e93d1b06a1f1404e455b05c8f170b. All1155 recorded source inputs rechecked after tests, before releasing the next mutation window.

36/36 serial CTest cases pass, zero skips,100.63s. All six peer directions pass. Includes notification RAII, PMTU probe-loss/Reno flight accounting, experimental opt-in GSO and ACK-range atomic validation; does not include dynamic MDS phase2. These are correctness regressions, not a new performance comparison.

Commands use local WSL archlinux as root with private mount/network namespace and the archived scripts. Build output retains compiler warnings; build exit0. Existing protected h3-interop-results.json was not used as output. The first archive wrapper expected an older CTest summary wording; no test failed, and this wrapper was corrected to require36 explicit Passed rows and the actual summary.
