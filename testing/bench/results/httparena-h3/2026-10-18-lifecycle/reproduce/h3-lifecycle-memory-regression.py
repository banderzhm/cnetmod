"""External, serial lifecycle/memory regression; does not modify the repository."""
import argparse
import asyncio
import ctypes
import importlib
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import time


def memory_sample(pid):
    if os.name != "nt":
        fields = {}
        for line in Path(f"/proc/{pid}/smaps_rollup").read_text().splitlines():
            pieces = line.split()
            if len(pieces) >= 3 and pieces[2] == "kB":
                fields[pieces[0].rstrip(":")] = int(pieces[1]) * 1024
        return {"rss_bytes": fields["Rss"], "pss_bytes": fields.get("Pss"),
                "private_bytes": fields.get("Private_Clean", 0) + fields.get("Private_Dirty", 0)}
    from ctypes import wintypes
    class Counters(ctypes.Structure):
        _fields_ = [("cb", wintypes.DWORD), ("PageFaultCount", wintypes.DWORD)] + [
            (name, ctypes.c_size_t) for name in (
                "PeakWorkingSetSize", "WorkingSetSize", "QuotaPeakPagedPoolUsage",
                "QuotaPagedPoolUsage", "QuotaPeakNonPagedPoolUsage", "QuotaNonPagedPoolUsage",
                "PagefileUsage", "PeakPagefileUsage", "PrivateUsage")]
    kernel = ctypes.WinDLL("kernel32", use_last_error=True)
    psapi = ctypes.WinDLL("psapi", use_last_error=True)
    kernel.OpenProcess.argtypes = [wintypes.DWORD, wintypes.BOOL, wintypes.DWORD]
    kernel.OpenProcess.restype = wintypes.HANDLE
    kernel.CloseHandle.argtypes = [wintypes.HANDLE]
    psapi.GetProcessMemoryInfo.argtypes = [wintypes.HANDLE, ctypes.POINTER(Counters), wintypes.DWORD]
    psapi.GetProcessMemoryInfo.restype = wintypes.BOOL
    handle = kernel.OpenProcess(0x0400 | 0x0010, False, pid)
    if not handle:
        raise ctypes.WinError(ctypes.get_last_error())
    try:
        counters = Counters()
        counters.cb = ctypes.sizeof(counters)
        if not psapi.GetProcessMemoryInfo(handle, ctypes.byref(counters), counters.cb):
            raise ctypes.WinError(ctypes.get_last_error())
        return {"rss_bytes": counters.WorkingSetSize, "private_bytes": counters.PrivateUsage}
    finally:
        kernel.CloseHandle(handle)


def summarize(samples):
    values = [sample["private_bytes"] for sample in samples]
    if not values:
        return {}
    count = len(values)
    xs = list(range(count))
    denominator = count * sum(x*x for x in xs) - sum(xs)**2
    slope = ((count * sum(x*y for x, y in zip(xs, values)) - sum(xs) * sum(values))
             / denominator if denominator else 0)
    return {"samples": count, "private_min": min(values), "private_max": max(values),
            "private_start": values[0], "private_end": values[-1],
            "private_end_growth": values[-1] - values[0],
            "private_slope_bytes_per_sample": slope}


async def exercise(args, lifecycle, server, report):
    from aioquic.buffer import encode_uint_var
    from aioquic.h3.connection import encode_frame
    async with lifecycle.connect("127.0.0.1", args.port,
            configuration=lifecycle.configuration(), create_protocol=lifecycle.Probe) as peer:
        assert await peer.get(args.port) == b"ok\n"
        for index in range(args.long_streams):
            stream = peer._quic.get_next_available_stream_id()
            reset = peer.expect_reset(stream)
            # Future-stream priorities plus RESET exercise retirement of both
            # scheduler metadata and transport final-size history.
            priority = encode_frame(0xF0700, encode_uint_var(stream) + b"u=1")
            peer._quic.send_stream_data(peer.h3._local_control_stream_id, priority)
            peer._quic.reset_stream(stream, 0x10C)
            peer.transmit()
            await asyncio.wait_for(reset, 5)
            peer.resets.pop(stream)
            if (index + 1) % 100 == 0:
                assert await peer.get(args.port) == b"ok\n"
            if (index + 1) % 1000 == 0 or index + 1 == args.long_streams:
                count = int(await peer.get(args.port, b"/active-connections"))
                assert count == 1, ("long connection", index, count)
                sample = {"streams": index + 1, "active_connections": count,
                          **memory_sample(server.pid)}
                report["long_connection"].append(sample)
                print("LONG", sample, flush=True)
    await asyncio.sleep(args.pause)
    for index in range(args.rounds):
        await lifecycle.run(args.port)
        await asyncio.sleep(args.pause)
        async with lifecycle.connect("127.0.0.1", args.port,
                configuration=lifecycle.configuration(), create_protocol=lifecycle.Probe) as probe:
            count = int(await probe.get(args.port, b"/active-connections"))
            # The only live connection is this probe. All prior round owners
            # must have retired; closing this probe restores the zero baseline.
            assert count == 1, ("round cleanup", index + 1, count)
        await asyncio.sleep(args.pause)
        sample = {"round": index + 1, "active_connections_with_probe": count,
                  "elapsed_seconds": round(time.monotonic() - report["started"], 3),
                  **memory_sample(server.pid)}
        report["rounds"].append(sample)
        args.results.write_text(json.dumps(report, indent=2), encoding="utf-8")
        print("ROUND", sample, flush=True)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--server", type=Path, required=True)
    parser.add_argument("--source", type=Path, required=True, help="repository testing/http3 directory")
    parser.add_argument("--results", type=Path, required=True)
    parser.add_argument("--rounds", type=int, default=30)
    parser.add_argument("--long-streams", type=int, default=10000)
    parser.add_argument("--warmup", type=int, default=5)
    parser.add_argument("--pause", type=float, default=0.8)
    args = parser.parse_args()
    sys.path.insert(0, str(args.source.resolve()))
    lifecycle = importlib.import_module("http3_lifecycle")
    args.server = args.server.resolve()
    args.results = args.results.resolve()
    args.results.parent.mkdir(parents=True, exist_ok=True)
    args.port = lifecycle.reserve_port()
    report = {"started": time.monotonic(), "server": str(args.server),
              "pid": None, "long_connection": [], "rounds": [], "passed": False,
              "warmup_rounds": args.warmup}
    with tempfile.TemporaryDirectory(prefix="h3-lifecycle-memory-") as folder:
        cert, key = lifecycle.make_certificate(Path(folder))
        log_path = args.results.with_suffix(".server.log")
        with log_path.open("w", encoding="utf-8") as log:
            server = subprocess.Popen([str(args.server), "--port", str(args.port),
                "--cert", str(cert), "--key", str(key), "--lifecycle-test"],
                cwd=folder, text=True, stdout=log, stderr=log)
            report["pid"] = server.pid
            try:
                asyncio.run(exercise(args, lifecycle, server, report))
                report["passed"] = True
            finally:
                report["round_summary_after_warmup"] = summarize(report["rounds"][args.warmup:])
                report["long_connection_summary"] = summarize(report["long_connection"])
                report["elapsed_seconds"] = time.monotonic() - report.pop("started")
                args.results.write_text(json.dumps(report, indent=2), encoding="utf-8")
                lifecycle.stop_process(server)
    print(json.dumps(report["round_summary_after_warmup"], indent=2), flush=True)


if __name__ == "__main__":
    main()
