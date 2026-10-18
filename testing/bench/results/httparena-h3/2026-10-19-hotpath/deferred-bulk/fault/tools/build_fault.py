"""Build a dedicated fault fixture using one coherent frozen/current module set.

Run only in an assigned serial compilation window. Does not run networking,
invoke Ninja, reconfigure CMake, or write repository/build inputs. Old private
module interfaces require their matching frozen source tree and all archives.
"""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import shlex
import shutil
import subprocess
import sys


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def snapshot(paths):
    return {str(path): {"sha256": digest(path), "bytes": path.stat().st_size,
                        "mtime_ns": path.stat().st_mtime_ns}
            for path in sorted(set(paths)) if path.is_file()}


def run(command, directory, out, label):
    (out / (label + ".command.json")).write_text(json.dumps(command, indent=2) + "\n")
    with (out / (label + ".log")).open("w") as log:
        result = subprocess.run(command, cwd=directory, stdout=log,
                                stderr=subprocess.STDOUT)
    if result.returncode:
        raise RuntimeError(f"{label} failed ({result.returncode}); see {out / (label + '.log')}")


def expand(arguments, directory):
    expanded = []
    for argument in arguments:
        if argument.startswith("@"):
            response = Path(argument[1:])
            if not response.is_absolute():
                response = directory / response
            expanded.extend(expand(shlex.split(response.read_text()), directory))
        else:
            expanded.append(argument)
    return expanded


def compilation(entry, source, output, directory):
    arguments = expand(entry.get("arguments") or shlex.split(entry["command"]), directory)
    result = []
    index = 0
    while index < len(arguments):
        argument = arguments[index]
        if argument in ("-MF", "-MT", "-MQ", "-MJ", "-serialize-diagnostics", "--serialize-diagnostics"):
            index += 2
            continue
        if argument in ("-MD", "-MMD", "-MP"):
            index += 1
            continue
        if argument.startswith(("-fmodule-output=", "--serialize-diagnostics=")):
            raise RuntimeError("unredirected compiler output: " + argument)
        if argument.startswith("-fmodules-cache-path="):
            argument = "-fmodules-cache-path=" + str(output.parent / "module-cache")
        result.append(argument)
        index += 1
    result[result.index("-c") + 1] = str(source)
    result[result.index("-o") + 1] = str(output)
    return result


