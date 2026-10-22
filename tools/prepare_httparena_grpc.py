#!/usr/bin/env python3
"""Inspect pinned official HttpArena inputs and build a plan WITHOUT running load.

Only git inspection, official argv-building functions and docker info are
invoked. This tool never calls benchmark.sh, validate.sh, a client, container
start/build/prune, or host tuning. Run in Linux/WSL with Bash available.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import shutil
import subprocess

OFFICIAL_SHA = "8977be2721a5aba7b320345cddfcd6f6b654e698"
PROFILES = ("unary-grpc", "unary-grpc-tls", "baseline-h2", "static-h2",
            "baseline-h2c", "json-h2c")
SERVER_CPUS = "0-31,64-95"
CLIENT_CPUS = "32-63,96-127"


def cpu_set(text: str) -> set[int]:
    result: set[int] = set()
    for part in text.split(","):
        first, separator, last = part.partition("-")
        result.update(range(int(first), int(last) + 1 if separator else int(first) + 1))
    return result


def read_json(path: Path) -> dict:
    return json.loads(path.read_text(encoding="utf-8"))


def official_argv(root: Path) -> list[dict]:
    # Source the actual pinned functions. Do not reproduce their flags in a
    # separate client or invoke the functions that execute those arguments.
    script = r'''
set -euo pipefail
source "$1/scripts/lib/common.sh"
source "$1/scripts/lib/profiles.sh"
source "$1/scripts/lib/tools/h2load.sh"
shift
for profile in "$@"; do
    parse_profile "${PROFILES[$profile]}"
    IFS=',' read -ra counts <<< "$PROF_CONNS"
    for count in "${counts[@]}"; do
        mapfile -t argv < <(h2load_build_args "$PROF_ENDPOINT" "$count" "$PROF_PIPELINE" "$DURATION")
        printf '%s\0' "$profile" "$PROF_CPU" "$count" "$PROF_ENDPOINT" "${argv[@]}"
        printf '\0'
    done
done
'''
    env = os.environ | {"DURATION": "5s", "RUNS": "3", "H2THREADS": "64",
                        "GCANNON_CPUS": CLIENT_CPUS, "H2LOAD": "h2load", "H2LOAD_CMD": ""}
    output = subprocess.check_output(["bash", "-c", script, "official-argv", str(root), *PROFILES], env=env)
    rows = []
    for record in output.rstrip(b"\0").split(b"\0\0"):
        fields = record.decode().split("\0")
        rows.append({"profile": fields[0], "server_cpuset": fields[1],
                     "connections": int(fields[2]), "endpoint": fields[3],
                     "argv_from_official_builder": fields[4:]})
    return rows


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--arena-root", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--require-official-host", action="store_true",
                        help="exit 2 if official disjoint CPU sets are unavailable")
    args = parser.parse_args()
    root = args.arena_root.resolve()
    revision = subprocess.check_output(["git", "-C", str(root), "rev-parse", "HEAD"], text=True).strip()
    if revision != OFFICIAL_SHA:
        parser.error(f"expected official commit {OFFICIAL_SHA}, found {revision}")
    if subprocess.check_output(["git", "-C", str(root), "diff", "--name-only", "HEAD"], text=True).strip():
        parser.error("official tracked checkout has modifications; record overlays separately")
    rows = official_argv(root)
    available = set(os.sched_getaffinity(0))
    missing = sorted((cpu_set(SERVER_CPUS) | cpu_set(CLIENT_CPUS)) - available)
    inventory = {"scripts/benchmark.sh", "scripts/validate.sh", "scripts/lib/common.sh",
                 "scripts/lib/profiles.sh", "scripts/lib/framework.sh", "scripts/lib/system.sh",
                 "scripts/lib/stats.sh", "scripts/lib/tools/h2load.sh", "docker/h2load.Dockerfile",
                 "requests/grpc-sum.bin", "requests/benchmark.proto", "requests/static-h2-uris.txt",
                 "requests/json-h2c-uris.txt", "data/dataset.json", "certs/server.crt", "certs/generate.sh",
                 "site/content/docs/hardware/_index.md"}
    inventory.update(str(p.relative_to(root)).replace("\\", "/")
                     for p in (root / "data/static").rglob("*") if p.is_file())
    for directory in ("grpc/unary", "h2/baseline-h2", "h2/static-h2", "h2/baseline-h2c", "h2/json-h2c"):
        for name in ("implementation.md", "validation.md"):
            inventory.add(f"site/content/docs/test-profiles/{directory}/{name}")
    rankings: dict[str, dict] = {}
    for profile in PROFILES:
        count = max(row["connections"] for row in rows if row["profile"] == profile)
        tiers: dict[str, list] = {"standard_frameworks": [], "engines": []}
        for result in (root / "site/data/results").glob("*.json"):
            meta_path = root / "frameworks" / result.stem / "meta.json"
            if not meta_path.exists():
                continue  # Do not guess renamed entry/result aliases.
            meta = read_json(meta_path)
            value = read_json(result).get("results", {}).get(f"{profile}-{count}")
            if not value or not meta.get("enabled") or profile not in meta.get("tests", []):
                continue
            tier = ("engines" if meta.get("type") == "engine" else
                    "standard_frameworks" if meta.get("type") in {"flagship", "emerging"}
                    and meta.get("mode") == "standard" else None)
            if tier:
                tiers[tier].append({"entry": result.stem, "published_rps_reference_only": value["rps"],
                                    "result_path": str(result.relative_to(root)), "category": meta.get("type"),
                                    "mode": meta.get("mode")})
        selected = {}
        for tier, candidates in tiers.items():
            candidates.sort(key=lambda value: value["published_rps_reference_only"], reverse=True)
            selected[tier] = candidates[:3]
            for candidate in selected[tier]:
                entry = candidate["entry"]
                inventory.add(candidate["result_path"])
                inventory.update(str(p.relative_to(root)).replace("\\", "/")
                                 for p in (root / "frameworks" / entry).rglob("*") if p.is_file())
        rankings[profile] = {"connections_used_for_selection": count, **selected}
    # grpc-go is a tuned reference and is deliberately kept out of the standard tier.
    inventory.update(str(p.relative_to(root)).replace("\\", "/")
                     for p in (root / "frameworks/grpc-go").rglob("*") if p.is_file())
    files = []
    for relative in sorted(inventory):
        path = root / relative
        if not path.is_file():
            parser.error(f"missing official input: {relative}")
        files.append({"path": relative, "bytes": path.stat().st_size,
                      "sha256": hashlib.sha256(path.read_bytes()).hexdigest()})
    docker = {"status": "unavailable"}
    if shutil.which("docker"):
        result = subprocess.run(["docker", "info", "--format", "{{json .}}"],
                                capture_output=True, text=True, timeout=15)
        if result.returncode == 0:
            info = json.loads(result.stdout)
            docker = {key: info.get(key) for key in ("ServerVersion", "NCPU", "MemTotal", "OSType", "OperatingSystem")}
    plan = {
        "official_repository": "https://github.com/MDA2AV/HttpArena",
        "official_commit": OFFICIAL_SHA, "official_checkout": str(root),
        "execution_status": "PREPARATION_ONLY_NO_BENCHMARK_STARTED",
        "defaults": {"duration": "5s", "runs": 3, "threads": 64, "explicit_warmup": "none in official h2load argv",
                     "cooldown_seconds": 2, "headline": "best official integer RPS of three rounds",
                     "rps": "HTTP 2xx / duration parsed from h2load finished-in line, truncated to integer",
                     "p99_latency": "official field uses request MAX; it is not a measured p99",
                     "cpu_memory": "official Docker stats mean CPU and peak memory; cgroup CPU delta also sampled",
                     "server_cpuset": SERVER_CPUS, "generator_cpuset": CLIENT_CPUS,
                     "container_memory_limit": "not set by framework_start"},
        "host": {"platform": platform.platform(), "effective_cpus": sorted(available), "docker": docker,
                 "cpu_model": next((line.split(":", 1)[1].strip() for line in
                                    Path("/proc/cpuinfo").read_text().splitlines() if line.startswith("model name")), "unknown"),
                 "mem_total_kib": int(next(line.split()[1] for line in
                                           Path("/proc/meminfo").read_text().splitlines() if line.startswith("MemTotal:"))),
                 "competitor_image_digests_and_compiler_flags": "pending real image preparation; not measured"},
        "official_host_gate": {"passed": not missing, "missing_cpu_ids": missing,
                               "fallback_permitted_for_official_claim": False},
        "profiles": rows, "published_leader_selection": rankings,
        "ranking_policy": "Top three enabled, subscribed entries with exact matching result/meta directory at the highest connection count; standard flagship/emerging and engine tiers separate. Unresolved aliases and experimental/tuned rows are not silently mixed.",
        "inputs": files,
        "pending_before_execution": ["explicit performance token", "hardware satisfying both official disjoint CPU sets",
                                     "dedicated Docker daemon/host for official cleanup and system tuning",
                                     "cnetmod official-port container entries and official validate.sh pass",
                                     "immutable image digests, actual compiler/Release flags and <=4 build jobs for every entry",
                                     "separate, clearly labelled true-quantile diagnostics; official statistics unchanged"],
        "prior_local_comparison_eligible_as_official_result": False,
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(plan, indent=2) + "\n", encoding="utf-8")
    print(f"Prepared {len(rows)} official configurations; CPU gate={'PASS' if not missing else 'BLOCKED'}; no benchmark started")
    return 2 if args.require_official_host and missing else 0


if __name__ == "__main__":
    raise SystemExit(main())
