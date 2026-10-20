#!/usr/bin/env python3
"""Build a separate, explicitly patched client; never alter the pinned original."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess

PIN = "11c802b2bdfe9af2fac4ac2571ae2de4c530cc62"
OLD = "bid, w->buf_mask, w->buf_index++);"
NEW = "bid, w->buf_mask, 0);"


def correct_return_buffer(source):
    # Initial bulk provisioning uses i, not bid, and must retain its offsets.
    if source.count(OLD) != 1:
        raise ValueError("unexpected pinned return_buffer implementation")
    return source.replace(OLD, NEW)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    if args.source.resolve() in args.output.resolve().parents:
        parser.error("output must be outside the original source tree")
    revision = subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=args.source, text=True).strip()
    if revision != PIN:
        parser.error(f"expected gcannon {PIN}, got {revision}")
    subprocess.run(["git", "diff", "--exit-code", "HEAD", "--", "src", "include", "Makefile"],
                   cwd=args.source, check=True)
    if args.output.exists():
        parser.error("output must be a new directory")
    shutil.copytree(args.source, args.output,
                    ignore=shutil.ignore_patterns(".git", "build", "gcannon"))
    worker = args.output / "src/worker.c"
    original = worker.read_text()
    worker.write_text(correct_return_buffer(original))
    subprocess.run(["make", "-j4"], cwd=args.output, check=True)
    digest = lambda path: hashlib.sha256(path.read_bytes()).hexdigest()
    manifest = dict(upstream_commit=PIN, correction="return_buffer: buf_offset=0 before advance(1)",
                    original_worker_sha256=hashlib.sha256(original.encode()).hexdigest(),
                    corrected_worker_sha256=digest(worker), binary_sha256=digest(args.output / "gcannon"),
                    original_binary_sha256=digest(args.source / "gcannon"),
                    build_command=["make", "-j4"])
    (args.output / "correction.json").write_text(json.dumps(manifest, indent=2) + "\n")


if __name__ == "__main__":
    main()
