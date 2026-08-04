#!/usr/bin/env python3
"""Run an HTTP/3 command under Linux tc-netem without masking prerequisites.

The gate deliberately refuses to overwrite a non-default root qdisc.  It only
modifies an explicitly named interface and always removes the qdisc it added.
Use a network namespace/veth pair in CI; do not point it at a production NIC.
"""

from __future__ import annotations

import argparse
import os
import shlex
import shutil
import subprocess
import sys

SKIP = 77


def tc(*arguments: str) -> subprocess.CompletedProcess[str]:
    return subprocess.run(["tc", *arguments], text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)


def main() -> int:
    parser = argparse.ArgumentParser(description="tc-netem HTTP/3 weak-network gate")
    parser.add_argument("--interface", required=True)
    parser.add_argument("--command", required=True, help="quoted client test command")
    parser.add_argument("--delay-ms", type=int, default=80)
    parser.add_argument("--jitter-ms", type=int, default=20)
    parser.add_argument("--loss-percent", type=float, default=2.0)
    parser.add_argument("--reorder-percent", type=float, default=0.0)
    parser.add_argument("--rate-kbit", type=int, default=0)
    args = parser.parse_args()

    if sys.platform != "linux" or not shutil.which("tc"):
        print("SKIP: Linux tc is unavailable")
        return SKIP
    if os.geteuid() != 0:
        print("SKIP: tc-netem requires root or CAP_NET_ADMIN")
        return SKIP
    current = tc("qdisc", "show", "dev", args.interface)
    if current.returncode:
        print(f"SKIP: cannot inspect {args.interface}: {current.stderr.strip()}")
        return SKIP
    # A custom hierarchy cannot be restored generically; never destroy it.
    if "noqueue" not in current.stdout and "pfifo_fast" not in current.stdout:
        print(f"SKIP: refusing to replace non-default qdisc on {args.interface}: {current.stdout.strip()}")
        return SKIP

    netem = ["qdisc", "replace", "dev", args.interface, "root", "netem", "delay", f"{args.delay_ms}ms", f"{args.jitter_ms}ms", "loss", f"{args.loss_percent}%"]
    if args.reorder_percent:
        netem.extend(["reorder", f"{args.reorder_percent}%"])
    applied = tc(*netem)
    if applied.returncode:
        print(f"SKIP: failed to apply tc-netem: {applied.stderr.strip()}")
        return SKIP
    try:
        if args.rate_kbit:
            print("SKIP: rate shaping needs an isolated veth/HTB hierarchy and is intentionally not applied to a host interface")
            return SKIP
        command = subprocess.run(shlex.split(args.command), text=True)
        return command.returncode
    finally:
        removed = tc("qdisc", "del", "dev", args.interface, "root")
        if removed.returncode:
            print(f"WARNING: failed to remove test qdisc: {removed.stderr.strip()}", file=sys.stderr)


if __name__ == "__main__":
    raise SystemExit(main())
