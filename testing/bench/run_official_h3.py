#!/usr/bin/env python3
"""Run the pinned HttpArena driver with task-scoped cleanup and native h2load.

Workload generation, profile constants, CPU fallback, timing, stats and winner
selection remain in the official scripts. No load generator is implemented here.
"""

import argparse
import difflib
import json
import hashlib
import os
from pathlib import Path
import shutil
import shlex
import subprocess

OFFICIAL_SHA = "8977be2721a5aba7b320345cddfcd6f6b654e698"


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--official", type=Path, required=True)
    parser.add_argument("--stage", type=Path, required=True)
    parser.add_argument("--h2load", type=Path, required=True)
    parser.add_argument("--images", type=Path, required=True,
                        help="JSON: entry name -> {image, tests}; exact image IDs recommended")
    parser.add_argument("--prepare-only", action="store_true")
    parser.add_argument("--validation-image", default="cnetmod-h3-official-loadgen")
    args = parser.parse_args()
    sha = subprocess.check_output(["git", "-C", str(args.official), "rev-parse", "HEAD"], text=True).strip()
    if sha != OFFICIAL_SHA:
        raise SystemExit(f"Expected {OFFICIAL_SHA}, got {sha}")
    subprocess.run(["git", "-C", str(args.official), "diff", "--exit-code", sha,
                    "--", "scripts", "requests", "data/static", "data/dataset.json"], check=True)
    stage = args.stage.resolve()
    stage.mkdir(parents=True, exist_ok=True)
    records = stage / "records"
    records.mkdir(exist_ok=True)
    shutil.copytree(args.official / "scripts", stage / "scripts", dirs_exist_ok=True)
    for name in ("data", "requests", "certs"):
        if not (stage / name).exists():
            (stage / name).symlink_to(args.official / name, target_is_directory=True)

    # These affect shared Docker state, not benchmark semantics. The official
    # framework_stop still stops/removes each container owned by this run.
    path = stage / "scripts/benchmark.sh"
    original = path.read_text()
    excluded = ("docker volume prune", "docker image prune", "docker image prune ",
                'docker ps -q  --filter "name=httparena-"',
                'docker ps -aq --filter "name=httparena-"')
    updated = "".join(line for line in original.splitlines(keepends=True)
                      if not any(token in line for token in excluded))
    path.write_text(updated)
    changes = list(difflib.unified_diff(original.splitlines(True), updated.splitlines(True),
                                      fromfile="official/scripts/benchmark.sh",
                                      tofile="staged/scripts/benchmark.sh"))
    # A QUIC-only fixture has no TCP readiness listener. Probe the actual H3
    # endpoint for every entry, without changing the measured argv.
    path = stage / "scripts/lib/framework.sh"
    original = path.read_text()
    before = 'h3|static-h3)\n            probe_url="https://localhost:$H2PORT/baseline2?a=1&b=1"'
    if before not in original:
        raise SystemExit("Official readiness layout changed")
    updated = original.replace(before, before + '\n            probe_extra+=(--http3-only)', 1)
    path.write_text(updated)
    changes.extend(difflib.unified_diff(original.splitlines(True), updated.splitlines(True),
                                       fromfile="official/scripts/lib/framework.sh",
                                       tofile="staged/scripts/lib/framework.sh"))
    # The official validator also publishes unrelated HTTP/1 ports. Isolate
    # correctness-only port mappings from other local development servers;
    # benchmark.sh's measured port remains the official 8443.
    path = stage / "scripts/validate.sh"
    original = path.read_text()
    updated = original
    for before, after in (("PORT=8080\n", "PORT=18080\n"),
                          ("H2PORT=8443\n", "H2PORT=48443\n"),
                          ("H1TLS_PORT=8081\n", "H1TLS_PORT=18081\n"),
                          ("H2C_PORT=8082\n", "H2C_PORT=18082\n")):
        updated = updated.replace(before, after, 1)
    # With pipefail, grep -q can close Docker stdout early and turn a valid
    # readiness response into SIGPIPE. Consume the complete output instead.
    updated = updated.replace("| grep -q ' 1 2xx'; then",
                              "| grep ' 1 2xx' >/dev/null; then")
    path.write_text(updated)
    changes.extend(difflib.unified_diff(original.splitlines(True), updated.splitlines(True),
                                       fromfile="official/scripts/validate.sh",
                                       tofile="staged/scripts/validate.sh"))
    (records / "official-driver.patch").write_text("".join(changes))

    images = json.loads(args.images.read_text())
    for name, entry in images.items():
        directory = stage / "frameworks" / name
        directory.mkdir(parents=True, exist_ok=True)
        (directory / "meta.json").write_text(json.dumps({
            "display_name": name, "language": entry.get("language", ""),
            "tests": entry["tests"], "enabled": True}, indent=2))
        # Tag a previously built, recorded image; never rebuild during timing.
        script = directory / "build.sh"
        script.write_text("#!/bin/sh\nset -eu\ndocker tag " + shlex.quote(entry["image"]) +
                          " " + shlex.quote("httparena-" + name) + "\n")
        script.chmod(0o755)
    shutil.copyfile(args.images, records / "images.json")
    if args.prepare_only:
        return
    env = dict(os.environ, SKIP_TUNE="true", H2LOAD_H3=str(args.h2load.resolve()),
               TMPDIR=str(stage / "tmp"), DURATION="5s", RUNS="3", H3THREADS="64")
    Path(env["TMPDIR"]).mkdir(exist_ok=True)
    env["H2LOAD_H3_IMAGE"] = args.validation_image
    (records / "invocations.json").write_text(json.dumps({
        "official_commit": sha, "cwd": str(stage),
        "env": {k: env[k] for k in ("SKIP_TUNE", "H2LOAD_H3", "TMPDIR", "DURATION", "RUNS", "H3THREADS")},
        "selection": "official best valid-2xx RPS of three; no extra warmup",
        "system_tuning": "disabled on this shared WSL host using official SKIP_TUNE option"}, indent=2))
    fixtures = {str(p.relative_to(stage / "data")): hashlib.sha256(p.read_bytes()).hexdigest()
                for p in (stage / "data").rglob("*") if p.is_file()}
    (records / "dataset-sha256.json").write_text(json.dumps(fixtures, indent=2))
    # The official scripts use shared httparena container names for cleanup.
    # Refuse to run while any such container already belongs to another run.
    active = subprocess.check_output(["docker", "ps", "-a", "--filter", "name=httparena-",
                                      "--format", "{{.Names}}"], text=True).strip()
    processes = subprocess.run(["pgrep", "-x", "h2load|gcannon|wrk|zrk|ghz|oha"],
                               capture_output=True, text=True)
    listeners = subprocess.check_output(["ss", "-H", "-lun", "sport = :8443"], text=True).strip()
    if active or processes.returncode == 0 or listeners:
        raise RuntimeError("Another HttpArena run or UDP/8443 listener exists; wait for the test token")
    # Run the official correctness script on H3-only subscription metadata,
    # so unrelated database or HTTP/1 profiles cannot be mistaken for H3 work.
    for name in images:
        print("Validating", name, flush=True)
        with (records / f"{name}-official-validation.log").open("wb") as log:
            subprocess.run(["bash", str(stage / "scripts/validate.sh"), name],
                           cwd=stage, env=env, stdout=log, stderr=subprocess.STDOUT,
                           check=True, timeout=180)
    # Exactly one benchmark driver at a time, each with three serial runs.
    for name, entry in images.items():
        for profile in entry["tests"]:
            command = ["bash", str(stage / "scripts/benchmark.sh"), name, profile, "--save"]
            print("Running", name, profile, flush=True)
            with (records / f"{name}-{profile}.log").open("wb") as log:
                subprocess.run(command, cwd=stage, env=env, stdout=log,
                               stderr=subprocess.STDOUT, check=True, timeout=240)


if __name__ == "__main__":
    main()
