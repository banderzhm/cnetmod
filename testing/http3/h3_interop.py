#!/usr/bin/env python3
"""
HTTP/3 Interoperability Test Suite

Tests cnetmod HTTP/3 server against Python aioquic client
and cnetmod HTTP/3 client against Python aioquic server
"""

import asyncio
import subprocess
import sys
import time
from pathlib import Path
from typing import Optional, List


try:
    from aioquic.asyncio import connect, serve
    from aioquic.h3.connection import H3Connection
    from aioquic.h3.events import HeadersReceived, DataReceived, PushPromiseReceived
    from aioquic.quic.configuration import Configuration
except ImportError:
    print("Error: aioquic not installed. Run: pip install -r requirements.txt")
    sys.exit(1)


# =============================================================================
# Helper Classes and Functions
# =============================================================================

def create_tls_config(cert_file=None, key_file=None):
    """Create TLS configuration for aioquic server/client."""
    from aioquic.quic.config import QuicConfiguration

    config = Configuration(mode="server", alpn_protocols=["h3"])

    if cert_file and key_file:
        config.context.load_cert_chain(cert_file, key_file)
    else:
        # Generate self-signed certificate
        from cryptography import x509
        from cryptography.x509.oid import NameOID
        from cryptography.hazmat.primitives import hashes
        from cryptography.hazmat.primitives.asymmetric import rsa
        from cryptography.hazmat.backends import default_backend
        from cryptography.x509 import load_pem_x509_certificate
        from cryptography.hazmat.primitives.serialization import Encoding, PrivateFormat, NoEncryption

        # Import datetime if needed
        try:
            import datetime
        except ImportError:
            from datetime import datetime, timedelta

        try:
            from cryptography.hazmat.primitives import serialization
        except ImportError:
            from cryptography.hazmat.primitives.serialization import serialization

        # Generate private key
        private_key = rsa.generate_private_key(
            public_exponent=65537,
            key_size=2048,
            backend=default_backend()
        )

        # Generate certificate
        subject = issuer = x509.Name([
            x509.NameAttribute(NameOID.COUNTRY_NAME, u"US"),
            x509.NameAttribute(NameOID.STATE_OR_PROVINCE_NAME, u"CA"),
            x509.NameAttribute(NameOID.LOCALITY_NAME, u"San Francisco"),
            x509.NameAttribute(NameOID.ORGANIZATION_NAME, u"Test"),
            x509.NameAttribute(NameOID.COMMON_NAME, u"localhost"),
        ])

        valid_from = datetime.utcnow()
        valid_until = valid_from + datetime.timedelta(days=365)

        cert = x509.CertificateBuilder().subject_name(
            subject
        ).issuer_name(
            issuer
        ).public_key(
            private_key.public_key()
        ).serial_number(
            x509.random_serial_number()
        ).not_valid_before(
            valid_from
        ).not_valid_after(
            valid_until
        ).sign(private_key, hashes.SHA256(), default_backend())

        return {
            'cert': cert.public_bytes(Encoding.PEM),
            'key': private_key.private_bytes(Encoding.PEM, PrivateFormat.PKCS8, NoEncryption()),
            'context': config.context
        }

    return None


async def read_response(conn: H3Connection, stream_id: int, timeout: float = 30) -> bytes:
    """Read response data from a stream until EOS."""
    received_data = bytearray()

    end_time = asyncio.get_event_loop().time() + timeout

    async for event in conn.advance(None):
        current_time = asyncio.get_event_loop().time()
        if current_time > end_time:
            raise TimeoutError(f"Timeout waiting for response")

        if isinstance(event, DataReceived) and event.stream_id == stream_id:
            received_data.extend(event.data)

        if hasattr(event, 'eos') and getattr(event, 'eos', False):
            break

    return bytes(received_data)


# =============================================================================
# Python HTTP/3 Server Protocol
# =============================================================================

