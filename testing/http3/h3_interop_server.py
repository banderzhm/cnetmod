#!/usr/bin/env python3
"""
Simple HTTP/3 Echo Server for Testing cnetmod Client

This is a minimal server using aioquic that echoes back requests.
Used to test interoperability with cnetmod's HTTP/3 client.
"""

import asyncio
import argparse
import sys
from typing import Optional, List, Tuple

try:
    from aioquic.asyncio import serve
    from aioquic.h3.connection import H3Connection
    from aioquic.h3.events import HeadersReceived, DataReceived
    from aioquic.quic.configuration import Configuration
except ImportError:
    print("Error: aioquic not installed. Run: pip install -r requirements.txt")
    sys.exit(1)


class Http3EchoProtocol:
    """A simple HTTP/3 echo protocol handler."""

    def __init__(self):
        self.connections = {}

    async def process_headers(self, h3_conn: H3Connection, event):
        """Process incoming HTTP/3 HEADERS and send echo response."""

        if event.headers is None:
            return

        # Extract request information
        method = event.headers.get(b":method", b"GET").decode('utf-8', errors='replace')
        path = event.headers.get(b":path", b"/").decode('utf-8', errors='replace')
        authority = event.headers.get(b":authority", b"localhost").decode('utf-8', errors='replace')

        # Prepare echo response
        echo_text = f"ECHO: {method} {path}"

        headers_response = [
            (b":status", b"200"),
            (b"content-type", b"text/plain"),
            (b"content-length", str(len(echo_text)).encode('utf-8')),
            (b"x-request-method", method.encode('utf-8')),
            (b"x-request-path", path.encode('utf-8')),
        ]

        try:
            # Send headers response
            h3_conn.send_headers(
                stream_id=event.stream_id,
                headers=headers_response,
                eos=False
            )

            # Send body
            h3_conn.send_data(
                stream_id=event.stream_id,
                data=echo_text.encode('utf-8'),
                eos=True
            )

            # For POST requests, also read the body
            if method.upper() == "POST":
                received_body = bytearray()

                while True:
                    async for ev in h3_conn.advance(None):
                        if isinstance(ev, DataReceived):
                            received_body.extend(ev.data)

                        if hasattr(ev, 'eos') and getattr(ev, 'eos', False):
                            break

                if received_body:
                    # Update response with body info
                    echo_with_body = f"POST ECHO: length={len(received_body)}"

                    h3_conn.send_headers(
                        stream_id=event.stream_id,
                        headers=[
                            (b":status", b"200"),
                            (b"content-type", b"text/plain"),
                            (b"content-length", str(len(echo_with_body)).encode('utf-8')),
                        ],
                        eos=True
                    )

                    h3_conn.send_data(
                        stream_id=event.stream_id,
                        data=echo_with_body.encode('utf-8'),
                        eos=True
                    )

        except Exception as e:
            print(f"Error handling headers: {e}")
            import traceback
            traceback.print_exc()

            # Send error response
            try:
                h3_conn.send_headers(
                    stream_id=event.stream_id,
                    headers=[
                        (b":status", b"500"),
                        (b"content-type", b"text/plain"),
                    ],
                    eos=True
                )
                h3_conn.send_data(
                    stream_id=event.stream_id,
                    data=f"Server error: {e}".encode('utf-8'),
                    eos=True
                )
            except:
                pass


async def handle_connection(reader, writer):
    """Handle an incoming QUIC connection."""
    from aioquic.asyncio.protocol import QuicConnectionProtocol

    # Create QUIC connection
    config = Configuration(mode='server', alpn_protocols=['h3'])

    # Generate self-signed certificate for testing
    try:
        from cryptography import x509
        from cryptography.x509.oid import NameOID
        from cryptography.hazmat.primitives import hashes
        from cryptography.hazmat.primitives.asymmetric import rsa
        from cryptography.hazmat.backends import default_backend

        # Import datetime
        try:
            from datetime import datetime, timedelta
        except ImportError:
            import datetime

        try:
            from cryptography.hazmat.primitives import serialization
        except ImportError:
            pass

        # Generate key pair
        private_key = rsa.generate_private_key(
            public_exponent=65537,
            key_size=2048,
            backend=default_backend()
        )

        # Generate cert
        subject = issuer = x509.Name([
            x509.NameAttribute(NameOID.COUNTRY_NAME, u"US"),
            x509.NameAttribute(NameOID.STATE_OR_PROVINCE_NAME, u"CA"),
            x509.NameAttribute(NameOID.LOCALITY_NAME, u"San Francisco"),
            x509.NameAttribute(NameOID.ORGANIZATION_NAME, u"Test"),
            x509.NameAttribute(NameOID.COMMON_NAME, u"localhost"),
        ])

        valid_from = datetime.utcnow()
        valid_until = valid_from + timedelta(days=365)

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

        config.context.load_cert_chain(
            cert_pem=cert.public_bytes(serialization.Encoding.PEM),
            keyfile_pem=private_key.private_bytes(
                serialization.Encoding.PEM,
                serialization.PrivateFormat.PKCS8,
                serialization.NoEncryption()
            )
        )

    except ImportError:
        # If cryptography not available, skip TLS setup
        pass

    quic_protocol = QuicConnectionProtocol(
        asyncio.get_event_loop(),
        quic_configuration=config
    )

    # Attach our handler
    quic_protocol._http._protocol = Http3EchoProtocol()

    try:
        async for frame in reader:
            await quic_protocol.receive_frame(frame)

            # Process events until empty queue
            processed = True
            while processed:
                try:
                    event = await quic_protocol.advance(None)
                    processed = True

                except StopAsyncIteration:
                    processed = False

    except Exception as e:
        print(f"Connection error: {e}")
    finally:
        try:
            writer.close()
            await writer.wait_closed()
        except:
            pass


def main():
    """Main entry point."""
    parser = argparse.ArgumentParser(description="HTTP/3 Echo Server")
    parser.add_argument("--port", type=int, default=4433, help="Port to listen on")
    parser.add_argument("--host", default="localhost", help="Host to bind to")
    args = parser.parse_args()

    print(f"Starting HTTP/3 Echo Server on port {args.port}...")
    print("Endpoints:")
    print("  GET /echo/path -> Returns 'ECHO: GET /echo/path'")
    print("  POST /echo     -> Returns 'POST ECHO: length=N'")

    try:
        asyncio.run(
            serve(args.host, args.port, create_protocol=lambda r, w: handle_connection(r, w))
        )
    except KeyboardInterrupt:
        print("\nServer stopped by user")
    except Exception as e:
        print(f"Server error: {e}")
        import traceback
        traceback.print_exc()
        return 1

    return 0


if __name__ == "__main__":
    sys.exit(main())
