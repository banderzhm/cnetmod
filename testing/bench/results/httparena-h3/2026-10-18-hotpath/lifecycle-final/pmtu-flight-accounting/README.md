# PMTU loss and NewReno flight accounting: directed correctness validation

Native Windows x64, Visual Studio 18 2026, Release. Build directory: `C:/Users/25007/.codex/tmp/cnetmod-h3-windows`. These are correctness unit runs; no throughput workload was run.

Two source fixes were applied: PMTU probe bytes leave congestion flight accounting without becoming a congestion-loss signal, both on ACK-based detection and pure-probe PTO cleanup; NewReno subtracts lost bytes before its recovery guard, so a second loss event releases flight without reducing the window twice. Ordinary loss behavior, retained reliable frames, frame queues and packet construction were preserved.

The same final tests were built and run against the saved old production sources first: congestion 19 passed / 1 failed (the second NewReno loss left 1000 phantom bytes); loss detection 21 passed / 3 failed (ACK-detected probe loss and probe PTO cleanup reduced all three controllers' windows, and a probe carrying a reliable STREAM frame also reduced cwnd). The mixed ordinary-loss cases already passed on the old source. See `old-*.log` and `old-results.json`.

After applying only the two production fixes, congestion passed 20/20 and loss detection passed 24/24. CTest passed 2/2. All six newly added cases passed. The PMTU ACK-loss test compares with a matched controller that sees the same ordinary ACK, so it does not assume that Reno, CUBIC and BBR share a growth rule. Tests also require full flight release, disappearance of the PTO deadline, preservation of a probe's reliable retransmission frame, and ordinary-loss congestion response.

`before-sha256.json` and `new-source-sha256.json` identify the four changed files. Old/new compiler logs, binary hashes, direct test logs, CTest output and the minimal patch are preserved here. Full old/new `.lib` and `.exe` copies plus byte-exact source backups are at `C:/Users/25007/.codex/tmp/pmtu-cc-review/validation/`. The applied patch is based on the working tree at acquisition of this serial validation window and preserves the existing packet-node-cache changes.

Linux coverage is deferred to the coordinator's final cross-platform suite. The separate external dynamic-MDS proposal was not applied or compiled. No claim of full downward PMTU or post-discovery blackhole recovery is made.
