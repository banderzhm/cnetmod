"""JSON-lines process contract used by all cnetmod messaging test drivers."""

from __future__ import annotations

import json
import os
import subprocess
import threading
import time
from dataclasses import dataclass
from pathlib import Path
from typing import Any


class DriverContractError(RuntimeError):
    pass


@dataclass(frozen=True)
class MessagingDriver:
    executable: Path
    protocol: str

    @classmethod
    def from_environment(cls, variable: str, protocol: str) -> "MessagingDriver":
        raw_path = os.environ.get(variable)
        if not raw_path:
            raise FileNotFoundError(f"{variable} is not set")
        executable = Path(raw_path).resolve()
        if not executable.is_file():
            raise FileNotFoundError(f"{variable} does not name a file: {executable}")
        return cls(executable, protocol)

    def request(self, operation: str, **parameters: Any) -> dict[str, Any]:
        process_timeout = float(parameters.pop("process_timeout_seconds", 60))
        request = {
            "contract_version": 1,
            "protocol": self.protocol,
            "operation": operation,
            "parameters": parameters,
        }
        completed = subprocess.run(
            [str(self.executable), "--json-lines"],
            input=json.dumps(request, separators=(",", ":")) + "\n",
            encoding="utf-8",
            capture_output=True,
            timeout=process_timeout,
            check=False,
        )
        if completed.returncode != 0:
            raise DriverContractError(
                f"{self.protocol}/{operation} exited {completed.returncode}: "
                f"{completed.stderr.strip()}"
            )
        lines = [line for line in completed.stdout.splitlines() if line.strip()]
        if len(lines) != 1:
            raise DriverContractError(
                f"expected one JSON response, received {len(lines)} lines"
            )
        try:
            response = json.loads(lines[0])
        except json.JSONDecodeError as error:
            raise DriverContractError(f"invalid driver JSON: {lines[0]!r}") from error
        if response.get("contract_version") != 1:
            raise DriverContractError(f"unsupported response contract: {response!r}")
        if response.get("status") == "error":
            raise DriverContractError(
                f"driver error {response.get('error_code')}: {response.get('message')}"
            )
        if response.get("status") != "ok" or not isinstance(response.get("result"), dict):
            raise DriverContractError(f"malformed driver response: {response!r}")
        return response["result"]

    def request_during_fault(
        self,
        operation: str,
        fault_action,
        fault_delay_seconds: float = 1.0,
        **parameters: Any,
    ) -> dict[str, Any]:
        """Run a driver operation while Docker injects a transport/service fault."""
        failure: list[BaseException] = []

        def inject() -> None:
            try:
                time.sleep(fault_delay_seconds)
                fault_action()
            except BaseException as error:  # propagate infrastructure failure
                failure.append(error)

        injector = threading.Thread(target=inject, daemon=True)
        injector.start()
        request_error: BaseException | None = None
        result: dict[str, Any] | None = None
        try:
            result = self.request(operation, **parameters)
        except BaseException as error:
            request_error = error
        finally:
            injector.join(timeout=120)
        if injector.is_alive():
            raise DriverContractError("fault injection did not finish")
        if failure:
            raise DriverContractError(f"fault injection failed: {failure[0]}")
        if request_error is not None:
            raise request_error
        if result is None:
            raise DriverContractError("driver returned no result")
        return result
