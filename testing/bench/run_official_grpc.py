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
    args = parser.parse_args()
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
    path.write_text(updated)
    changes.extend(difflib.unified_diff(original.splitlines(True), updated.splitlines(True),
                   fromfile="official/scripts/validate.sh", tofile="staged/scripts/validate.sh"))
    (records / "official-driver.patch").write_text("".join(changes))
    images = json.loads(args.images.read_text())
    for name, image in images.items():
        if not name.startswith("cnetmod-grpc-"):
            raise SystemExit("Entry names must be task scoped: cnetmod-grpc-*")
        entry = stage / "frameworks" / name
        entry.mkdir(parents=True, exist_ok=True)
        (entry / "meta.json").write_text(json.dumps({
            "display_name": name, "language": image.get("language", "C++"), "enabled": True,
            "tests": image.get("tests", PROFILES)}, indent=2))
        build = entry / "build.sh"
        build.write_text("#!/bin/sh\nset -eu\ndocker tag " +
                         shlex.quote(image["image"]) + " " +
                         shlex.quote("httparena-" + name) + "\n")
        build.chmod(0o755)
    shutil.copyfile(args.images, records / "images.json")
    env = dict(os.environ, SKIP_TUNE="true", LOADGEN_DOCKER="true",
               H2LOAD_IMAGE=args.client_image, DURATION="5s", RUNS="3",
               H2THREADS="64", NO_PROXY="localhost,127.0.0.1,::1",
               no_proxy="localhost,127.0.0.1,::1")
    # The official startup checks all tool images even for gRPC-only profiles.
    # Reuse an existing image for unused tools so they aren't needlessly built.
    for key in ("GCANNON_IMAGE", "H2LOAD_H3_IMAGE", "WRK_IMAGE", "ZRK_IMAGE"):
        env[key] = args.client_image
    (records / "invocation.json").write_text(json.dumps({
        "official_sha": SHA, "host_mode": "official small-host fallback; local comparison",
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
