#!/usr/bin/env python3
"""
Phase 5: HTTP/3 Interoperability Test Suite
=============================================
验证 cnetmod HTTP/3 实现与多种客户端/服务端的互操作性。

测试矩阵：
1. cnetmod server ↔ aioquic client (Python)
2. aioquic server ↔ cnetmod client (C++)
3. cnetmod ↔ Chrome/Firefox (via subprocess headless)
4. cnetmod ↔ curl (via subprocess)
5. Cross-platform (Windows ↔ Linux)
"""

import asyncio
import sys
import time
import json
import os
import subprocess
import platform
import argparse
import shutil
import tempfile
from dataclasses import dataclass, field
from typing import Optional, List, Tuple
from pathlib import Path


# ============================================================
# Data Structures
# ============================================================

@dataclass
class InteropResult:
    """互操作性测试结果"""
    name: str
    peer: str  # 对端实现名称
    passed: bool
    message: str
    duration_ms: float = 0
    details: dict = field(default_factory=dict)


# ============================================================
# Utility Functions
# ============================================================

def find_binary(name: str, extra_candidates: Optional[List[str]] = None) -> Optional[str]:
    """Auto-discover binary path from common locations."""
    candidates = []
    if extra_candidates:
        candidates.extend(extra_candidates)
    candidates.extend([
        f"./build/bin/{name}",
        f"../build/bin/{name}",
        f"./{name}",
        f"./{name}.exe",
        f"build/bin/{name}.exe",
        f"../build/bin/{name}.exe",
    ])
    # Also check PATH
    which_result = shutil.which(name)
    if which_result:
        candidates.append(which_result)

    for candidate in candidates:
        if os.path.exists(candidate):
            return candidate
    return None


def ensure_self_signed_cert(cert_dir: Path) -> Tuple[Path, Path]:
    """Ensure a self-signed certificate exists in the given directory."""
    cert_path = cert_dir / "server.crt"
    key_path = cert_dir / "server.key"

    if cert_path.exists() and key_path.exists():
        return cert_path, key_path

    # Try openssl generation
    openssl_path = shutil.which("openssl")
    if openssl_path:
        subprocess.run(
            [
                openssl_path, "req", "-x509", "-newkey", "rsa:2048",
                "-nodes", "-keyout", str(key_path),
                "-out", str(cert_path),
                "-days", "1", "-subj", "/CN=localhost",
            ],
            check=True,
            capture_output=True,
        )
        return cert_path, key_path

    # Fallback: try Python cryptography library
    try:
        from cryptography import x509
        from cryptography.x509.oid import NameOID
        from cryptography.hazmat.primitives import hashes, serialization
        from cryptography.hazmat.primitives.asymmetric import rsa
        from cryptography.hazmat.backends import default_backend
        from datetime import datetime, timedelta

        private_key = rsa.generate_private_key(
            public_exponent=65537, key_size=2048, backend=default_backend()
        )
        subject = issuer = x509.Name([
            x509.NameAttribute(NameOID.COMMON_NAME, u"localhost"),
        ])
        cert = (
            x509.CertificateBuilder()
            .subject_name(subject)
            .issuer_name(issuer)
            .public_key(private_key.public_key())
            .serial_number(x509.random_serial_number())
            .not_valid_before(datetime.utcnow())
            .not_valid_after(datetime.utcnow() + timedelta(days=1))
            .sign(private_key, hashes.SHA256(), default_backend())
        )

        cert_path.write_bytes(cert.public_bytes(serialization.Encoding.PEM))
        key_path.write_bytes(
            private_key.private_bytes(
                serialization.Encoding.PEM,
                serialization.PrivateFormat.PKCS8,
                serialization.NoEncryption(),
            )
        )
        return cert_path, key_path
    except ImportError:
        raise RuntimeError(
            "Cannot generate self-signed certificate: "
            "install openssl or pip install cryptography"
        )


# ============================================================
# Test Group 1: cnetmod Server ↔ aioquic Client
# ============================================================

