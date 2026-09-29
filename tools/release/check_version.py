#!/usr/bin/env python3

import json
import pathlib
import re
import sys


ROOT = pathlib.Path(__file__).resolve().parents[2]


def fail(message: str) -> None:
    print(f"version contract failed: {message}", file=sys.stderr)
    raise SystemExit(1)


version = (ROOT / "VERSION").read_text(encoding="utf-8").strip()
if not re.fullmatch(r"\d+\.\d+\.\d+", version):
    fail(f"VERSION is not semantic: {version!r}")

vcpkg = json.loads((ROOT / "vcpkg.json").read_text(encoding="utf-8"))
if vcpkg.get("version") != version:
    fail(f"vcpkg.json has {vcpkg.get('version')!r}, expected {version!r}")

cargo_toml = (ROOT / "bindings/rust/cnetmod/Cargo.toml").read_text(
    encoding="utf-8"
)
cargo_package = cargo_toml.split("[package]", 1)
cargo_version = None
if len(cargo_package) == 2:
    package_body = cargo_package[1].split("\n[", 1)[0]
    match = re.search(r'^version\s*=\s*"([^"]+)"\s*$', package_body, re.MULTILINE)
    if match:
        cargo_version = match.group(1)
if cargo_version != version:
    fail("Rust package version does not match VERSION")

python_test = (ROOT / "bindings/python/test_cnetmod_native.py").read_text(
    encoding="utf-8"
)
if f'module.version() == "{version}"' not in python_test:
    fail("Python binding contract does not match VERSION")

cmake = (ROOT / "CMakeLists.txt").read_text(encoding="utf-8")
if 'VERSION "${CNETMOD_VERSION}"' not in cmake:
    fail("CMake project version is not sourced from VERSION")

conan = (ROOT / "conanfile.py").read_text(encoding="utf-8")
if "def set_version(self):" not in conan or '"VERSION"' not in conan:
    fail("Conan recipe version is not sourced from VERSION")

print(f"cnetmod version contract is consistent: {version}")
