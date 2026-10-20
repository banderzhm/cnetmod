# Final Linux validation

Candidate arena SHA256: `05fe27108dd833f86493aa2d00eefe7efef9df47805be198ef3eacd09042c479`.

Full selected CTest suite: 35/35 passed, zero failed/skipped, 96.67 seconds. All six acceptance peers passed. Tests and peers run sequentially after Windows released the shared execution window.

Build completed successfully with the frozen production sources. Existing signedness warnings in test_framework.hpp were emitted while rebuilding test_quic_varint; no build error. Build stdout was captured by the task tool, not a standalone log.

The first test-launch attempt stopped before CTest because the private-mount wrapper left an empty /tmp target directory after the preceding build namespace exited. The guard was corrected to permit binding only an empty nonsymlink directory in a private mount namespace; the actual build is /var/tmp/cnetmod-h3-arena-build. No protocol source or test expectation changed. The archived test log belongs to the successful launch.

This is correctness validation, not a performance ranking or final memory acceptance. The protected repository h3-interop-results.json was not used as the result destination.
