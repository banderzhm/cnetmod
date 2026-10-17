#!/usr/bin/env python3
"""Read-only official WS inventory/ranking. Never starts builds or benchmarks."""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import subprocess
import sys


ARENA_REVISION = "8977be2721a5aba7b320345cddfcd6f6b654e698"
PROFILES = ("echo-ws", "echo-ws-pipeline", "echo-ws-limited")
SOURCES = (
    "scripts/benchmark.sh", "scripts/validate.sh", "scripts/validate-ws.py",
    "scripts/lib/common.sh", "scripts/lib/profiles.sh", "scripts/lib/framework.sh",
    "scripts/lib/stats.sh", "scripts/lib/system.sh", "scripts/lib/tools/gcannon.sh",
    "scripts/gen_leaderboard_data.py", "docker/gcannon.Dockerfile",
    "site/data/current.json", "site/data/frameworks.json",
    "site/content/docs/test-profiles/ws/echo/implementation.md",
    "site/content/docs/test-profiles/ws/echo-pipeline/implementation.md",
    "site/content/docs/test-profiles/ws/echo-limited/implementation.md",
)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--arena", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    arena = args.arena.resolve()
    sha = subprocess.check_output(["git", "-C", str(arena), "rev-parse", "HEAD"], text=True).strip()
    if sha != ARENA_REVISION:
        parser.error(f"expected official snapshot {ARENA_REVISION}, found {sha}")
    # Do not scan unrelated multi-gigabyte HTTP datasets on an NTFS checkout.
    dirty = subprocess.check_output(["git", "-C", str(arena), "diff", "--name-only", "HEAD", "--",
                                     "scripts", "docker/gcannon.Dockerfile", "site/data/current.json",
                                     "site/data/frameworks.json", "site/data/results",
                                     "site/content/docs/test-profiles/ws"], text=True, timeout=60).splitlines()
    if dirty:
        parser.error("official tracked files are modified: " + ", ".join(dirty))
    sys.path.insert(0, str(arena / "scripts"))
    spec = importlib.util.spec_from_file_location("ws_official_board", arena / "scripts/gen_leaderboard_data.py")
    board = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(board)
    board.RESULTS = board.load_results()
    metadata = board.load("frameworks.json")
    profiles, results = [], {}
    for category, entries in board.CATALOG:
        for pid, label, blurb, explorer, scored, standard, engine, infra in entries:
            if pid not in PROFILES:
                continue
            present = []
            for connections in explorer:
                key = f"{pid}-{connections}"
                rows = board.RESULTS.get(key, [])
                if rows:
                    present.append(connections)
                    results[key] = [dict(row, fw=row["framework"]) for row in rows]
            profiles.append(dict(id=pid, label=label, category=category, conns=present,
                                 scoredConns=[count for count in scored if count in present],
                                 scored=standard, engineScored=engine, infraScored=infra))
    aggregate = board.badge_aggregate(profiles, results)
    rankings = {}
    selected = []
    result_files = []
    registrations = [(path.parent.name, json.loads(path.read_text()))
                     for path in sorted((arena / "frameworks").glob("*/meta.json"))]
    for league, types in {"frameworks": ("flagship", "emerging"), "engines": ("engine",)}.items():
        ranks = board.badge_composite(aggregate, profiles, metadata, "ws", types, show_tuned=False)
        rankings[league] = []
        for position, (name, score) in enumerate(ranks[:3], 1):
            # Display names are shared by protocol-specific entries (wtx,
            # zix). Resolve to the actual WS registration, not the last
            # directory that happened to win the website metadata map.
            candidates = [directory for directory, entry in registrations
                          if entry.get("display_name", directory) == name
                          and entry.get("enabled", True)
                          and all(profile in entry.get("tests", []) for profile in PROFILES)]
            if len(candidates) != 1:
                parser.error(f"ambiguous/missing WS registration for {name}: {candidates}")
            directory = candidates[0]
            rankings[league].append(dict(rank=position, name=name, directory=directory, score=score,
                                          type=metadata[name]["type"]))
            if directory not in selected:
                selected.append(directory)
            for result_file in sorted((arena / "site/data/results").glob("*.json")):
                if json.loads(result_file.read_text()).get("framework") == name:
                    result_files.append(str(result_file.relative_to(arena)))
    files = list(SOURCES)
    for directory in selected:
        changed = subprocess.check_output(["git", "-C", str(arena), "diff", "--name-only", "HEAD", "--",
                                           f"frameworks/{directory}"], text=True, timeout=60).strip()
        if changed:
            parser.error("selected official peer is modified: " + changed)
        files.extend(str(path.relative_to(arena)).replace("\\", "/")
                     for path in sorted((arena / "frameworks" / directory).rglob("*"))
                     if path.is_file())
    files.extend(sorted(set(result_files)))
    manifest = {
        "official_revision": sha,
        "purpose": "Preparation only; no official benchmark or peer build has run",
        "planned_gcannon_revision": "11c802b2bdfe9af2fac4ac2571ae2de4c530cc62",
        "profiles": profiles,
        "workload": {"endpoint": "http://localhost:8080/ws", "payload": "hello",
                     "tls": False, "quic": False, "compression": False,
                     "threads": 64, "duration": "5s", "runs": 3,
                     "selection": "best received-frame throughput of three, with all rounds retained",
                     "server_cpu_set": "0-31,64-95 (official fallback on smaller hosts)",
                     "warmup": "gcannon implementation; freeze source/image before running"},
        "ranking": rankings,
        "sha256": {name: hashlib.sha256((arena / name).read_bytes()).hexdigest() for name in files},
        "commands_after_token": {
            directory: [["bash", "scripts/validate.sh", directory]] +
                       [["bash", "scripts/benchmark.sh", directory, profile, "--save"] for profile in PROFILES]
            for directory in ["cnetmod-ws-baseline", "cnetmod-ws-candidate"] + selected},
    }
    with args.output.open("x", encoding="utf-8") as output:
        json.dump(manifest, output, indent=2, ensure_ascii=False)
        output.write("\n")
    print(json.dumps({"revision": sha, "ranking": rankings, "hashed_files": len(files)}, indent=2))


if __name__ == "__main__":
    main()
