#!/usr/bin/env python3
"""Stage the pinned HttpArena gRPC driver with cleanup scoped to this task.

The official workload builder, client, CPU fallback, timing, sampling and
statistics remain unchanged. Run only while holding the shared test token.
"""
import argparse
import difflib
import json
import os
from pathlib import Path
import shlex
import shutil
import subprocess
from grpc_quiet_host import QuietHost

SHA = "8977be2721a5aba7b320345cddfcd6f6b654e698"
PROFILES = ["unary-grpc", "unary-grpc-tls"]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--official", type=Path, required=True)
    parser.add_argument("--stage", type=Path, required=True)
    parser.add_argument("--images", type=Path, required=True)
    parser.add_argument("--client-image", required=True)
    parser.add_argument("--prepare-only", action="store_true")
    parser.add_argument("--skip-validation", action="store_true",
                        help="Only after separately recorded official validation")
    parser.add_argument("--validation-host-network", action="store_true",
                        help="Use measured host topology so localhost sidecars remain reachable")
    parser.add_argument("--duration", choices=("5s", "30s"), default="5s")
    parser.add_argument("--high-only", action="store_true",
                        help="Supplementary 30-second stability pass at 1024 connections only")
    args = parser.parse_args()
    if args.high_only and args.duration != "30s":
        parser.error("--high-only is supplementary; the headline must retain all official cases")
    actual = subprocess.check_output(
        ["git", "-C", str(args.official), "rev-parse", "HEAD"], text=True).strip()
    if actual != SHA:
        raise SystemExit(f"Expected {SHA}, got {actual}")
    subprocess.run(["git", "-C", str(args.official), "diff", "--exit-code", SHA,
                    "--", "scripts", "requests", "data", "certs"], check=True)
    stage = args.stage.resolve()
    stage.mkdir(parents=True, exist_ok=True)
    records = stage / "records"
    records.mkdir(exist_ok=True)
    shutil.copytree(args.official / "scripts", stage / "scripts", dirs_exist_ok=True)
    for name in ("data", "requests", "certs"):
        if not (stage / name).exists():
            (stage / name).symlink_to(args.official / name, target_is_directory=True)
    path = stage / "scripts/benchmark.sh"
    original = path.read_text()
    # The official driver otherwise stops other tasks' containers and prunes
    # shared images. Per-entry framework_stop remains completely unchanged.
    excluded = ("docker volume prune", "docker image prune",
                'docker ps -q  --filter "name=httparena-"',
                'docker ps -aq --filter "name=httparena-"')
    updated = "".join(line for line in original.splitlines(keepends=True)
                      if not any(token in line for token in excluded))
    if args.high_only:
        marker = "# Iterate profiles × conns."
        if marker not in updated:
            raise ValueError("Official iteration layout changed")
        updated = updated[:updated.index(marker)] + '''# Supplementary high-connection selection only.
declare -A BEST_M
for profile in "${profiles_to_run[@]}"; do
    parse_profile "${PROFILES[$profile]}"
    CONNS=1024
    run_one "$profile" "$CONNS"
done
'''
    path.write_text(updated)
    changes = list(difflib.unified_diff(
        original.splitlines(True), updated.splitlines(True),
        fromfile="official/scripts/benchmark.sh", tofile="staged/scripts/benchmark.sh"))
    path = stage / "scripts/validate.sh"
    original = path.read_text()
    updated = original.replace('PG_CONTAINER="httparena-validate-postgres"',
                               'PG_CONTAINER="httparena-validate-cnetmod-grpc-postgres"')
    updated = updated.replace('PG_NETWORK="httparena-validate-net"',
                              'PG_NETWORK="httparena-validate-cnetmod-grpc-net"')
    if args.validation_host_network:
        if updated.count("docker_args=(") != 1:
            raise ValueError("Official validator container layout changed")
        updated = updated.replace("docker_args=(", "docker_args=(--network host ", 1)
    path.write_text(updated)
    changes.extend(difflib.unified_diff(original.splitlines(True), updated.splitlines(True),
                   fromfile="official/scripts/validate.sh", tofile="staged/scripts/validate.sh"))
    for relative, before, after in (
            ("scripts/lib/common.sh", 'PG_CONTAINER="httparena-postgres"',
             'PG_CONTAINER="httparena-cnetmod-grpc-postgres"'),
            ("scripts/lib/redis.sh", 'REDIS_CONTAINER="httparena-redis"',
             'REDIS_CONTAINER="httparena-cnetmod-grpc-redis"')):
        path = stage / relative
        original = path.read_text()
        if original.count(before) != 1:
            raise ValueError(f"Official sidecar layout changed: {relative}")
        updated = original.replace(before, after, 1)
        path.write_text(updated)
        changes.extend(difflib.unified_diff(original.splitlines(True), updated.splitlines(True),
                       fromfile="official/" + relative, tofile="staged/" + relative))
    (records / "official-driver.patch").write_text("".join(changes))
    images = json.loads(args.images.read_text())
    for name, image in images.items():
        image["image"] = subprocess.check_output(
            ["docker", "image", "inspect", "--format", "{{.Id}}", image["image"]], text=True).strip()
        if not name.startswith("cnetmod-grpc-"):
            raise SystemExit("Entry names must be task scoped: cnetmod-grpc-*")
        entry = stage / "frameworks" / name
        entry.mkdir(parents=True, exist_ok=True)
        (entry / "meta.json").write_text(json.dumps({
            "display_name": name, "language": image.get("language", "C++"), "enabled": True,
            "type": image.get("type", "framework"), "tier": image.get("tier", "flagship"),
            "tests": image.get("tests", PROFILES)}, indent=2))
        build = entry / "build.sh"
        build.write_text("#!/bin/sh\nset -eu\ndocker tag " +
                         shlex.quote(image["image"]) + " " +
                         shlex.quote("httparena-" + name) + "\n")
        build.chmod(0o755)
    (records / "images.json").write_text(json.dumps(images, indent=2))
    args.client_image = subprocess.check_output(
        ["docker", "image", "inspect", "--format", "{{.Id}}", args.client_image], text=True).strip()
    env = dict(os.environ, SKIP_TUNE="true", LOADGEN_DOCKER="true",
               H2LOAD_IMAGE=args.client_image, DURATION=args.duration, RUNS="3",
               H2THREADS="64", NO_PROXY="localhost,127.0.0.1,::1",
               no_proxy="localhost,127.0.0.1,::1")
    env.pop("GCANNON_CPUS", None)  # Resolve the pinned driver's default CPU fallback.
    # The official startup checks all tool images even for gRPC-only profiles.
    # Reuse an existing image for unused tools so they aren't needlessly built.
    for key in ("GCANNON_IMAGE", "H2LOAD_H3_IMAGE", "WRK_IMAGE", "ZRK_IMAGE"):
        env[key] = args.client_image
    (records / "invocation.json").write_text(json.dumps({
        "official_sha": SHA, "host_mode": "official small-host fallback; local comparison",
        "supplementary_high_only": args.high_only,
        "environment": {k: env[k] for k in (
            "SKIP_TUNE", "LOADGEN_DOCKER", "H2LOAD_IMAGE", "H2THREADS", "RUNS", "DURATION")},
        "headline": "official best of three successful-2xx / measured duration",
        "p99_latency": "official field is maximum, not a measured percentile",
        "global_system_tuning": "disabled; host settings retained for all entries"}, indent=2))
    if args.prepare_only:
        return
    active = subprocess.check_output(["docker", "ps", "--filter", "name=httparena-",
                                      "--format", "{{.Names}}"], text=True).strip()
    if active:
        raise SystemExit(f"Another benchmark is active: {active}")
    with QuietHost(records / "quiet-host.json"):
        for name, image in images.items():
            if not args.skip_validation:
                with (records / f"{name}-validation.log").open("wb") as log:
                    subprocess.run(["bash", "scripts/validate.sh", name], cwd=stage,
                                   env=env, stdout=log, stderr=subprocess.STDOUT,
                                   check=True, timeout=300)
            for profile in image.get("tests", PROFILES):
                print(name, profile, flush=True)
                with (records / f"{name}-{profile}.log").open("wb") as log:
                    subprocess.run(["bash", "scripts/benchmark.sh", name, profile, "--save"],
                                   cwd=stage, env=env, stdout=log,
                                   stderr=subprocess.STDOUT, check=True, timeout=300)


if __name__ == "__main__":
    main()
