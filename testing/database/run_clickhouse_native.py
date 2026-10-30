#!/usr/bin/env python3
"""Start an isolated loopback ClickHouse server and run Native interoperability tests."""
import argparse
import os
from pathlib import Path
import shutil
import socket
import subprocess
import tempfile
import time
from xml.sax.saxutils import escape


def free_port():
    with socket.socket() as listener:
        listener.bind(("127.0.0.1", 0))
        return listener.getsockname()[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--clickhouse", default=shutil.which("clickhouse"))
    parser.add_argument("--test-dir", type=Path)
    parser.add_argument("--test-executable", type=Path)
    parser.add_argument("--application-executable", type=Path)
    parser.add_argument("--openssl", default=shutil.which("openssl"))
    args = parser.parse_args()
    if not args.clickhouse:
        parser.error("supply --clickhouse /path/to/clickhouse")
    if not args.openssl:
        parser.error("openssl is required to verify Native TLS")
    if not args.test_executable and not args.test_dir:
        parser.error("supply --test-dir or --test-executable")
    executable = (args.test_executable.resolve() if args.test_executable else
                  args.test_dir.resolve() / "testing/tests/test_clickhouse_live")
    if os.name == "nt" and not args.test_executable:
        executable = executable.with_suffix(".exe")
    if not executable.is_file():
        parser.error(f"build test_clickhouse_live first: {executable}")
    with tempfile.TemporaryDirectory(prefix="cnetmod-clickhouse-") as directory:
        root = Path(directory)
        port, tls_port = free_port(), free_port()
        while tls_port == port:
            tls_port = free_port()
        certificate, key = root / "certificate.pem", root / "key.pem"
        subprocess.run([args.openssl, "req", "-x509", "-newkey", "rsa:2048", "-nodes",
                        "-keyout", str(key), "-out", str(certificate), "-days", "1",
                        "-subj", "/CN=cnetmod-clickhouse-test",
                        "-addext", "subjectAltName=IP:127.0.0.1"],
                       check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        path = escape(str(root))
        config = root / "config.xml"
        config.write_text(f"""<clickhouse>
<logger><level>warning</level><log>{path}/server.log</log><errorlog>{path}/error.log</errorlog></logger>
<path>{path}/data/</path><tmp_path>{path}/tmp/</tmp_path>
<listen_host>127.0.0.1</listen_host><tcp_port>{port}</tcp_port><tcp_port_secure>{tls_port}</tcp_port_secure>
<openSSL><server><certificateFile>{path}/certificate.pem</certificateFile><privateKeyFile>{path}/key.pem</privateKeyFile>
<verificationMode>none</verificationMode></server></openSSL>
<max_connections>16</max_connections><max_server_memory_usage>1073741824</max_server_memory_usage>
<background_schedule_pool_size>4</background_schedule_pool_size><background_pool_size>16</background_pool_size>
<profiles><default><max_threads>2</max_threads><max_memory_usage>268435456</max_memory_usage></default></profiles>
<users><default><password></password><networks><ip>127.0.0.1</ip></networks><profile>default</profile><quota>default</quota></default></users>
<quotas><default><interval><duration>3600</duration><queries>0</queries></interval></default></quotas>
</clickhouse>""", encoding="utf-8")
        environment = dict(os.environ, CNETMOD_CLICKHOUSE_TEST_PORT=str(port),
                           CNETMOD_CLICKHOUSE_TLS_PORT=str(tls_port),
                           CNETMOD_CLICKHOUSE_CA_FILE=str(certificate))
        with (root / "console.log").open("w", encoding="utf-8") as console:
            server = subprocess.Popen([args.clickhouse, "server", f"--config-file={config}"],
                                      stdout=console, stderr=subprocess.STDOUT)
            try:
                deadline = time.monotonic() + 30
                while True:
                    if server.poll() is not None:
                        raise RuntimeError("ClickHouse exited during startup")
                    try:
                        with socket.create_connection(("127.0.0.1", port), timeout=0.2):
                            break
                    except OSError:
                        if time.monotonic() >= deadline:
                            raise RuntimeError("ClickHouse startup timed out")
                        time.sleep(0.1)
                for mode in ("none", "checksum", "lz4", "zstd"):
                    print(f"Native interoperability compression={mode}", flush=True)
                    environment["CNETMOD_CLICKHOUSE_COMPRESSION"] = mode
                    completed = subprocess.run([str(executable)], env=environment, timeout=60)
                    if completed.returncode:
                        raise RuntimeError(f"Native {mode} interoperability failed: {completed.returncode}")
                    if args.application_executable:
                        application = subprocess.run([str(args.application_executable.resolve())],env=environment,timeout=90)
                        if application.returncode:
                            raise RuntimeError(f"Application/ORM interoperability failed: {application.returncode}")
            except Exception:
                for name in ("console.log", "error.log"):
                    log = root / name
                    if log.exists():
                        print(log.read_text(encoding="utf-8", errors="replace")[-12000:])
                raise
            finally:
                server.terminate()
                try:
                    server.wait(timeout=15)
                except subprocess.TimeoutExpired:
                    server.kill()
                    server.wait()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
