"""Prepared single-round frozen-native cold memory diagnostic. Requires --execute.

No builds or repository edits. Only nonce-owned child process groups/container
are cleaned up. GDB reads existing target memory and never calls target code.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import resource
import shutil
import signal
import subprocess
import time
import uuid

FROZEN = "298bf8277e7b1edc32f7e1ab1ce64729a52e8884e97e9486a77a56a20f4cabc4"
IMAGE = "sha256:dd75c50576e08009267603a62a55f01e59b7e79891c6eb81420735a78e28b504"


def sha(path):
    h = hashlib.sha256()
    with Path(path).open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            h.update(block)
    return h.hexdigest()


def save(path, value):
    Path(path).write_text(json.dumps(value, indent=2) + "\n")


def start_time(pid):
    # Fields after the parenthesized comm start at stat field3.
    return Path(f"/proc/{pid}/stat").read_text().rpartition(")")[2].split()[19]


def stop_owned(child, identity):
    if child is None or child.poll() is not None:
        return
    if start_time(child.pid) != identity or os.getpgid(child.pid) != child.pid:
        raise RuntimeError("owned process identity changed; refuse cleanup")
    os.killpg(child.pid, signal.SIGTERM)
    try:
        child.wait(timeout=10)
    except subprocess.TimeoutExpired:
        if start_time(child.pid) != identity:
            raise RuntimeError("PID reuse; refuse kill")
        os.killpg(child.pid, signal.SIGKILL)
        child.wait(timeout=10)


def limits():
    resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
    resource.setrlimit(resource.RLIMIT_FSIZE, (16 * 1024**2, 16 * 1024**2))


def run(args):
    if not args.execute:
        raise ValueError("prepared diagnostic: explicit --execute required")
    if sha(args.binary) != FROZEN:
        raise ValueError("not the approved frozen 298bf native binary")
    root = args.output or Path("/var/tmp/cnetmod-h3-pool-native-" + uuid.uuid4().hex)
    if not re.fullmatch(r"/var/tmp/cnetmod-h3-pool-native-[0-9a-f]{32}", str(root)):
        raise ValueError("output must be a fresh nonce directory under /var/tmp")
    root.mkdir()
    nonce = root.name.rsplit("-", 1)[1]
    anchor = "cnetmod-pool-" + nonce
    binary = root / "arena_h3_server"
    shutil.copy2(args.binary, binary)
    if sha(binary) != FROZEN:
        raise ValueError("copied binary hash mismatch")
    snapshot = args.snapshot.resolve()
    if not snapshot.is_file():
        raise ValueError("snapshot script missing")
    # Freeze the debugger helper too; external concurrent changes cannot alter
    # a later sample silently. GDB reads this exact copied script each time.
    frozen_snapshot = root / "pool-snapshot.py"
    shutil.copy2(snapshot, frozen_snapshot)
    frozen_runner = root / "runner.py"
    shutil.copy2(Path(__file__).resolve(), frozen_runner)
    gai = Path("/etc/gai.conf")
    approved_gai = Path("/var/tmp/cnetmod-h3-official/gai.conf")
    if sha(gai) != sha(approved_gai):
        raise ValueError("use existing cnetmod-h3-isolated.sh mount wrapper; gai.conf mismatch")
    h2load = Path("/var/tmp/cnetmod-h3-loadgen169/h2load")
    inputs = [args.binary, binary, snapshot, frozen_snapshot, frozen_runner, h2load, gai, approved_gai,
              Path("/mnt/c/Users/25007/.codex/tmp/cnetmod-h3-isolated.sh"),
              args.arena / "certs/server.crt", args.arena / "certs/server.key",
              args.arena / "requests/static-h2-uris.txt"]
    inputs += sorted(path for path in (args.arena / "data/static").rglob("*") if path.is_file())
    manifest = {str(path): sha(path) for path in inputs}
    report = dict(scope="one native200k round; debugger pauses only in cold windows; not ranking",
                  valid=False, output=str(root), input_sha256=manifest,
                  snapshots=[], commands=[], nonce=nonce, samples_after_load_seconds=[2, 37])
    server = client = None
    server_identity = client_identity = None
    created = False

    def persist():
        save(root / "summary.json", report)

    def recorded(command, **kwargs):
        report["commands"].append(command)
        persist()
        return subprocess.run(command, **kwargs)

    def snapshot_now(name, load_finished=None):
        if server.poll() is not None or start_time(server.pid) != server_identity:
            raise RuntimeError("server not alive for snapshot")
        folder = root / name
        folder.mkdir()
        started = time.monotonic()
        for item in ("status", "smaps_rollup", "maps", "stat"):
            (folder / ("proc-" + item)).write_text(Path(f"/proc/{server.pid}/{item}").read_text())
        command = ["gdb", "-q", "-nx", "-batch", "-iex", "set auto-load off",
                   "-iex", "set pagination off", "-iex", "set non-stop off",
                   "-iex", "set may-call-functions off", "-se", f"/proc/{server.pid}/exe",
                   "-p", str(server.pid), "-ex", "source " + str(frozen_snapshot),
                   "-ex", "cnetmod-cold-pools", "-ex", "detach"]
        completed = recorded(command, capture_output=True, text=True, timeout=25,
                             start_new_session=True)
        (folder / "gdb.log").write_text(completed.stdout + completed.stderr)
        lines = [line[len("CNETMOD_COLD_SNAPSHOT "):] for line in completed.stdout.splitlines()
                 if line.startswith("CNETMOD_COLD_SNAPSHOT ")]
        if completed.returncode or len(lines) != 1:
            raise RuntimeError("GDB snapshot failed; preserve gdb.log")
        value = json.loads(lines[0])
        save(folder / "snapshot.json", value)
        record = dict(name=name, pid=server.pid, start_wall_time=time.time(),
                      gdb_duration_seconds=time.monotonic() - started,
                      seconds_after_load=None if load_finished is None else started - load_finished,
                      snapshot=str(folder / "snapshot.json"))
        report["snapshots"].append(record)
        if any("unavailable" in value["pools"][key] for key in ("small", "large")):
            raise RuntimeError("pool metadata unavailable")
        if "unavailable" in value["server"] or "unavailable" in value["mimalloc"]:
            raise RuntimeError("server/allocator metadata unavailable")
        # Owned-page bounded partial results are secondary evidence, not a
        # reason to discard the primary pool/root-owner snapshot.
        record["owned_pages_complete"] = value["owned_pages"]["complete_within_declared_scope"]
        persist()

    try:
        print("RESULTS", root, flush=True)
        recorded(["docker", "run", "-d", "--name", anchor, "--label",
                  "codex.cnetmod.pool_nonce=" + nonce, "--network", "none",
                  "--entrypoint", "/bin/sleep", IMAGE, "infinity"],
                 check=True, stdout=subprocess.DEVNULL)
        created = True
        netns_pid = recorded(["docker", "inspect", anchor, "--format", "{{.State.Pid}}"],
                             check=True, capture_output=True, text=True).stdout.strip()
        prefix = ["nsenter", "-t", netns_pid, "-n"]
        for name, command in (
            ("loopback-link.json", prefix + ["ip", "-j", "link", "show", "lo"]),
            ("loopback-address.json", prefix + ["ip", "-j", "address", "show", "lo"]),
            ("localhost-addresses.log", prefix + ["getent", "ahosts", "localhost"])):
            observed = recorded(command, check=True, capture_output=True, text=True)
            (root / name).write_text(observed.stdout)
        if not (root / "localhost-addresses.log").read_text().startswith("127.0.0.1"):
            raise RuntimeError("localhost IPv4 precedence differs from approved native environment")
        server_command = prefix + [str(binary), str(args.arena / "certs/server.crt"),
            str(args.arena / "certs/server.key"), "8443", "32", str(args.arena / "data/static")]
        report["commands"].append(server_command)
        with (root / "server.log").open("wb") as log:
            server = subprocess.Popen(server_command, stdout=log, stderr=subprocess.STDOUT,
                                      start_new_session=True, preexec_fn=limits)
            server_identity = start_time(server.pid)
            report.update(owned_server_pid=server.pid, owned_server_start_time=server_identity)
            persist()
            for _ in range(100):
                ready = subprocess.run(prefix + ["ss", "-H", "-u", "-l", "-n", "sport = :8443"],
                                       check=True, capture_output=True, text=True)
                if len(ready.stdout.splitlines()) == 32:
                    (root / "ready-sockets.log").write_text(ready.stdout)
                    break
                if server.poll() is not None:
                    raise RuntimeError("server exited before sockets were ready")
                time.sleep(0.05)
            else:
                raise RuntimeError("32 worker sockets not ready")
            # Unlike a warm-up GET this does not create a connection before
            # the initial root/pool observation.
            snapshot_now("initial")
            command = prefix + ["taskset", "-c", "16-31", str(h2load), "--alpn-list=h3",
                "-i", str(args.arena / "requests/static-h2-uris.txt"),
                "-H", "Accept-Encoding: br;q=1, gzip;q=0.8", "-c", "64", "-m", "64",
                "-t", "64", "-n", "200000"]
            report["commands"].append(command)
            env = os.environ.copy()
            env.pop("SSLKEYLOGFILE", None)
            # Record only loader settings, not unrelated sensitive environment.
            report["loader_environment"] = {key: env[key] for key in (
                "LD_LIBRARY_PATH", "LD_PRELOAD") if key in env}
            recorded(["/lib64/ld-linux-x86-64.so.2", "--list", str(h2load)],
                     stdout=(root / "loadgen-libraries.log").open("w"), check=True)
            library_paths = re.findall(r"=> (/[^ ]+)", (root / "loadgen-libraries.log").read_text())
            report["loadgen_library_sha256"] = {path: sha(path) for path in library_paths}
            with (root / "h2load.stdout").open("wb") as stdout, (root / "h2load.stderr").open("wb") as stderr:
                client = subprocess.Popen(command, stdout=stdout, stderr=stderr, env=env,
                                          start_new_session=True, preexec_fn=limits)
                client_identity = start_time(client.pid)
                persist()
                code = client.wait(timeout=60)
                load_finished = time.monotonic()
                client = None
            text = (root / "h2load.stdout").read_text() + (root / "h2load.stderr").read_text()
            match = re.search(r"requests: (\d+) total, (\d+) started, (\d+) done, (\d+) succeeded, (\d+) failed, (\d+) errored, (\d+) timeout", text)
            report["load_exit"] = code
            report["request_counts"] = list(map(int, match.groups())) if match else None
            report["http_2xx"] = int(re.search(r"(\d+) 2xx", text)[1]) if re.search(r"(\d+) 2xx", text) else None
            passed = code == 0 and report["request_counts"] == [200000] * 4 + [0] * 3 and report["http_2xx"] == 200000
            for name, delay in (("post-load-idle-2s", 2), ("post-load-idle-37s", 37)):
                time.sleep(max(0, load_finished + delay - time.monotonic()))
                snapshot_now(name, load_finished)
            report["input_sha256_after"] = {path: sha(path) for path in manifest}
            report["inputs_unchanged"] = report["input_sha256_after"] == manifest
            report["valid"] = bool(passed and report["inputs_unchanged"])
            if not report["valid"]:
                raise RuntimeError("native correctness/hash validation failed")
    except BaseException as error:
        report["failure"] = repr(error)
        raise
    finally:
        try:
            stop_owned(client, client_identity)
            stop_owned(server, server_identity)
        finally:
            if created:
                label = subprocess.check_output(["docker", "inspect", anchor, "--format",
                    '{{index .Config.Labels "codex.cnetmod.pool_nonce"}}'], text=True).strip()
                if label != nonce:
                    report["cleanup_failure"] = "container nonce mismatch; not removed"
                else:
                    recorded(["docker", "rm", "-f", anchor], check=True, stdout=subprocess.DEVNULL)
            persist()
            print("RESULTS", root, "VALID", report["valid"], flush=True)


if __name__ == "__main__":
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--execute", action="store_true")
    p.add_argument("--output", type=Path)
    p.add_argument("--binary", type=Path, default=Path(
        "/var/tmp/cnetmod-h3-handshake-retention-1792320533/arena_h3_server"))
    p.add_argument("--arena", type=Path, default=Path(
        "/var/tmp/cnetmod-h3-lifecycle-e1c6da60-1792307675/arena"))
    p.add_argument("--snapshot", type=Path, default=Path(
        "/mnt/c/Users/25007/.codex/tmp/cnetmod-h3-pool-snapshot.py"))
    run(p.parse_args())
