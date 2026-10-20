Atomic-map erased-value ownership proof, Linux Clang/libc++.

Old negative: new tests were externally compiled against the frozen original Value+bool map PCMs and unchanged runtime. Of five filtered map cases, three passed; both ownership cases failed exactly three weak-expiration assertions, covering erase, erase_if and erase_min_by (six failed assertions total). Exit 1 is the expected negative observation, not a regression accepted as passing. Source/PCM/archive hashes stayed unchanged. Normal old target was built before the external test; no old/new modules were mixed.

New positive: after applying the optional-value tombstone change and rebuilding its modules/consumers, all eleven tests in test_concurrent_containers passed. This includes immediate owner release, concurrent public reader snapshot lifetime, non-default-constructible Value, collision/probe/reinsert/assignment semantics and the previous utility tests.

Actual private entry record layouts were dumped from the real http3_server.cpp instantiation, not a mirror struct. Old and new inbox-map entries are both size 56/alignment 8, with Value/optional storage at offset 32 and erased/engaged flag at offset 48. Slots remain size 64/alignment 64. Empty optional still reserves inline Value storage; this change releases the payload owner, not the metadata allocation, Key or hash-table capacity. Windows layout/behavior is pending its own platform build.

The combined phase build also contains the independently reviewed crypto error-propagation candidate. Its first compile failed on a local sent-variable redefinition; that failed log is preserved and build2 passed after a three-line rename. Those logs do not establish crypto fault-injection correctness or full Linux/Windows regression. The map unit tests isolate the ownership behavior.

Full layout compiler output remains at each /var/tmp/cnetmod-h3-atomic-map-{old,new}-layout/complete-layout.log; filtered actual map record layouts and the invocation/integrity hashes are archived here. No allocator collection or benchmark conclusion is part of this proof.