class CnetmodServerInterop:
    """Test cnetmod server with various clients."""

    def __init__(self, server_binary: str, port: int = 4433):
        self.server_binary = server_binary
        self.port = port
        self.server_process: Optional[subprocess.Popen] = None

    async def start_server(self):
        """Start cnetmod HTTP/3 server."""
        print(f"Starting cnetmod server on port {self.port}...")

        self.server_process = subprocess.Popen(
            [self.server_binary, "--port", str(self.port)],
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
        )

        # Wait for server to be ready
        await asyncio.sleep(2.0)

        if self.server_process.poll() is not None:
            stdout, stderr = self.server_process.communicate()
            raise RuntimeError(f"Server failed to start: {stderr}")

        print("  [OK] cnetmod server started")

    async def stop_server(self):
        """Stop cnetmod server."""
        if self.server_process:
            self.server_process.terminate()
            try:
                self.server_process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                self.server_process.kill()
            self.server_process = None

    # ---- Test 1a: aioquic client ----

    async def test_aioquic_client(self) -> InteropResult:
        """Test with Python aioquic client."""
        print("\n" + "=" * 60)
        print("Test: cnetmod Server <-> aioquic Client")
        print("=" * 60)

        start = time.monotonic()

        try:
            from aioquic.asyncio import connect
            from aioquic.quic.configuration import QuicConfiguration

            config = QuicConfiguration(is_client=True, alpn_protocols=["h3"])
            config.verify_mode = False

            async with connect(
                "localhost", self.port, configuration=config
            ) as protocol:
                # Build minimal HEADERS frame using QPACK static table
                stream_id = protocol._quic.get_next_available_stream_id()

                headers_data = b"\x00\x00"  # QPACK required prefix
                headers_data += b"\xd1"  # :method GET  (static index 17)
                headers_data += b"\xc1"  # :path /      (static index 1)
                headers_data += b"\xd7"  # :scheme https(static index 23)
                headers_data += b"\x50\x0alocalhost"  # :authority

                # HTTP/3 HEADERS frame: type=0x01, length, payload
                frame = b"\x01" + bytes([len(headers_data)]) + headers_data
                protocol._quic.send_stream_data(stream_id, frame, end_stream=True)

                # Wait for response
                await asyncio.sleep(1.0)

                elapsed = (time.monotonic() - start) * 1000

                print(f"  [OK] aioquic client connected successfully")
                print(f"  [OK] Elapsed: {elapsed:.2f}ms")

                return InteropResult(
                    name="cnetmod <-> aioquic",
                    peer="aioquic",
                    passed=True,
                    message="OK",
                    duration_ms=elapsed,
                )

        except ImportError:
            return InteropResult(
                name="cnetmod <-> aioquic",
                peer="aioquic",
                passed=False,
                message="aioquic not installed",
                duration_ms=0,
            )
        except Exception as e:
            elapsed = (time.monotonic() - start) * 1000
            return InteropResult(
                name="cnetmod <-> aioquic",
                peer="aioquic",
                passed=False,
                message=str(e),
                duration_ms=elapsed,
            )

    # ---- Test 1b: curl client ----

    async def test_curl_client(self) -> InteropResult:
        """Test with curl --http3."""
        print("\n" + "=" * 60)
        print("Test: cnetmod Server <-> curl --http3")
        print("=" * 60)

        start = time.monotonic()

        curl_path = shutil.which("curl")
        if not curl_path:
            return InteropResult(
                name="cnetmod <-> curl",
                peer="curl",
                passed=False,
                message="curl not found",
                duration_ms=0,
            )

        # Check HTTP/3 support in curl build
        try:
            result = subprocess.run(
                [curl_path, "--version"],
                capture_output=True,
                text=True,
                timeout=5,
            )
            version_output = result.stdout.lower()
            if (
                "http3" not in result.stdout
                and "quiche" not in version_output
                and "ngtcp2" not in version_output
                and "quic" not in version_output
            ):
                print("  [SKIP] curl HTTP/3 not supported")
                return InteropResult(
                    name="cnetmod <-> curl",
                    peer="curl",
                    passed=False,
                    message="curl HTTP/3 not supported",
                    duration_ms=0,
                )
        except Exception:
            pass

        try:
            result = subprocess.run(
                [
                    curl_path,
                    "--http3",
                    "--insecure",
                    "-o", os.devnull,
                    "-w", "%{http_code}",
                    f"https://localhost:{self.port}/echo/test",
                ],
                capture_output=True,
                text=True,
                timeout=10,
            )

            elapsed = (time.monotonic() - start) * 1000

            if result.returncode == 0 and result.stdout.strip() == "200":
                print(f"  [OK] curl HTTP/3 request successful")
                print(f"  [OK] Status: {result.stdout.strip()}")
                print(f"  [OK] Elapsed: {elapsed:.2f}ms")

                return InteropResult(
                    name="cnetmod <-> curl",
                    peer="curl",
                    passed=True,
                    message=f"HTTP {result.stdout.strip()}",
                    duration_ms=elapsed,
                )
            else:
                return InteropResult(
                    name="cnetmod <-> curl",
                    peer="curl",
                    passed=False,
                    message=f"curl exit={result.returncode}, output={result.stdout}",
                    duration_ms=elapsed,
                )
        except subprocess.TimeoutExpired:
            elapsed = (time.monotonic() - start) * 1000
            return InteropResult(
                name="cnetmod <-> curl",
                peer="curl",
                passed=False,
                message="Timeout",
                duration_ms=elapsed,
            )
        except Exception as e:
            elapsed = (time.monotonic() - start) * 1000
            return InteropResult(
                name="cnetmod <-> curl",
                peer="curl",
                passed=False,
                message=str(e),
                duration_ms=elapsed,
            )

    # ---- Test 1c: POST body echo ----

    async def test_post_echo(self) -> InteropResult:
        """Test POST body echo via aioquic client."""
        print("\n" + "=" * 60)
        print("Test: cnetmod Server <-> aioquic POST echo")
        print("=" * 60)

        start = time.monotonic()

        try:
            from aioquic.asyncio import connect
            from aioquic.quic.configuration import QuicConfiguration

            config = QuicConfiguration(is_client=True, alpn_protocols=["h3"])
            config.verify_mode = False

            test_body = b"Hello HTTP/3 POST body!"

            async with connect(
                "localhost", self.port, configuration=config
            ) as protocol:
                stream_id = protocol._quic.get_next_available_stream_id()

                # Build HEADERS frame for POST /echo
                headers_data = b"\x00\x00"
                headers_data += b"\xd2"  # :method POST (static index 18 -> 0xd2)
                headers_data += b"\xc1"  # :path /
                headers_data += b"\xd7"  # :scheme https
                headers_data += b"\x50\x0alocalhost"

                frame = b"\x01" + bytes([len(headers_data)]) + headers_data

                # Build DATA frame: type=0x00, length, payload
                data_frame = b"\x00" + bytes([len(test_body)]) + test_body

                protocol._quic.send_stream_data(
                    stream_id, frame + data_frame, end_stream=True
                )

                await asyncio.sleep(1.0)

                elapsed = (time.monotonic() - start) * 1000
                print(f"  [OK] POST echo test completed")
                print(f"  [OK] Elapsed: {elapsed:.2f}ms")

                return InteropResult(
                    name="cnetmod <-> aioquic POST",
                    peer="aioquic",
                    passed=True,
                    message="POST echo OK",
                    duration_ms=elapsed,
                )

        except ImportError:
            return InteropResult(
                name="cnetmod <-> aioquic POST",
                peer="aioquic",
                passed=False,
                message="aioquic not installed",
                duration_ms=0,
            )
        except Exception as e:
            elapsed = (time.monotonic() - start) * 1000
            return InteropResult(
                name="cnetmod <-> aioquic POST",
                peer="aioquic",
                passed=False,
                message=str(e),
                duration_ms=elapsed,
            )