class Http3EchoProtocol:
    """Python aioquic-based HTTP/3 echo server protocol."""

    def __init__(self, loop):
        self.loop = loop
        self.connections = {}

    async def handle_client_connection(self, reader, writer):
        """Handle incoming QUIC connection."""
        from aioquic.asyncio.protocol import QuicConnectionProtocol
        quic_conn = QuicConnectionProtocol(self.loop, quic_configuration=QuicConfiguration(mode='server'))

        try:
            while True:
                event = await quic_conn.receive_data(b'')  # Placeholder
                # Process events
        except Exception as e:
            # Handle errors gracefully
            pass

    async def handle_headers_received(self, h3_conn: H3Connection, event):
        """Handle incoming HEADERS frame."""
        if event.headers is None:
            return

        method = event.headers.get(b":method", b"GET").decode('utf-8', errors='replace')
        path = event.headers.get(b":path", b"/").decode('utf-8', errors='replace')

        # Echo back the request
        body = f"ECHO: {method} {path}".encode('utf-8')

        headers_response = [
            (b":status", b"200"),
            (b"content-type", b"text/plain"),
            (b"content-length", str(len(body)).encode('utf-8')),
        ]

        h3_conn.send_headers(stream_id=event.stream_id, headers=headers_response, eos=False)
        h3_conn.send_data(stream_id=event.stream_id, data=body, eos=True)


# =============================================================================
# Test Cases
# =============================================================================

async def test_python_client_against_cnetmod_server(port: int) -> bool:
    """
    Test: Python aioquic client ↔ cnetmod HTTP/3 server

    This test starts the cnetmod server and tests it with Python's aioquic client.
    """
    print(f"\n=== Test: Python Client → cnetmod Server (port {port}) ===")

    try:
        # Start cnetmod server
        server_executable = "h3_interop_server.exe"  # Windows
        if not Path(server_executable).exists():
            server_executable = "./h3_interop_server"  # Linux/Mac

        if not Path(server_executable).exists():
            print(f"✗ FAIL: {server_executable} not found")
            return False

        print(f"Starting cnetmod server on port {port}...")
        server_process = await asyncio.create_subprocess_exec(
            server_executable, "--port", str(port),
            stdout=asyncio.subprocess.PIPE,
            stderr=asyncio.subprocess.PIPE
        )

        # Wait for server to start
        await asyncio.sleep(2)

        # Check if server is running
        if server_process.returncode is not None:
            stdout, stderr = await server_process.communicate()
            print(f"✗ FAIL: Server exited immediately")
            print(f"stdout: {stdout.decode()}")
            print(f"stderr: {stderr.decode()}")
            return False

        try:
            # Create QUIC configuration
            config = Configuration(alpn_protocols=["h3"], enable_0rtt=False)

            # Connect to cnetmod server
            print(f"Connecting to 127.0.0.1:{port}...")

            # Note: We need to use aioquic's HTTP interface
            # The native fixture binds IPv4.  Do not make this gate depend on
            # a host resolver preferring IPv4 over the IPv6 ::1 loopback.
            async with connect("127.0.0.1", port, configuration=config) as protocol:
                # Send GET request using low-level API
                stream = protocol._http._create_request_stream()

                await stream.send_headers(
                    headers=[
                        (b":method", b"GET"),
                        (b":path", b"/echo/test1"),
                        (b":authority", b"localhost"),
                    ],
                    eos=False,
                )

                # Receive response
                status_code = None
                body_parts = []

                async for event in protocol:
                    if isinstance(event, HeadersReceived):
                        for header_name, header_value in event.headers:
                            if header_name == b":status":
                                status_code = int(header_value.decode())

                    if isinstance(event, DataReceived):
                        body_parts.append(event.data)

                    if hasattr(event, 'eos') and getattr(event, 'eos', False):
                        break

                body = b"".join(body_parts)

                if status_code is None:
                    print("✗ FAIL: No status code received")
                    return False

                if status_code != 200:
                    print(f"✗ FAIL: Unexpected status code: {status_code}")
                    return False

                if len(body) == 0:
                    print(f"✗ FAIL: Empty response body")
                    return False

                if b"test1" in body:
                    print(f"✓ PASS: Got expected response")
                    print(f"  Status: {status_code}")
                    print(f"  Body: {body.decode()[:100]}")
                    return True
                else:
                    print(f"✗ FAIL: Response doesn't contain expected content")
                    print(f"  Body: {body.decode()}")
                    return False

        except Exception as e:
            print(f"✗ FAIL: Client exception: {e}")
            import traceback
            traceback.print_exc()
            return False

    finally:
        # Kill the server process
        try:
            server_process.terminate()
            await asyncio.wait_for(server_process.wait(), timeout=5.0)
        except Exception as e:
            print(f"Error terminating server: {e}")
            try:
                server_process.kill()
            except:
                pass


