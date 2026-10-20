# Ready-barrier directed verification

All five cases (`pto`, `pto-full`, `close`, `draining`, `active`) passed for both 1 and 2 workers with immediate restart enabled. PTO/full/draining socket errors are empty in these final runs. Credit notification passed observer/private modes; private delayed-send/drop passed without an observer.

`pto-full` verifies a dropped 1199-byte datagram containing the unchanged single 1166-byte STREAM payload and FIN. It also passed against the prior implementation; a speculative PTO priority production change was withdrawn. The private-send scenario also passed before and after and is not evidence of an old failure.

The draining negative in the parent directory returned a token-matching reset about 1 ms after CLOSE; these positive runs observe silence inside the implementation's guaranteed 78 ms minimum and a matching reset only after actual expiry. The independent active-count request confirms the server has processed CLOSE. No fixed 50 ms expiry is assumed.

Earlier non-ready startup ICMP evidence remains in the parent directory and is not classified as a clean run. The final script waits for the server's all-worker readiness log without sending a probe.

This verifies protocol behavior, not destruction of the last closed connection object. The latter still needs an external weak-pointer/destructor probe; active-count zero alone is insufficient.