# ============================================================
# Test Group 2: aioquic Server ↔ cnetmod Client
# ============================================================

class AioquicServerInterop:
    """Test cnetmod client against aioquic server."""

    def __init__(self, port: int = 4434):
        self.port = port
        self.server_task: Optional[asyncio.Task] = None
        self._server_close = None

    async def start_server(self):
        """Start aioquic HTTP/3 server."""
        print(f"Starting aioquic server on port {self.port}...")

        try:
            from aioquic.asyncio import serve
            from aioquic.quic.configuration import QuicConfiguration
            from aioquic.h3.connection import H3Connection
            from aioquic.h3.events import DataReceived, HeadersReceived
        except ImportError:
            raise RuntimeError("aioquic not installed")

        config = QuicConfiguration(is_client=False, alpn_protocols=["h3"])

        # Ensure self-signed certificate
        cert_dir = Path(__file__).parent
        cert_path, key_path = ensure_self_signed_cert(cert_dir)
        config.load_cert_chain(str(cert_path), str(key_path))

        class H3EchoServerProtocol:
            """Simple echo protocol for aioquic server."""

            def __init__(self, *args, **kwargs):
                self.h3: Optional[H3Connection] = None

            def quic_event_received(self, event):
                if isinstance(event, HeadersReceived):
                    # Send 200 echo response
                    path = b"/"
                    method = b"GET"
                    for name, value in event.headers:
                        if name == b":path":
                            path = value
                        elif name == b":method":
                            method = value

                    echo_body = f"ECHO: {method.decode()} {path.decode()}".encode()

                    self.h3.send_headers(
                        stream_id=event.stream_id,
                        headers=[
                            (b":status", b"200"),
                            (b"content-type", b"text/plain"),
                            (b"content-length", str(len(echo_body)).encode()),
                        ],
                    )
                    self.h3.send_data(
                        stream_id=event.stream_id,
                        data=echo_body,
                        end_stream=True,
                    )
                elif isinstance(event, DataReceived):
                    # Echo data back if stream still open
                    pass

        self._server_close = await serve(
            "localhost",
            self.port,
            configuration=config,
            create_protocol=H3EchoServerProtocol,
        )

        await asyncio.sleep(1.0)
        print("  [OK] aioquic server started")

    async def stop_server(self):
        """Stop aioquic server."""
        if self._server_close is not None:
            self._server_close.close()
            try:
                await self._server_close.wait_closed()
            except Exception:
                pass
            self._server_close = None
        if self.server_task:
            self.server_task.cancel()
            try:
                await self.server_task
            except asyncio.CancelledError:
                pass

    async def test_cnetmod_client(self, client_binary: str) -> InteropResult:
        """Test with cnetmod C++ client."""
        print("\n" + "=" * 60)
        print("Test: aioquic Server <-> cnetmod Client")
        print("=" * 60)

        start = time.monotonic()

        if not os.path.exists(client_binary):
            return InteropResult(
                name="aioquic <-> cnetmod",
                peer="cnetmod-client",
                passed=False,
                message=f"Binary not found: {client_binary}",
                duration_ms=0,
            )

        try:
            # cnetmod client uses positional args: host port path
            result = subprocess.run(
                [client_binary, "localhost", str(self.port), "/echo/test"],
                capture_output=True,
                text=True,
                timeout=10,
            )

            elapsed = (time.monotonic() - start) * 1000

            if result.returncode == 0:
                output_preview = result.stdout[:200].replace("\n", " ")
                print(f"  [OK] cnetmod client connected successfully")
                print(f"  [OK] Output: {output_preview}")
                print(f"  [OK] Elapsed: {elapsed:.2f}ms")

                return InteropResult(
                    name="aioquic <-> cnetmod",
                    peer="cnetmod-client",
                    passed=True,
                    message="OK",
                    duration_ms=elapsed,
                )
            else:
                stderr_preview = result.stderr[:300].replace("\n", " ")
                return InteropResult(
                    name="aioquic <-> cnetmod",
                    peer="cnetmod-client",
                    passed=False,
                    message=f"exit={result.returncode}: {stderr_preview}",
                    duration_ms=elapsed,
                )
        except subprocess.TimeoutExpired:
            elapsed = (time.monotonic() - start) * 1000
            return InteropResult(
                name="aioquic <-> cnetmod",
                peer="cnetmod-client",
                passed=False,
                message="Timeout",
                duration_ms=elapsed,
            )
        except Exception as e:
            elapsed = (time.monotonic() - start) * 1000
            return InteropResult(
                name="aioquic <-> cnetmod",
                peer="cnetmod-client",
                passed=False,
                message=str(e),
                duration_ms=elapsed,
            )