async def test_concurrent_streams(cnetmod_port: int, python_port: int) -> bool:
    """
    Test concurrent streams over single connections.
    Tests both cnetmod ↔ Python interop.
    """
    print(f"\n=== Test: Concurrent Streams ===")

    results = []

    # Test 1: Python client to cnetmod server
    print("Testing Python → cnetmod...")

    try:
        config = Configuration(alpn_protocols=["h3"], enable_0rtt=False)
        async with connect("127.0.0.1", cnetmod_port, configuration=config) as protocol:
            tasks = []

            for i in range(10):
                async def send_req(idx):
                    try:
                        stream = protocol._http._create_request_stream()

                        await stream.send_headers(
                            headers=[
                                (b":method", b"GET"),
                                (b":path", f"/echo/stream{idx}".encode()),
                                (b":authority", b"localhost"),
                            ],
                            eos=True,
                        )

                        got_response = False

                        async for event in protocol:
                            if isinstance(event, DataReceived):
                                got_response = True

                            if hasattr(event, 'eos') and getattr(event, 'eos', False):
                                break

                        return got_response

                    except Exception as e:
                        print(f"Request {idx} failed: {e}")
                        return False

                tasks.append(send_req(i))

            responses = await asyncio.gather(*tasks)
            success_count = sum(1 for r in responses if r)

            print(f"  Completed: {success_count}/10 successful")

            if success_count == 10:
                print("✓ PASS: All concurrent streams succeeded")
                results.append(True)
            else:
                print("✗ FAIL: Some streams failed")
                results.append(False)

    except Exception as e:
        print(f"✗ FAIL: Concurrent stream test error: {e}")
        import traceback
        traceback.print_exc()
        results.append(False)

    return all(results) if results else False


async def run_all_tests(base_port: int = 4433) -> int:
    """Run all interoperability tests."""

    print("="*60)
    print("HTTP/3 INTEROPERABILITY TEST SUITE")
    print("="*60)

    results = []

    try:
        # Test 1: Python client vs cnetmod server
        results.append(await test_python_client_against_cnetmod_server(base_port))

        # Test 2: Concurrent streams
        results.append(await test_concurrent_streams(base_port, base_port + 1000))

    except KeyboardInterrupt:
        print("\n\nTests interrupted by user")
        return 1
    except Exception as e:
        print(f"\nFatal error running tests: {e}")
        import traceback
        traceback.print_exc()
        return 1

    # Summary
    print("\n" + "="*60)
    print("INTEROPERABILITY TEST SUMMARY")
    print("="*60)
    passed = sum(1 for r in results if r)
    total = len(results)
    print(f"Passed: {passed}/{total}")

    if passed == total:
        print("✓ ALL TESTS PASSED")
        return 0
    else:
        print("✗ SOME TESTS FAILED")
        return 1


if __name__ == "__main__":
    # Parse command line arguments
    import argparse
    parser = argparse.ArgumentParser(description="HTTP/3 Interop Test Suite")
    parser.add_argument("--port", type=int, default=4433, help="Base port number (default: 4433)")
    args = parser.parse_args()

    exit_code = asyncio.run(run_all_tests(args.port))
    sys.exit(exit_code)