def link_command(build, compiler, output, fixture, archive):
    lines = (build / "build.ninja").read_text().splitlines()
    start = next(index for index, line in enumerate(lines)
                 if line.startswith("build bin/quic_retire_stream_server: "))
    explicit = lines[start].split(": ", 1)[1].split(" | ", 1)[0].split()[1:]
    values = {}
    for line in lines[start + 1:]:
        if not line.startswith("  "):
            break
        key, value = line.strip().split(" = ", 1)
        values[key] = value
    if any("$" in values[name] for name in ("FLAGS", "LINK_FLAGS", "LINK_LIBRARIES")):
        raise RuntimeError("unexpected Ninja expansion in link variables")
    objects = []
    for value in explicit:
        objects.append(str(fixture) if value.endswith("quic_retire_stream_server.cpp.o") else value)
    assert objects == [str(fixture)], objects # Current OFF target has exactly its fixture object.
    flags = shlex.split(values["FLAGS"] + " " + values["LINK_FLAGS"])
    flags = ["--dependency-file=" + str(output.parent / "link.d")
             if flag.startswith("--dependency-file=") else flag for flag in flags]
    libraries = shlex.split(values["LINK_LIBRARIES"])
    assert libraries.count("libcnetmod_quic.a") == 1, libraries
    libraries[libraries.index("libcnetmod_quic.a")] = str(archive)
    result = [compiler, *flags, *objects, "-o", str(output),
              "-Wl,-Map=" + str(output.parent / "link.map"), "-Wl,--trace", *libraries]
    archive_arguments = [argument for argument in result
                         if Path(argument).name == "libcnetmod_quic.a"]
    assert archive_arguments == [str(archive)], archive_arguments
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build", type=Path, required=True)
    parser.add_argument("--repo", type=Path, default=Path("/mnt/c/Users/25007/.codex/worktrees/2c3c/cnetmod"))
    parser.add_argument("--frozen-source", type=Path, help="old snapshot's src directory")
    parser.add_argument("--frozen-manifest", type=Path, help="verify old snapshot before use")
    parser.add_argument("--expect-connection-sha", required=True)
    parser.add_argument("--expect-interface-sha", required=True)
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--namespace-ready", action="store_true", help=argparse.SUPPRESS)
    args = parser.parse_args()
    build, repo, out = args.build.resolve(), args.repo.resolve(), args.out.resolve()
    home = Path(__file__).resolve().parent
    entries = json.loads((build / "compile_commands.json").read_text())
    connection_entry = next(entry for entry in entries
                            if Path(entry["file"]).name == "quic_connection.cpp")
    fixture_entry = next(entry for entry in entries
                        if Path(entry["file"]).name == "quic_retire_stream_server.cpp")
    recorded = Path(connection_entry["directory"])
    if Path(fixture_entry["directory"]) != recorded:
        raise RuntimeError("module/fixture build directories differ")
    if not args.namespace_ready:
        if out.exists() or out == build or build in out.parents or out == repo or repo in out.parents:
            parser.error("--out must be a new external directory")
        command = ["unshare", "--mount", "--propagation", "private", sys.executable,
                   str(Path(__file__).resolve()), *sys.argv[1:], "--namespace-ready"]
        raise SystemExit(subprocess.run(command).returncode)
    if args.frozen_manifest:
        manifest = json.loads(args.frozen_manifest.read_text())
        for name, expected in manifest["build"].items():
            assert digest(build / name) == expected, ("frozen build changed", name)
        assert args.frozen_source, "old PCMs require matching old sources"
        for name, expected in manifest["source"].items():
            assert digest(args.frozen_source / name) == expected, ("frozen source changed", name)
    # Retain embedded PCM paths; do not rewrite module identities or disable
    # Clang validation. Bind mounts are private and disappear on process exit.
    recorded.mkdir(parents=True, exist_ok=True)
    subprocess.run(["mount", "--bind", str(build), str(recorded)], check=True)
    subprocess.run(["mount", "-o", "remount,bind,ro", str(recorded)], check=True)
    if args.frozen_source:
        subprocess.run(["mount", "--bind", str(args.frozen_source.resolve()), str(repo / "src")], check=True)
        subprocess.run(["mount", "-o", "remount,bind,ro", str(repo / "src")], check=True)
    assert digest(repo / "src/protocol/quic/quic_connection.cpp") == args.expect_connection_sha.lower()
    assert digest(repo / "src/protocol/quic/quic_connection.cppm") == args.expect_interface_sha.lower()
    build = recorded
    inputs = set(build.rglob("*.pcm")) | set(build.rglob("*.modmap"))
    inputs |= set(build.rglob("*.a")) | set(build.rglob("static.c.o"))
    inputs |= {build / "compile_commands.json", build / "build.ninja"}
    inputs |= {path for path in (repo / "src").rglob("*") if path.is_file()}
    inputs |= {path for path in (repo / "include").rglob("*") if path.is_file()}
    inputs |= {home / "inject_fault.py", home / "quic_packet_failure_server.cpp",
               home / "quic_packet_failure.py", Path(__file__).resolve()}
    before = snapshot(inputs)
    out.mkdir(parents=True)
    (out / "inputs-before.json").write_text(json.dumps(before, indent=2) + "\n")
    try:
        injected = out / "quic_connection.cpp"
        run([sys.executable, str(home / "inject_fault.py"), "--source",
             str(repo / "src/protocol/quic/quic_connection.cpp"), "--out", str(injected)],
            build, out, "inject")
        fixture = out / "quic_packet_failure_server.cpp"
        shutil.copy2(home / fixture.name, fixture)
        connection_object = out / "quic_connection.cpp.o"
        fixture_object = out / "quic_packet_failure_server.cpp.o"
        command = compilation(connection_entry, injected, connection_object, build)
        run(command, build, out, "compile-connection")
        run(compilation(fixture_entry, fixture, fixture_object, build), build, out, "compile-fixture")
        archive = out / "libcnetmod_quic.a"
        shutil.copy2(build / archive.name, archive)
        members = subprocess.check_output(["llvm-ar", "t", str(archive)], text=True).splitlines()
        assert members.count(connection_object.name) == 1, "ambiguous archive member"
        original_member = subprocess.check_output(["llvm-ar", "p", str(archive), connection_object.name])
        run(["llvm-ar", "rcs", str(archive), str(connection_object)], build, out, "replace-member")
        modified_member = subprocess.check_output(["llvm-ar", "p", str(archive), connection_object.name])
        assert hashlib.sha256(modified_member).hexdigest() == digest(connection_object), "replacement member differs"
        assert original_member != modified_member, "fault object did not replace original member"
        (out / "archive-member-proof.json").write_text(json.dumps({
            "archive": str(archive), "member": connection_object.name,
            "original_sha256": hashlib.sha256(original_member).hexdigest(),
            "replacement_sha256": hashlib.sha256(modified_member).hexdigest(),
            "object_sha256": digest(connection_object)}, indent=2) + "\n")
        binary = out / "quic_packet_failure_server"
        run(link_command(build, command[0], binary, fixture_object, archive), build, out, "link")
        trace = (out / "link.log").read_text()
        link_map = (out / "link.map").read_text()
        assert str(archive) in trace, "linker trace omitted the external QUIC archive"
        member_trace = [line for line in trace.splitlines()
                        if str(archive) in line and connection_object.name in line]
        assert member_trace, "linker trace omitted the external connection member"
        # ThinLTO can collapse all input names into synthesized output objects;
        # its map describes output layout, not archive selection. Prove input
        # selection using --trace plus argv uniqueness and extracted-member SHA.
        assert link_map.strip(), "link map is empty"
        (out / "link-proof.json").write_text(json.dumps({"external_quic_archive": str(archive),
            "archive_sha256": digest(archive), "member_sha256": digest(connection_object),
            "link_map_sha256": digest(out / "link.map"), "trace_sha256": digest(out / "link.log"),
            "archive_seen_in_trace": True, "member_trace": member_trace,
            "map_role": "ThinLTO output layout; not archive-selection evidence"}, indent=2) + "\n")
        (out / "outputs.json").write_text(json.dumps(snapshot(out.iterdir()), indent=2) + "\n")
        print(json.dumps({"binary": str(binary), "sha256": digest(binary)}), flush=True)
    finally:
        after = snapshot(inputs)
        (out / "inputs-after.json").write_text(json.dumps(after, indent=2) + "\n")
        changed = [name for name in before.keys() | after.keys() if before.get(name) != after.get(name)]
        (out / "integrity.json").write_text(json.dumps({"unchanged": not changed,
                                                      "changed": changed}, indent=2) + "\n")
        if changed:
            raise RuntimeError("compilation inputs changed: " + repr(changed))


if __name__ == "__main__":
    main()
