"""Serial Windows cnetmod interoperability: native curl and two WSL/Linux peers.

Prepared only; running this file performs network interoperability tests.
Every run preserves its own logs, PID records and cleanup audit directory.
"""

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import socket
import subprocess
import sys
import time
import uuid


PINNED_CURL_SHA256 = "b2ad02d8334b4a24783cb08f615236f1572ee92edc08394baf775e509721dc81"
LINUX_START = r"""
import json, os, sys
pidfile, nonce, executable, *arguments = sys.argv[1:]
if os.getpgrp() == os.getpid() and os.getsid(0) != os.getpid():
    child = os.fork()
    if child:
        _, status = os.waitpid(child, 0)
        os._exit(os.waitstatus_to_exitcode(status))
if os.getsid(0) != os.getpid():
    os.setsid()
record = dict(pid=os.getpid(), pgid=os.getpgrp(), sid=os.getsid(0), nonce=nonce,
              executable=os.path.realpath(executable),
              launch_executable=os.readlink('/proc/self/exe'))
with open(pidfile + '.new', 'x', encoding='utf-8') as output:
    json.dump(record, output)
os.replace(pidfile + '.new', pidfile)
os.environ['CNETMOD_H3_PEER_RUN'] = nonce
os.execv(executable, [executable, *arguments])
"""

LINUX_CLEANUP = r"""
import json, os, signal, sys, time
record = json.loads(sys.argv[1])
pid, nonce = record['pid'], record['nonce']
assert pid > 1 and record['pgid'] == pid and record['sid'] == pid
allowed = {record['executable'], record['launch_executable']}
def members():
    result = []
    for item in os.scandir('/proc'):
        if not item.name.isdigit():
            continue
        current = int(item.name)
        try:
            if os.getpgid(current) != pid or os.getsid(current) != pid:
                continue
            state = open(f'/proc/{current}/stat').read().rsplit(')', 1)[1].split()[0]
            if state == 'Z':
                continue
            executable = os.readlink(f'/proc/{current}/exe')
            args = open(f'/proc/{current}/cmdline', 'rb').read().split(b'\0')
            environment = open(f'/proc/{current}/environ', 'rb').read().split(b'\0')
            tagged = ('CNETMOD_H3_PEER_RUN=' + nonce).encode() in environment
            launching = nonce.encode() in args and record['executable'].encode() in args
            if executable not in allowed or not (tagged or launching):
                raise RuntimeError(f'refusing unowned PID/group {current}: {executable}')
            result.append(dict(pid=current, executable=executable))
        except (FileNotFoundError, ProcessLookupError):
            continue
    return result
before = members()
actions = []
for sig, budget in ((signal.SIGTERM, 3.0), (signal.SIGKILL, 2.0)):
    active = members()
    if not active:
        break
    # Revalidate every member's exact executable, unique run tag and group
    # before signalling. Never kill by process name or a global port search.
    try:
        os.killpg(pid, sig)
    except ProcessLookupError:
        pass
    actions.append(sig.name)
    deadline = time.monotonic() + budget
    while time.monotonic() < deadline and members():
        time.sleep(0.02)
after = members()
print(json.dumps(dict(before=before, actions=actions, after=after)))
if after:
    raise RuntimeError('owned Linux process group remained alive after cleanup')
"""