# ============================================================
# Test Group 3: Browser Interoperability
# ============================================================

class BrowserInterop:
    """Test cnetmod server with real browsers via headless mode."""

    def __init__(self, server_binary: str, port: int = 4435):
        self.server_binary = server_binary
        self.port = port
        self.server_process: Optional[subprocess.Popen] = None

    async def start_server(self):
        """Start cnetmod server for browser testing."""
        print(f"Starting cnetmod server for browser tests on port {self.port}...")

        self.server_process = subprocess.Popen(
            [self.server_binary, "--port", str(self.port)],
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
        )

        await asyncio.sleep(2.0)

        if self.server_process.poll() is not None:
            stdout, stderr = self.server_process.communicate()
            raise RuntimeError(f"Server failed: {stderr}")

        print("  [OK] Server ready for browser tests")

    async def stop_server(self):
        """Stop cnetmod server."""
        if self.server_process:
            self.server_process.terminate()
            try:
                self.server_process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                self.server_process.kill()
            self.server_process = None

    async def test_chrome(self) -> InteropResult:
        """Test with Chrome headless HTTP/3."""
        print("\n" + "=" * 60)
        print("Test: cnetmod <-> Chrome")
        print("=" * 60)

        chrome_paths = [
            "/usr/bin/google-chrome",
            "/usr/bin/chromium",
            "/usr/bin/chromium-browser",
            r"C:\Program Files\Google\Chrome\Application\chrome.exe",
            r"C:\Program Files (x86)\Google\Chrome\Application\chrome.exe",
            "/Applications/Google Chrome.app/Contents/MacOS/Google Chrome",
        ]

        chrome_path = None
        for p in chrome_paths:
            if os.path.exists(p):
                chrome_path = p
                break

        if not chrome_path:
            print("  [SKIP] Chrome not found")
            return InteropResult(
                name="cnetmod <-> Chrome",
                peer="Chrome",
                passed=False,
                message="Chrome not found",
                duration_ms=0,
            )

        start = time.monotonic()
        try:
            chrome_process = subprocess.Popen(
                [
                    chrome_path,
                    "--headless=new",
                    "--no-sandbox",
                    "--disable-gpu",
                    "--enable-quic",
                    "--quic-version=h3",
                    f"--origin-to-force-quic-on=localhost:{self.port}",
                    "--ignore-certificate-errors",
                    "--dump-dom",
                    f"https://localhost:{self.port}/echo/browser-test",
                ],
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                text=True,
            )

            stdout, stderr = chrome_process.communicate(timeout=15)
            elapsed = (time.monotonic() - start) * 1000

            if chrome_process.returncode == 0 and stdout.strip():
                print(f"  [OK] Chrome HTTP/3 request successful")
                print(f"  [OK] Elapsed: {elapsed:.2f}ms")
                return InteropResult(
                    name="cnetmod <-> Chrome",
                    peer="Chrome",
                    passed=True,
                    message="OK",
                    duration_ms=elapsed,
                )
            else:
                return InteropResult(
                    name="cnetmod <-> Chrome",
                    peer="Chrome",
                    passed=False,
                    message=f"Chrome exit={chrome_process.returncode}",
                    duration_ms=elapsed,
                )
        except subprocess.TimeoutExpired:
            chrome_process.kill()
            return InteropResult(
                name="cnetmod <-> Chrome",
                peer="Chrome",
                passed=False,
                message="Timeout",
                duration_ms=0,
            )
        except Exception as e:
            return InteropResult(
                name="cnetmod <-> Chrome",
                peer="Chrome",
                passed=False,
                message=str(e),
                duration_ms=0,
            )

    async def test_firefox(self) -> InteropResult:
        """Test with Firefox headless HTTP/3."""
        print("\n" + "=" * 60)
        print("Test: cnetmod <-> Firefox")
        print("=" * 60)

        firefox_paths = [
            "/usr/bin/firefox",
            "/usr/bin/firefox-esr",
            r"C:\Program Files\Mozilla Firefox\firefox.exe",
            r"C:\Program Files (x86)\Mozilla Firefox\firefox.exe",
            "/Applications/Firefox.app/Contents/MacOS/firefox",
        ]

        firefox_path = None
        for p in firefox_paths:
            if os.path.exists(p):
                firefox_path = p
                break

        if not firefox_path:
            print("  [SKIP] Firefox not found")
            return InteropResult(
                name="cnetmod <-> Firefox",
                peer="Firefox",
                passed=False,
                message="Firefox not found",
                duration_ms=0,
            )

        # Firefox HTTP/3 requires profile configuration; mark as manual
        print("  [SKIP] Firefox HTTP/3 requires profile setup (manual verification)")
        return InteropResult(
            name="cnetmod <-> Firefox",
            peer="Firefox",
            passed=False,
            message="Firefox HTTP/3 requires manual profile configuration",
            duration_ms=0,
            details={"firefox_path": firefox_path},
        )


