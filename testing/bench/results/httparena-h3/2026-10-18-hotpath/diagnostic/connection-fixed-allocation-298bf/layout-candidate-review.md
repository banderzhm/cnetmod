# Descriptor-only command-ring candidate rejected

This was a read-only follow-up; the final production candidate remains frozen.
Capacity stays 512 and no allocator option or reclamation call is introduced.

An initially attractive idea was to store normal input bytes/shared body in a
command's existing completion object, enqueue only a small descriptor, and
restore the public command at dequeue. A <=56-byte descriptor would fit with
the sequence in a 64-byte ring cell, halving the 512-cell backing request from
65,536 to32,768 bytes. For this mimalloc build that would move the aligned store
from the81,920 class to40,960. This is not an equivalent simple optimization.

`quic_write_command`, `quic_write_completion` and the queue are exported. Their
existing public fields must remain valid. A caller may submit a command whose
completion is null and whose bytes/shared body are nonempty; `fail_all` explicitly
skips completing null pointers, but still owns/destroys the command payload.
The original API permits512 null-completion commands in the ring while all512
completion pool slots remain available. Borrowing completion slots for those
commands would change `try_acquire` and independent capacity semantics.

Even non-null completion pointers can refer to caller-owned objects rather
than this queue's pool. Moving input into the public `rejected_*` fields before
publication changes publicly observable state and may overwrite existing values.
Keeping the exported command shape and moving data back on dequeue does not
undo those earlier mutations. A fallback for null/external completions needs
independent per-ring-slot payload storage or new allocations, with additional
capacity, OOM, destruction-order and teardown implications. It is not the small
layout-only change proposed here.

The ordinary consumer currently owns payload in the dequeued local command.
On flow-control rejection it transfers payload to completion before notification;
on success the local command releases its payload at the end of that dequeue
iteration, before synchronous producer notification. A redesign must preserve
that lifetime. Merely clearing a vector retains its allocation; keeping normal
payload in completion until the producer later resumes can extend body lifetime
through an awaited send flush. Inline completion may also let a producer release
and reacquire the same slot before the notifier returns: no slot reference may
be accessed after that notification. The current ownership boundary avoids the
new descriptor/payload alias.

Simple field reordering is insufficient: the public command is72 bytes here;
the minimum field total is60 bytes (vector24, shared_ptr16, stream8, completion8,
operation/fin/urgency/incremental4), so reordering can at best round to64. Adding
the ring sequence still exceeds64 and the aligned cell remains128. Removing the
cell's64-byte alignment or densely separating sequence metadata can save space,
but introduces adjacent-slot false sharing and a wider container-layout change.
It requires separate correctness and contention measurements, not a capacity cut.

Private completion result encoding could independently reduce its88-byte object,
but needs to preserve empty/success/failure, including failure with error_code0.
Root deferred it as a separate variable. Stronger4KiB backing alignment could
avoid this allocator's specific size+63 fallback for large queues, but needs a
matching custom allocation/deletion path and cross-allocator validation; globally
overaligning small queues/tasks would substantially increase their footprint.
None of these alternatives is applied or compiled in this experiment.
