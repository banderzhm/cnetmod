#!/usr/bin/env python3

import argparse
import hashlib
import json
import os
import pathlib
import zipfile


def sha256(path: pathlib.Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for block in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def package_manifest(path: pathlib.Path) -> dict:
    with zipfile.ZipFile(path) as archive:
        candidates = [
            name
            for name in archive.namelist()
            if name.endswith("share/cnetmod/cnetmod-package-manifest.json")
        ]
        if len(candidates) != 1:
            raise ValueError(
                f"{path.name} must contain exactly one cnetmod package manifest"
            )
        return json.loads(archive.read(candidates[0]))


parser = argparse.ArgumentParser()
parser.add_argument("--version", required=True)
parser.add_argument("--git-sha", required=True)
parser.add_argument("--output", required=True, type=pathlib.Path)
parser.add_argument("artifacts", nargs="+", type=pathlib.Path)
args = parser.parse_args()

entries = []
targets = set()
files = set()
for artifact in sorted(args.artifacts):
    manifest = package_manifest(artifact)
    if manifest.get("version") != args.version:
        raise ValueError(
            f"{artifact.name} contains version {manifest.get('version')}, expected {args.version}"
        )
    target = manifest["target"]
    if target in targets:
        raise ValueError(f"duplicate release target: {target}")
    if artifact.name in files:
        raise ValueError(f"duplicate release file name: {artifact.name}")
    targets.add(target)
    files.add(artifact.name)
    entries.append(
        {
            "target": target,
            "file": artifact.name,
            "sha256": sha256(artifact),
            "size": artifact.stat().st_size,
            "abi": manifest["abi"],
            "components": manifest["components"],
        }
    )

catalog = {
    "schemaVersion": 1,
    "name": "cnetmod-sdk",
    "version": args.version,
    "gitSha": args.git_sha,
    "artifacts": entries,
}
args.output.write_text(
    json.dumps(catalog, indent=2, sort_keys=False) + os.linesep,
    encoding="utf-8",
)

checksums = args.output.with_name("SHA256SUMS")
lines = [f"{entry['sha256']}  {entry['file']}" for entry in entries]
lines.append(f"{sha256(args.output)}  {args.output.name}")
checksums.write_text("\n".join(lines) + "\n", encoding="utf-8")
