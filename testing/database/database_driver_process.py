"""Strict JSON-lines process contract for cnetmod database drivers."""

from __future__ import annotations

import json
import os
import subprocess
from dataclasses import dataclass
from pathlib import Path
from typing import Any


class DatabaseDriverError(RuntimeError):
    """Raised when the native driver violates the process contract."""


@dataclass(frozen=True)
class DatabaseDriver:
    executable: Path
    protocol: str

    @classmethod
    def from_environment(cls, variable: str, protocol: str) -> "DatabaseDriver":
        raw_path = os.environ.get(variable)
        if not raw_path:
            raise FileNotFoundError(f"{variable} is not set")
        executable = Path(raw_path).expanduser().resolve()
        if not executable.is_file():
            raise FileNotFoundError(f"{variable} does not name a file: {executable}")
        return cls(executable=executable, protocol=protocol)

    def request(self, operation: str, *, timeout_seconds: float = 60, **parameters: Any) -> dict[str, Any]:
        envelope = {
            "contract_version": 1,
            "protocol": self.protocol,
            "operation": operation,
            "parameters": parameters,
        }
        completed = subprocess.run(
            [str(self.executable), "--json-lines"],
            input=json.dumps(envelope, separators=(",", ":")) + "\n",
            text=True,
            encoding="utf-8",
            capture_output=True,
            timeout=timeout_seconds,
            check=False,
        )
        if completed.returncode != 0:
            raise DatabaseDriverError(
                f"{self.protocol}/{operation} exited {completed.returncode}: "
                f"{completed.stderr.strip()}"
            )
        lines = [line for line in completed.stdout.splitlines() if line.strip()]
        if len(lines) != 1:
            raise DatabaseDriverError(f"expected one response line, received {len(lines)}")
        try:
            response = json.loads(lines[0])
        except json.JSONDecodeError as error:
            raise DatabaseDriverError("native driver returned invalid JSON") from error
        if response.get("contract_version") != 1:
            raise DatabaseDriverError(f"unsupported response contract: {response!r}")
        if response.get("status") == "error":
            raise DatabaseDriverError(
                f"driver error {response.get('error_code')}: {response.get('message')}"
            )
        result = response.get("result")
        if response.get("status") != "ok" or not isinstance(result, dict):
            raise DatabaseDriverError(f"malformed driver response: {response!r}")
        return result