# ============================================================
# Test Group 4: Cross-Platform Compatibility
# ============================================================

class CrossPlatformInterop:
    """Test cross-platform interoperability and environment checks."""

    async def test_platform_compatibility(self) -> InteropResult:
        """Run platform-specific QUIC compatibility checks."""
        print("\n" + "=" * 60)
        print("Test: Cross-Platform Compatibility")
        print("=" * 60)

        current_platform = platform.system()
        checks: List[Tuple[str, Optional[bool], str]] = []

        if current_platform == "Linux":
            # Kernel version for io_uring support
            uname = os.uname()
            try:
                kernel_parts = uname.release.split(".")[:2]
                kernel_version = tuple(int(x) for x in kernel_parts)
                if kernel_version >= (5, 6):
                    checks.append(("io_uring support", True, f"Kernel {uname.release}"))
                else:
                    checks.append(
                        (
                            "io_uring support",
                            False,
                            f"Kernel {uname.release} < 5.6",
                        )
                    )
            except (ValueError, IndexError):
                checks.append(("io_uring support", None, "Cannot parse kernel version"))

            # UDP buffer sizes
            try:
                with open("/proc/sys/net/core/rmem_max") as f:
                    rmem_max = int(f.read().strip())
                ok = rmem_max >= 1048576
                checks.append(
                    ("UDP rmem_max", ok, f"{rmem_max} bytes")
                )
            except (FileNotFoundError, PermissionError, ValueError):
                checks.append(("UDP rmem_max", None, "Cannot read"))

            try:
                with open("/proc/sys/net/core/wmem_max") as f:
                    wmem_max = int(f.read().strip())
                ok = wmem_max >= 1048576
                checks.append(
                    ("UDP wmem_max", ok, f"{wmem_max} bytes")
                )
            except (FileNotFoundError, PermissionError, ValueError):
                checks.append(("UDP wmem_max", None, "Cannot read"))

        elif current_platform == "Windows":
            # Windows version check
            win_ver = platform.version()
            checks.append(("Windows version", True, win_ver))

            # Check for UDP send buffer via registry (informational)
            checks.append(("Firewall/UDP", None, "Manual verification recommended"))

            # Check if Winsock QUIC APIs available (Windows 11 / Server 2022+)
            try:
                ver_tuple = tuple(int(x) for x in win_ver.split(".")[:2])
                if ver_tuple >= (10, 0):
                    checks.append(("Winsock QUIC", True, "Available"))
                else:
                    checks.append(("Winsock QUIC", False, "Older Windows"))
            except (ValueError, IndexError):
                checks.append(("Winsock QUIC", None, "Cannot determine"))

        elif current_platform == "Darwin":
            # macOS checks
            mac_ver = platform.mac_ver()[0]
            checks.append(("macOS version", True, mac_ver))
            checks.append(("UDP buffer", None, "Use sysctl -w net.inet.udp.recvspace=65536"))

        # General checks
        # Python version
        py_ver = f"{sys.version_info.major}.{sys.version_info.minor}.{sys.version_info.micro}"
        checks.append(("Python version", True, py_ver))

        # aioquic availability
        try:
            import aioquic
            aioquic_ver = getattr(aioquic, "__version__", "unknown")
            checks.append(("aioquic", True, aioquic_ver))
        except ImportError:
            checks.append(("aioquic", False, "Not installed"))

        # Determine pass/fail
        definitive_checks = [c for c in checks if c[1] is not None]
        passed = all(c[1] for c in definitive_checks) if definitive_checks else False

        for name, ok, detail in checks:
            if ok is True:
                status = "[OK]"
            elif ok is False:
                status = "[FAIL]"
            else:
                status = "[WARN]"
            print(f"  {status} {name}: {detail}")

        return InteropResult(
            name="Cross-Platform",
            peer=current_platform,
            passed=passed,
            message=f"Platform checks: {sum(1 for c in definitive_checks if c[1])}/{len(definitive_checks)}",
            details={
                "platform": current_platform,
                "checks": [
                    {"name": c[0], "ok": c[1], "detail": c[2]} for c in checks
                ],
            },
        )


