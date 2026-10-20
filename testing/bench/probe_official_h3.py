#!/usr/bin/env python3
"""Supplement official H3 validation with full fixture-byte comparisons."""

import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import tempfile
import time
from urllib.parse import urlsplit


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--stage", type=Path, required=True)
    parser.add_argument("--images", type=Path, required=True)
    args = parser.parse_args()
    stage = args.stage.resolve()
    records = stage / "records"
    records.mkdir(exist_ok=True)
    names = [Path(urlsplit(url).path).name for url in
             (stage / "requests/static-h2-uris.txt").read_text().splitlines()]
    images = json.loads(args.images.read_text())
    for name, entry in images.items():
        container = "cnetmod-h3-byte-probe-" + name
        command = ["docker", "run", "-d", "--name", container, "--network", "host",
                   "--cpus=32", "--security-opt", "seccomp=unconfined",
                   "--ulimit", "memlock=-1:-1", "--ulimit", "nofile=1048576:1048576",
                   "-v", f"{stage / 'certs'}:/certs:ro",
                   "-v", f"{stage / 'data/static'}:/data/static:ro",
                   "-v", f"{stage / 'data/dataset.json'}:/data/dataset.json:ro", entry["image"]]
        subprocess.run(command, check=True, stdout=subprocess.DEVNULL)
        checks = []
        try:
            # Native/JIT entries can take longer than two seconds to bind all
            # reactors. This is readiness only, outside any timed measurement.
            for attempt in range(30):
                ready = subprocess.run(["curl", "--noproxy", "*", "--http3-only", "-skf",
                    "--max-time", "1", "https://localhost:8443/baseline2?a=13&b=42"],
                    capture_output=True)
                if ready.returncode == 0:
                    break
                time.sleep(1)
            else:
                raise RuntimeError(f"{name}: QUIC readiness timed out")
            with tempfile.TemporaryDirectory(dir=stage / "tmp") as directory:
                root = Path(directory)

                def check(path, expected, encoding="", expected_encoding=""):
                    headers, body = root / "headers", root / "body"
                    curl = ["curl", "--noproxy", "*", "--http3-only", "-skf",
                            "--max-time", "15", "-D", str(headers), "-o", str(body)]
                    if encoding:
                        curl += ["-H", "Accept-Encoding: " + encoding]
                    curl += ["https://localhost:8443" + path]
                    subprocess.run(curl, check=True, capture_output=True)
                    actual = body.read_bytes()
                    head = headers.read_text().lower()
                    assert "http/3 200" in head, head
                    assert actual == expected, (name, path, len(actual), len(expected))
                    if expected_encoding:
                        assert f"content-encoding: {expected_encoding}" in head, head
                    else:
                        assert "content-encoding:" not in head, head
                    checks.append({"path": path, "accept_encoding": encoding,
                                   "bytes": len(actual), "sha256": hashlib.sha256(actual).hexdigest()})

                check("/baseline2?a=13&b=42", b"55")
                if "static-h3" in entry["tests"]:
                    for filename in names:
                        source = stage / "data/static" / filename
                        check("/static/" + filename, source.read_bytes(), "identity")
                        compressed = source.with_name(filename + ".br")
                        use_brotli = compressed.is_file()
                        check("/static/" + filename,
                              (compressed if use_brotli else source).read_bytes(),
                              "br;q=1, gzip;q=0.8", "br" if use_brotli else "")
                    missing = subprocess.run(["curl", "--noproxy", "*", "--http3-only", "-sk",
                        "--max-time", "10", "-o", "/dev/null", "-w", "%{http_code}",
                        "https://localhost:8443/static/nonexistent.txt"], capture_output=True, check=True)
                    assert missing.stdout == b"404", missing.stdout
            (records / f"{name}-fixture-bytes.json").write_text(json.dumps({
                "status": "passed", "checks": checks, "static_missing_status":
                404 if "static-h3" in entry["tests"] else None}, indent=2))
            print(name, len(checks), "fixture byte checks passed", flush=True)
        finally:
            logs = subprocess.run(["docker", "logs", container], capture_output=True)
            (records / f"{name}-byte-probe-server.log").write_bytes(logs.stdout + logs.stderr)
            subprocess.run(["docker", "rm", "-f", container], stdout=subprocess.DEVNULL, check=True)


if __name__ == "__main__":
    main()
