"""Run isolated benchmark processes against an owned ephemeral loopback peer."""

import argparse
import hashlib
import http.server
import json
import os
from pathlib import Path
import platform
import re
import subprocess
import sys
import threading

from analyze_http_observation import analyze, analyze_report
from http_observation_peer import Handler


ROW = re.compile(r"round=(\d+) mode=(raw|disabled) requests=(\d+) rps=([\d.eE+-]+) p50_us=([\d.eE+-]+) p99_us=([\d.eE+-]+)")


def run(executable, processes, baseline=None, cpu=None):
    executable = Path(executable).resolve(strict=True)
    if processes < 8:
        raise ValueError("at least eight process pairs are required")
    if cpu is not None and (type(cpu) is not int or not 0 <= cpu < 65536):
        raise ValueError("CPU must be an integer in [0, 65536)")
    digest = hashlib.sha256(executable.read_bytes()).hexdigest()
    baseline = Path(baseline).resolve(strict=True) if baseline else None
    if baseline == executable:
        raise ValueError("baseline and candidate must be different executables")
    baseline_digest = hashlib.sha256(baseline.read_bytes()).hexdigest() if baseline else None
    runs = []
    with http.server.ThreadingHTTPServer(("127.0.0.1", 0), Handler) as server:
        worker = threading.Thread(target=server.serve_forever, daemon=True)
        worker.start()
        try:
            for index in range(processes):
                order = [("candidate", executable)]
                if baseline:
                    order.append(("baseline", baseline))
                    if index % 2 == 0:
                        order.reverse()
                measured = {}
                for label, binary in order:
                    command = [str(binary), f"http://127.0.0.1:{server.server_port}/bench"]
                    if cpu is not None:
                        command.append(str(cpu))
                    result = subprocess.run(
                        command,
                        capture_output=True, text=True, timeout=60,
                        creationflags=subprocess.CREATE_NO_WINDOW if os.name == "nt" else 0,
                    )
                    if result.returncode:
                        raise RuntimeError(f"{label} process {index} failed: exit {result.returncode}")
                    if cpu is not None and re.findall(r"benchmark_cpu=(\d+)\b", result.stdout + result.stderr) != [str(cpu)]:
                        raise ValueError("missing or inconsistent CPU affinity acknowledgement")
                    rows = [dict(round=int(r), mode=m, requests=int(n), rps=float(rate),
                                 p50_us=float(p50), p99_us=float(p99))
                            for r, m, n, rate, p50, p99 in ROW.findall(result.stdout + result.stderr)]
                    validation = rows
                    if label == "baseline":
                        if len(rows) != 8 or any(row["mode"] != "raw" for row in rows):
                            raise ValueError("baseline must emit exactly eight raw rounds")
                        validation = rows + [dict(row, mode="disabled") for row in rows]
                    elif baseline and len(rows) == 8 and all(row["mode"] == "disabled" for row in rows):
                        validation = rows + [dict(row, mode="raw") for row in rows]
                    elif len(rows) != 16:
                        raise ValueError("expected exactly eight paired rounds per process")
                    analyze({"rows": validation}, repetitions=1000)
                    measured[label] = rows
                rows = measured["candidate"]
                if baseline:
                    baseline_counts = {row["round"]: row["requests"] for row in measured["baseline"]}
                    candidate_counts = {row["round"]: row["requests"] for row in rows if row["mode"] == "disabled"}
                    if baseline_counts != candidate_counts:
                        raise ValueError("baseline and candidate round request counts differ")
                runs.append({"process": index, "rows": rows,
                             "execution_order": [label for label, _ in order],
                             "baseline_rows": measured.get("baseline")})
                print(f"Completed process {index + 1}/{processes}", file=sys.stderr, flush=True)
        finally:
            server.shutdown()
            worker.join(timeout=5)
    analysis = analyze_report({"runs": runs, "baseline_executable": str(baseline) if baseline else None})
    return {"platform": platform.platform(), "executable": str(executable),
            "executable_sha256": digest, "runs": runs,
            "baseline_executable": str(baseline) if baseline else None,
            "baseline_sha256": baseline_digest,
            "client_cpu": cpu,
            "affinity_scope": "client event-loop thread only; OS binding acknowledged before warmup; peer and other threads unpinned" if cpu is not None else "no CPU affinity requested",
            "analysis_unit": "fresh client process pair; sequential shared Python peer" if baseline else "one fresh client process; sequential shared Python peer",
            "analysis": analysis}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("executable")
    parser.add_argument("--processes", type=int, default=8)
    parser.add_argument("--baseline", help="Raw-only executable from an independently verified historical build")
    parser.add_argument("--cpu", type=int, help="Bind each client event-loop thread before warmup (Windows/Linux)")
    parser.add_argument("--output", required=True)
    args = parser.parse_args()
    if Path(args.output).exists():
        parser.error("output exists; choose a new evidence file")
    report = run(args.executable, args.processes, args.baseline, args.cpu)
    with open(args.output, "x", encoding="utf-8") as destination:
        json.dump(report, destination, indent=2)
        destination.write("\n")
    print(json.dumps(report["analysis"], indent=2))
    return {"within_margin": 0, "regression": 1, "inconclusive": 2}[report["analysis"]["combined_assessment"]]


if __name__ == "__main__":
    raise SystemExit(main())