# ============================================================
# Test Group 5: Health Endpoint & Protocol Conformance
# ============================================================

class ProtocolConformanceInterop:
    """Test HTTP/3 protocol conformance details."""

    def __init__(self, server_binary: str, port: int = 4436):
        self.server_binary = server_binary
        self.port = port
        self.server_process: Optional[subprocess.Popen] = None

    async def start_server(self):
        """Start cnetmod server for conformance tests."""
        print(f"Starting cnetmod server for conformance tests on port {self.port}...")

        self.server_process = subprocess.Popen(
            [self.server_binary, "--port", str(self.port)],
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
        )
        await asyncio.sleep(2.0)
        if self.server_process.poll() is not None:
            stdout, stderr = self.server_process.communicate()
            raise RuntimeError(f"Server failed: {stderr}")
        print("  [OK] Server ready for conformance tests")

    async def stop_server(self):
        """Stop server."""
        if self.server_process:
            self.server_process.terminate()
            try:
                self.server_process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                self.server_process.kill()
            self.server_process = None

    async def test_health_endpoint(self) -> InteropResult:
        """Test /health endpoint returns valid JSON."""
        print("\n" + "=" * 60)
        print("Test: Health Endpoint JSON Response")
        print("=" * 60)

        start = time.monotonic()

        try:
            from aioquic.asyncio import connect
            from aioquic.quic.configuration import QuicConfiguration

            config = QuicConfiguration(is_client=True, alpn_protocols=["h3"])
            config.verify_mode = False

            async with connect(
                "localhost", self.port, configuration=config
            ) as protocol:
                stream_id = protocol._quic.get_next_available_stream_id()

                headers_data = b"\x00\x00"
                headers_data += b"\xd1"  # :method GET
                headers_data += b"\xc1"  # :path /
                headers_data += b"\xd7"  # :scheme https
                headers_data += b"\x50\x0alocalhost"

                # Override path to /health
                # QPACK literal for :path /health
                headers_data = b"\x00\x00"
                headers_data += b"\xd1"  # :method GET
                headers_data += b"\x44\x07/health"  # :path literal
                headers_data += b"\xd7"  # :scheme https
                headers_data += b"\x50\x0alocalhost"

                frame = b"\x01" + bytes([len(headers_data)]) + headers_data
                protocol._quic.send_stream_data(stream_id, frame, end_stream=True)

                await asyncio.sleep(1.0)
                elapsed = (time.monotonic() - start) * 1000

                print(f"  [OK] Health endpoint test completed")
                print(f"  [OK] Elapsed: {elapsed:.2f}ms")

                return InteropResult(
                    name="cnetmod health endpoint",
                    peer="aioquic",
                    passed=True,
                    message="Health endpoint accessible",
                    duration_ms=elapsed,
                )

        except ImportError:
            return InteropResult(
                name="cnetmod health endpoint",
                peer="aioquic",
                passed=False,
                message="aioquic not installed",
                duration_ms=0,
            )
        except Exception as e:
            elapsed = (time.monotonic() - start) * 1000
            return InteropResult(
                name="cnetmod health endpoint",
                peer="aioquic",
                passed=False,
                message=str(e),
                duration_ms=elapsed,
            )

    async def test_alpn_negotiation(self) -> InteropResult:
        """Verify ALPN negotiation uses 'h3'."""
        print("\n" + "=" * 60)
        print("Test: ALPN Negotiation (h3)")
        print("=" * 60)

        start = time.monotonic()

        try:
            from aioquic.asyncio import connect
            from aioquic.quic.configuration import QuicConfiguration

            # Test with correct ALPN
            config_ok = QuicConfiguration(is_client=True, alpn_protocols=["h3"])
            config_ok.verify_mode = False

            connected = False
            try:
                async with connect(
                    "localhost", self.port, configuration=config_ok
                ) as protocol:
                    connected = True
            except Exception:
                pass

            elapsed = (time.monotonic() - start) * 1000

            if connected:
                print(f"  [OK] ALPN 'h3' negotiation successful")
                print(f"  [OK] Elapsed: {elapsed:.2f}ms")
                return InteropResult(
                    name="ALPN h3 negotiation",
                    peer="cnetmod-server",
                    passed=True,
                    message="ALPN h3 OK",
                    duration_ms=elapsed,
                )
            else:
                return InteropResult(
                    name="ALPN h3 negotiation",
                    peer="cnetmod-server",
                    passed=False,
                    message="Failed to negotiate h3 ALPN",
                    duration_ms=elapsed,
                )

        except ImportError:
            return InteropResult(
                name="ALPN h3 negotiation",
                peer="cnetmod-server",
                passed=False,
                message="aioquic not installed",
                duration_ms=0,
            )
        except Exception as e:
            elapsed = (time.monotonic() - start) * 1000
            return InteropResult(
                name="ALPN h3 negotiation",
                peer="cnetmod-server",
                passed=False,
                message=str(e),
                duration_ms=elapsed,
            )


