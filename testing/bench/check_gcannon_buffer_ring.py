#!/usr/bin/env python3
"""Exercise the client's real return_buffer function against liburing's ring helpers.

No network or kernel ring is needed: publication and buffer ownership are tested
in userspace, including repeated ring wraparound. The pinned old client must fail
this check and the separately corrected client must pass.
"""
import argparse
from pathlib import Path
import subprocess
import tempfile


def check(source):
    worker = (source / "src/worker.c").read_text()
    start = worker.index("static inline void return_buffer(")
    end = worker.index("\n}", start) + 2
    harness = '#include "worker.h"\n#include <stdlib.h>\n' + worker[start:end] + r'''
int main(void) {
    worker_t w = {0};
    w.buf_ring = aligned_alloc(4096, 4096);
    if (!w.buf_ring) return 2;
    w.buf_slab = calloc(4, 16);
    if (!w.buf_slab) return 2;
    w.recv_buf_size = 16;
    w.buf_mask = 3;
    w.buf_index = 0;
    io_uring_buf_ring_init(w.buf_ring);
    for (int i = 0; i < 4; ++i)
        io_uring_buf_ring_add(w.buf_ring, w.buf_slab + 16*i,
                             16, i, w.buf_mask, w.buf_index++);
    io_uring_buf_ring_advance(w.buf_ring, 4);
    for (unsigned i = 0; i < 100000; ++i) {
        unsigned slot = w.buf_ring->tail & w.buf_mask;
        unsigned bid = i % 4;
        return_buffer(&w, bid);
        struct io_uring_buf *published = &w.buf_ring->bufs[slot];
        if (published->bid != bid || published->len != 16 ||
            published->addr != (unsigned long)(w.buf_slab + 16*bid))
            return 1;
    }
    free(w.buf_slab);
    free(w.buf_ring);
    return 0;
}
'''
    with tempfile.TemporaryDirectory() as directory:
        path = Path(directory)
        (path / "check.c").write_text(harness)
        subprocess.run(["gcc", "-O2", "-I", str(source / "include"),
                        str(path / "check.c"), "-o", str(path / "check")], check=True)
        return subprocess.run([str(path / "check")]).returncode


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path)
    args = parser.parse_args()
    result = check(args.source)
    print(f"buffer ring ownership: {'PASS' if result == 0 else 'FAIL'} ({args.source})")
    raise SystemExit(result)
