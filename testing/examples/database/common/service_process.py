"""Black-box lifecycle and HTTP helpers for compiled production examples."""

from __future__ import annotations

import json
import os
import subprocess
import tempfile
import time
import urllib.error
import urllib.request
from dataclasses import dataclass
from pathlib import Path
from typing import Any, Callable, Mapping, Sequence


class ExampleProcessError(RuntimeError):
    pass


def required_executable(variable: str) -> Path:
    raw = os.environ.get(variable)
    if not raw:
        raise FileNotFoundError(f"{variable} is not set")
    executable = Path(raw).expanduser().resolve()
    if not executable.is_file():
        raise FileNotFoundError(f"{variable} does not name a file: {executable}")
    return executable


def wait_until(
    probe: Callable[[], Any], timeout_seconds: float, description: str
) -> Any:
    deadline = time.monotonic() + timeout_seconds
    last_error: Exception | None = None
    while time.monotonic() < deadline:
        try:
            value = probe()
            if value:
                return value
        except Exception as error:  # readiness probes are expected to fail briefly
            last_error = error
        time.sleep(0.05)
    detail = f": {last_error}" if last_error else ""
    raise TimeoutError(f"timed out waiting for {description}{detail}")


@dataclass(frozen=True)
class HttpResponse:
    status: int
    body: dict[str, Any]


class ExampleProcess:
    def __init__(
        self,
        executable: Path,
        environment: Mapping[str, str],
        arguments: Sequence[str] = (),
    ) -> None:
        self.executable = executable
        self.environment = {**os.environ, **environment}
        self.arguments = list(arguments)
        self._logs = tempfile.TemporaryDirectory(prefix="cnetmod-example-")
        self._stdout_path = Path(self._logs.name) / "stdout.log"
        self._stderr_path = Path(self._logs.name) / "stderr.log"
        self._stdout = None
        self._stderr = None
        self.process: subprocess.Popen[str] | None = None

    def start(self) -> "ExampleProcess":
        self._stdout = self._stdout_path.open("w", encoding="utf-8")
        self._stderr = self._stderr_path.open("w", encoding="utf-8")
        self.process = subprocess.Popen(
            [str(self.executable), *self.arguments],
            env=self.environment,
            stdin=subprocess.DEVNULL,
            stdout=self._stdout,
            stderr=self._stderr,
            text=True,
        )
        return self

    def poll(self) -> int | None:
        return self.process.poll() if self.process else None

    def wait(self, timeout_seconds: float = 60) -> int:
        if not self.process:
            raise ExampleProcessError("example process was not started")
        try:
            code = self.process.wait(timeout=timeout_seconds)
        except subprocess.TimeoutExpired as error:
            raise ExampleProcessError(
                f"example did not exit in {timeout_seconds}s; {self.log_tail()}"
            ) from error
        if code != 0:
            raise ExampleProcessError(
                f"example exited with {code}; {self.log_tail()}"
            )
        return code

    def request(
        self,
        base_url: str,
        method: str,
        path: str,
        payload: Mapping[str, Any] | None = None,
        timeout_seconds: float = 5,
    ) -> HttpResponse:
        data = None if payload is None else json.dumps(payload).encode("utf-8")
        request = urllib.request.Request(
            f"{base_url}{path}",
            data=data,
            method=method,
            headers={"Content-Type": "application/json", "Accept": "application/json"},
        )
        try:
            with urllib.request.urlopen(request, timeout=timeout_seconds) as response:
                raw = response.read().decode("utf-8")
                return HttpResponse(response.status, json.loads(raw) if raw else {})
        except urllib.error.HTTPError as error:
            raw = error.read().decode("utf-8")
            body = json.loads(raw) if raw else {}
            return HttpResponse(error.code, body)

    def log_tail(self, maximum_characters: int = 4000) -> str:
        for stream in (self._stdout, self._stderr):
            if stream:
                stream.flush()
        chunks = []
        for path in (self._stdout_path, self._stderr_path):
            if path.exists():
                chunks.append(path.read_text(encoding="utf-8", errors="replace"))
        return "\n".join(chunks)[-maximum_characters:]

    def stop(self, timeout_seconds: float = 10) -> None:
        if not self.process or self.process.poll() is not None:
            self.close()
            return
        self.process.terminate()
        try:
            self.process.wait(timeout=timeout_seconds)
        except subprocess.TimeoutExpired:
            self.process.kill()
            self.process.wait(timeout=5)
        self.close()

    def close(self) -> None:
        for stream in (self._stdout, self._stderr):
            if stream and not stream.closed:
                stream.close()
        self._logs.cleanup()

    def __enter__(self) -> "ExampleProcess":
        return self.start()

    def __exit__(self, *_: object) -> None:
        self.stop()


def run_scenario(
    executable: Path,
    environment: Mapping[str, str],
    scenario: str | None,
    timeout_seconds: float = 90,
) -> None:
    process = ExampleProcess(
        executable,
        environment,
        arguments=() if scenario is None else ("--scenario", scenario),
    ).start()
    try:
        process.wait(timeout_seconds)
    finally:
        process.stop()