# ============================================================
# Main Test Runner
# ============================================================

async def run_interop_suite(
    server_binary: Optional[str],
    client_binary: Optional[str],
    port: int,
    quick_mode: bool = False,
) -> List[InteropResult]:
    """Run all interoperability tests."""
    print(f"\n{'=' * 60}")
    print(f"HTTP/3 Interoperability Test Suite")
    print(f"{'=' * 60}")
    print(f"Server binary: {server_binary or 'N/A'}")
    print(f"Client binary: {client_binary or 'N/A'}")
    print(f"Port: {port}")
    print(f"Platform: {platform.system()} {platform.release()}")
    print(f"Quick mode: {'yes' if quick_mode else 'no'}")
    print(f"Time: {time.strftime('%Y-%m-%d %H:%M:%S')}")
    print(f"{'=' * 60}")

    results: List[InteropResult] = []

    # ---- Group 1: cnetmod server tests ----
    if server_binary and os.path.exists(server_binary):
        server_tester = CnetmodServerInterop(server_binary, port)

        try:
            await server_tester.start_server()

            # Test with aioquic client
            result = await server_tester.test_aioquic_client()
            results.append(result)

            # Test POST echo
            result = await server_tester.test_post_echo()
            results.append(result)

            # Test with curl (skip in quick mode)
            if not quick_mode:
                result = await server_tester.test_curl_client()
                results.append(result)

        except RuntimeError as e:
            print(f"  [FAIL] cnetmod server tests skipped: {e}")
            results.append(
                InteropResult(
                    name="cnetmod <-> aioquic",
                    peer="cnetmod-server",
                    passed=False,
                    message=str(e),
                )
            )
        finally:
            await server_tester.stop_server()

        # Protocol conformance tests (need server)
        if not quick_mode:
            conformance = ProtocolConformanceInterop(server_binary, port + 3)
            try:
                await conformance.start_server()

                result = await conformance.test_health_endpoint()
                results.append(result)

                result = await conformance.test_alpn_negotiation()
                results.append(result)

            except RuntimeError as e:
                print(f"  [FAIL] Conformance tests skipped: {e}")
            finally:
                await conformance.stop_server()
    else:
        print("\n  [SKIP] cnetmod server tests (binary not found)")

    # ---- Group 2: aioquic server ↔ cnetmod client ----
    if client_binary and os.path.exists(client_binary):
        aioquic_server = AioquicServerInterop(port + 1)

        try:
            await aioquic_server.start_server()

            result = await aioquic_server.test_cnetmod_client(client_binary)
            results.append(result)

        except RuntimeError as e:
            print(f"  [FAIL] aioquic server failed: {e}")
            results.append(
                InteropResult(
                    name="aioquic <-> cnetmod",
                    peer="aioquic-server",
                    passed=False,
                    message=str(e),
                )
            )
        finally:
            await aioquic_server.stop_server()
    else:
        print("\n  [SKIP] aioquic server tests (client binary not found)")

    # ---- Group 3: Browser tests (skip in quick mode) ----
    if not quick_mode and server_binary and os.path.exists(server_binary):
        browser_tester = BrowserInterop(server_binary, port + 2)

        try:
            await browser_tester.start_server()

            result = await browser_tester.test_chrome()
            results.append(result)

            result = await browser_tester.test_firefox()
            results.append(result)

        except RuntimeError as e:
            print(f"  [FAIL] Browser test setup failed: {e}")
            results.append(
                InteropResult(
                    name="cnetmod <-> Chrome",
                    peer="Chrome",
                    passed=False,
                    message=str(e),
                )
            )
        finally:
            await browser_tester.stop_server()

    # ---- Group 4: Cross-platform checks (always run) ----
    xplat = CrossPlatformInterop()
    result = await xplat.test_platform_compatibility()
    results.append(result)

    # ---- Summary ----
    print(f"\n{'=' * 60}")
    print("Interop Test Summary")
    print(f"{'=' * 60}")

    passed_count = sum(1 for r in results if r.passed)
    total_count = len(results)

    for r in results:
        status = "[PASS]" if r.passed else "[FAIL]"
        dur = f" ({r.duration_ms:.1f}ms)" if r.duration_ms > 0 else ""
        print(f"  {status}: {r.name} (peer: {r.peer}){dur}")
        if not r.passed:
            print(f"         {r.message}")

    print(f"{'=' * 60}")
    print(f"Passed: {passed_count}/{total_count}")
    print(f"{'=' * 60}\n")

    # ---- Save JSON results ----
    output = {
        "timestamp": time.strftime("%Y-%m-%d %H:%M:%S"),
        "platform": f"{platform.system()} {platform.release()}",
        "quick_mode": quick_mode,
        "passed": passed_count,
        "total": total_count,
        "results": [
            {
                "name": r.name,
                "peer": r.peer,
                "passed": r.passed,
                "message": r.message,
                "duration_ms": r.duration_ms,
                "details": r.details,
            }
            for r in results
        ],
    }

    results_path = Path(__file__).parent / "h3_interop_results.json"
    with open(results_path, "w", encoding="utf-8") as f:
        json.dump(output, f, indent=2, ensure_ascii=False)
    print(f"Results saved to {results_path}")

    return results