def sha256(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def reserve_port():
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as probe:
        probe.bind(("127.0.0.1", 0))
        return probe.getsockname()[1]


class Driver:
    def __init__(self, args, root):
        self.args, self.root = args, root
        self.processes = []
        self.cleanup_errors = []

    def wsl(self, arguments):
        return ["C:/Windows/System32/wsl.exe", "--distribution", self.args.distro,
                "--user", "root", "--exec", *arguments]

    def linux_path(self, path):
        result = subprocess.run(self.wsl(["/usr/bin/wslpath", "-u", str(Path(path).resolve())]),
                                capture_output=True, text=True, timeout=10, check=True)
        return result.stdout.strip()

    def start(self, name, command, directory, linux=None):
        stdout = (directory / (name + ".stdout.log")).open("w", encoding="utf-8")
        stderr = (directory / (name + ".stderr.log")).open("w", encoding="utf-8")
        try:
            process = subprocess.Popen(command, cwd=directory, stdout=stdout, stderr=stderr,
                creationflags=subprocess.CREATE_NO_WINDOW | subprocess.CREATE_NEW_PROCESS_GROUP)
        except BaseException:
            stdout.close()
            stderr.close()
            raise
        entry = dict(name=name, process=process, stdout=stdout, stderr=stderr,
                     directory=directory, linux=linux, stopped=False)
        self.processes.append(entry)
        (directory / (name + ".windows-launch.json")).write_text(json.dumps(
            dict(pid=process.pid, command=command, cwd=str(directory),
                 target_platform="WSL/Linux" if linux else "Windows"), indent=2), encoding="utf-8")
        return entry

    def start_linux(self, name, executable, arguments, directory):
        nonce = uuid.uuid4().hex
        pidfile = directory / (name + ".linux-pid.json")
        command = self.wsl([self.args.wsl_python, "-c", LINUX_START,
                            self.linux_path(pidfile), nonce, executable, *arguments])
        return self.start(name, command, directory,
                          dict(pidfile=pidfile, nonce=nonce, executable=executable))

    def wait_started(self, entry):
        deadline = time.monotonic() + 10
        if entry["linux"]:
            while not entry["linux"]["pidfile"].is_file():
                if entry["process"].poll() is not None or time.monotonic() >= deadline:
                    raise RuntimeError("Linux peer failed before publishing its PID record")
                time.sleep(0.02)
        time.sleep(1)
        if entry["process"].poll() is not None:
            raise RuntimeError("peer exited before the request; see preserved stdout/stderr")

    def cleanup(self, entry):
        if entry["stopped"]:
            return
        try:
            if entry["linux"]:
                identity = entry["linux"]
                # The Linux wrapper publishes identity before exec, so an
                # absent record plus an exited launcher means no peer started.
                deadline = time.monotonic() + 5
                while not identity["pidfile"].is_file() and entry["process"].poll() is None and time.monotonic() < deadline:
                    time.sleep(0.02)
                if identity["pidfile"].is_file():
                    record = json.loads(identity["pidfile"].read_text(encoding="utf-8"))
                    if record["nonce"] != identity["nonce"] or record["executable"] != identity["executable"]:
                        raise RuntimeError("Linux peer identity record mismatch; refusing cancellation")
                    result = subprocess.run(self.wsl([self.args.wsl_python, "-c", LINUX_CLEANUP,
                        json.dumps(record)]), capture_output=True, text=True, timeout=12)
                    (entry["directory"] / (entry["name"] + ".cleanup.log")).write_text(
                        result.stdout + result.stderr, encoding="utf-8")
                    if result.returncode:
                        raise RuntimeError("Linux PID/group cleanup verification failed")
                elif entry["process"].poll() is None:
                    raise RuntimeError("Linux launcher still live without a PID record; cleanup unproven")
            if entry["process"].poll() is None:
                entry["process"].terminate()
            try:
                entry["process"].wait(timeout=5)
            except subprocess.TimeoutExpired:
                entry["process"].kill()
                entry["process"].wait(timeout=5)
            entry["stopped"] = True
        finally:
            entry["stdout"].close()
            entry["stderr"].close()

    def finish(self, entry, timeout=20):
        code = entry["process"].wait(timeout=timeout)
        entry["stdout"].flush()
        entry["stderr"].flush()
        output = (entry["directory"] / (entry["name"] + ".stdout.log")).read_text(encoding="utf-8")
        output += (entry["directory"] / (entry["name"] + ".stderr.log")).read_text(encoding="utf-8")
        if code:
            raise RuntimeError(f"{entry['name']} exited {code}: {output}")
        return output.replace("\r\n", "\n")

    def certificate(self, directory):
        config = directory / "openssl.cnf"
        config.write_text("[req]\ndistinguished_name=dn\nprompt=no\nx509_extensions=ext\n"
                          "[dn]\nCN=localhost\n[ext]\nsubjectAltName=DNS:localhost,IP:127.0.0.1\n", encoding="utf-8")
        entry = self.start("certificate", [str(self.args.openssl), "req", "-x509", "-newkey",
            "rsa:2048", "-nodes", "-days", "1", "-config", str(config), "-keyout",
            str(directory / "key.pem"), "-out", str(directory / "cert.pem")], directory)
        self.finish(entry)
        return directory / "cert.pem", directory / "key.pem"

    def windows_server(self, directory, port):
        cert, key = self.certificate(directory)
        entry = self.start("cnetmod-server", [str(self.args.server), "--port", str(port),
            "--workers", str(self.args.workers), "--cert", str(cert), "--key", str(key)], directory)
        self.wait_started(entry)
        return entry

    def run_case(self, name, platform, action):
        directory = self.root / name
        directory.mkdir()
        before = len(self.processes)
        began = time.monotonic()
        record = dict(name=name, status="failed", cnetmod_platform="Windows",
                      peer_platform=platform, workers=self.args.workers, address="127.0.0.1",
                      detail="", logs=str(directory))
        try:
            action(directory)
            record.update(status="passed", detail="request and response validated")
        except Exception as error:
            record["detail"] = repr(error)
        finally:
            for entry in reversed(self.processes[before:]):
                try:
                    self.cleanup(entry)
                except Exception as error:
                    detail = f"{entry['name']}: {error!r}"
                    self.cleanup_errors.append(detail)
                    record.update(status="failed", detail=record["detail"] + "; cleanup: " + detail)
        record["elapsed_ms"] = int((time.monotonic() - began) * 1000)
        return record

    def curl_case(self, directory):
        port = reserve_port()
        self.windows_server(directory, port)
        entry = self.start("native-curl", [str(self.args.curl), "--noproxy", "*", "--http3-only",
            "--insecure", "--silent", "--show-error", "--fail",
            "--write-out", "%{stderr}\nCNETMOD_HTTP_STATUS:%{http_code}\n",
            f"https://127.0.0.1:{port}/health"], directory)
        self.finish(entry)
        body = (directory / "native-curl.stdout.log").read_text(encoding="utf-8")
        status_output = (directory / "native-curl.stderr.log").read_text(encoding="utf-8")
        statuses = re.findall(r"(?m)^CNETMOD_HTTP_STATUS:([0-9]{3})$", status_output)
        if statuses != ["200"]:
            raise AssertionError(f"unexpected curl HTTP status records: {statuses!r}")
        if body.strip() != "ok":
            raise AssertionError("unexpected curl response body")

    def linux_client_case(self, directory):
        port = reserve_port()
        self.windows_server(directory, port)
        download = directory / "download"
        download.mkdir()
        entry = self.start_linux("linux-ngtcp2-client", self.args.ngtcp2_client,
            ["--timeout=1s", "--no-quic-dump", "--no-http-dump", "--download", self.linux_path(download),
             "--exit-on-first-stream-close", "127.0.0.1",
             str(port), f"https://127.0.0.1:{port}/health"], directory)
        output = self.finish(entry)
        body = download / "health"
        if not body.is_file() or body.read_bytes() != b"ok\n":
            raise AssertionError("ngtcp2 downloaded health body is missing or differs from exact ok\\n")
        if not re.search(r"(?m)^Negotiated ALPN is h3$", output):
            raise AssertionError("ngtcp2 did not report the h3 ALPN")
        statuses = re.findall(r"(?m)^http: stream (0x[0-9a-f]+) \[:status: ([0-9]{3})\](?:\(sensitive\))?$", output)
        if len(statuses) != 1 or statuses[0][1] != "200":
            raise AssertionError(f"ngtcp2 response status differs from one HTTP/3 200: {statuses!r}")
        stream = statuses[0][0]
        if f"http: stream {stream} response headers started\n" not in output or f"http: stream {stream} headers ended\n" not in output:
            raise AssertionError("ngtcp2 response header block is incomplete")

    def linux_server_case(self, directory):
        port = reserve_port()
        cert, key = self.certificate(directory)
        (directory / "health").write_text("ok\n", encoding="utf-8")
        entry = self.start_linux("linux-ngtcp2-server", self.args.ngtcp2_server,
            ["-q", "--htdocs", self.linux_path(directory), "127.0.0.1", str(port),
             self.linux_path(key), self.linux_path(cert)], directory)
        self.wait_started(entry)
        client = self.start("cnetmod-client", [str(self.args.client), "127.0.0.1", str(port), "/health"], directory)
        output = self.finish(client)
        if "Status: 200" not in output or "Body: ok\n" not in output:
            raise AssertionError(f"unexpected cnetmod status/body: {output!r}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--server", type=Path, required=True)
    parser.add_argument("--client", type=Path, required=True)
    parser.add_argument("--workers", type=int, choices=(1, 2), required=True)
    parser.add_argument("--curl", type=Path, default=Path(__file__).parent / "curl-8.23.0_1-win64-mingw/bin/curl.exe")
    parser.add_argument("--openssl", type=Path, default=Path("D:/runtime/miniconda3/Library/bin/openssl.exe"))
    parser.add_argument("--distro", default="archlinux")
    parser.add_argument("--wsl-python", default="/usr/sbin/python3")
    parser.add_argument("--ngtcp2-client", default="/var/tmp/cnetmod-h3-ngtcp2-peer-build/examples/osslclient")
    parser.add_argument("--ngtcp2-server", default="/var/tmp/cnetmod-h3-ngtcp2-peer-build/examples/osslserver")
    parser.add_argument("--output-parent", type=Path, default=Path(__file__).parent)
    args = parser.parse_args()
    if os.name != "nt":
        parser.error("this driver must run with native Windows Python and native Windows cnetmod targets")
    for path in (args.server, args.client, args.curl, args.openssl):
        if not path.is_file():
            parser.error(f"missing Windows executable: {path}")
        with path.open("rb") as binary:
            pe_magic = binary.read(2)
        if pe_magic != b"MZ":
            parser.error(f"missing/non-PE Windows executable: {path}")
    if sha256(args.curl) != PINNED_CURL_SHA256:
        parser.error("native curl executable differs from the pinned official package")
    root = args.output_parent.resolve() / ("run-" + uuid.uuid4().hex)
    root.mkdir(parents=True)
    driver = Driver(args, root)
    results = []
    try:
        version = subprocess.run([str(args.curl), "--version"], capture_output=True, text=True, timeout=5, check=True)
        if "HTTP3" not in version.stdout:
            raise RuntimeError("pinned curl does not advertise HTTP3")
        (root / "curl-version.txt").write_text(version.stdout, encoding="utf-8")
        inputs = {str(path.resolve()): sha256(path) for path in (args.server, args.client, args.curl, args.openssl)}
        linux_inputs = subprocess.run(driver.wsl(["/usr/bin/sha256sum", args.ngtcp2_client, args.ngtcp2_server]),
                                     capture_output=True, text=True, timeout=10, check=True)
        (root / "inputs.json").write_text(json.dumps(dict(windows_sha256=inputs,
            linux_sha256=linux_inputs.stdout, distro=args.distro, networking="mirrored IPv4 loopback; actual request verifies reachability",
            command=sys.argv), indent=2), encoding="utf-8")
        for name, platform, action in (
            ("cnetmod-server_to_curl-http3", "Windows/native", driver.curl_case),
            ("cnetmod-server_to_nghttp3-client", "WSL/Linux", driver.linux_client_case),
            ("nghttp3-server_to_cnetmod-client", "WSL/Linux", driver.linux_server_case),
        ):
            results.append(driver.run_case(name, platform, action))
            (root / "results.json").write_text(json.dumps(results, indent=2), encoding="utf-8")
    finally:
        # Outer lifetime owner retries every operation whose inner cleanup did
        # not establish a stopped state; logs/PID records are never deleted.
        for entry in reversed(driver.processes):
            if entry["stopped"]:
                continue
            try:
                driver.cleanup(entry)
            except Exception as error:
                driver.cleanup_errors.append(f"outer {entry['name']}: {error!r}")
        (root / "cleanup.json").write_text(json.dumps(driver.cleanup_errors, indent=2), encoding="utf-8")
    print(json.dumps(dict(output=str(root), results=results, cleanup_errors=driver.cleanup_errors), indent=2))
    return 0 if len(results) == 3 and all(item["status"] == "passed" for item in results) and not driver.cleanup_errors else 1


if __name__ == "__main__":
    raise SystemExit(main())