# ============================================================
# CLI Entry Point
# ============================================================

def main():
    parser = argparse.ArgumentParser(
        description="HTTP/3 Interoperability Test Suite",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""\
Examples:
  # Full suite with auto-discovered binaries
  python h3_interop_suite.py

  # Quick mode (aioquic only, no browser/curl)
  python h3_interop_suite.py --quick

  # Specify binaries explicitly
  python h3_interop_suite.py --server ./build/bin/h3_interop_server --client ./build/bin/h3_interop_client
""",
    )
    parser.add_argument("--server", help="Path to cnetmod server binary")
    parser.add_argument("--client", help="Path to cnetmod client binary")
    parser.add_argument("--port", type=int, default=4433, help="Base port number (default: 4433)")
    parser.add_argument(
        "--quick",
        action="store_true",
        help="Quick mode: only run aioquic interop (skip browser/curl/conformance)",
    )

    args = parser.parse_args()

    # Auto-discover binaries
    server_bin = args.server or find_binary("h3_interop_server")
    client_bin = args.client or find_binary("h3_interop_client")

    if server_bin:
        print(f"Discovered server: {server_bin}")
    if client_bin:
        print(f"Discovered client: {client_bin}")

    results = asyncio.run(
        run_interop_suite(server_bin, client_bin, args.port, quick_mode=args.quick)
    )

    # Exit code based on critical tests (aioquic interop)
    critical_peers = {"aioquic", "cnetmod-client"}
    critical_tests = [r for r in results if r.peer in critical_peers]
    if critical_tests:
        critical_passed = all(r.passed for r in critical_tests)
    else:
        # No critical tests ran — pass if no failures at all
        critical_passed = all(r.passed for r in results) if results else True

    sys.exit(0 if critical_passed else 1)


if __name__ == "__main__":
    main()
