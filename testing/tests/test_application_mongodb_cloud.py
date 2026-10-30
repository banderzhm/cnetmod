"""Validate Application startup/authentication/monitoring against a TLS wire peer."""
import os
from pathlib import Path
import socket
import ssl
import subprocess
import sys
import tempfile
import threading
import unittest
from test_mongodb_cloud_handshake import bson, decode, expected_signature, request, reply, sasl

DRIVER = os.environ.get("CNETMOD_MONGODB_APPLICATION_DRIVER")

class ApplicationCloud(unittest.TestCase):
    def run_case(self, mode):
        if not DRIVER:
            self.skipTest("set CNETMOD_MONGODB_APPLICATION_DRIVER")
        with tempfile.TemporaryDirectory(prefix="cnetmod-application-cloud-") as directory:
            root = Path(directory)
            cert, key, token = root / "cert.pem", root / "key.pem", root / "token"
            token.write_text("token-1")
            subprocess.run([DRIVER, "--write-test-certificate", str(cert), str(key)], check=True, capture_output=True)
            context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
            context.load_cert_chain(cert, key)
            if mode == "x509":
                context.load_verify_locations(cert)
                context.verify_mode = ssl.CERT_REQUIRED
            listener = socket.socket()
            listener.bind(("127.0.0.1", 0))
            listener.listen(16)
            listener.settimeout(.1)
            failures, workers, observations = [], [], []
            finished = threading.Event()
            hello = {"ok": 1.0, "minWireVersion": 0, "maxWireVersion": 25, "isWritablePrimary": True}
            def serve(peer):
                try:
                    with context.wrap_socket(peer, server_side=True) as stream:
                        stream.settimeout(5)
                        identifier, command = request(stream)
                        self.assertIn("hello", command)
                        reply(stream, identifier, hello)
                        identifier, command = request(stream)
                        if "hello" in command:
                            observations.append("monitor")
                            reply(stream, identifier, hello)
                            return
                        if mode == "aws":
                            self.assertEqual(command["mechanism"], "MONGODB-AWS")
                            first = decode(command["payload"])[0]
                            nonce = first["r"] + bytes(range(32))
                            host = "sts.us-east-1.amazonaws.com"
                            sasl(stream, identifier, done=False, payload=bson({"s": nonce, "h": host}))
                            identifier, command = request(stream)
                            signed = decode(command["payload"])[0]
                            self.assertEqual(signed["t"], "SESSION")
                            self.assertEqual(signed["a"], expected_signature(host, nonce, signed["d"]))
                            sasl(stream, identifier)
                        elif mode == "oidc":
                            self.assertEqual(command["mechanism"], "MONGODB-OIDC")
                            self.assertEqual(decode(command["payload"])[0], {"jwt": "token-1"})
                            sasl(stream, identifier)
                        else:
                            self.assertEqual(command["mechanism"], "MONGODB-X509")
                            self.assertTrue(stream.getpeercert())
                            reply(stream, identifier, {"ok": 1.0})
                        observations.append("authenticated")
                        pings = 0
                        while True:
                            identifier, command = request(stream)
                            self.assertIn("ping", command)
                            pings += 1
                            if mode == "oidc" and pings == 2:
                                token.write_text("token-2")
                                reply(stream, identifier, {"ok": 0.0, "code": 391})
                                identifier, command = request(stream)
                                self.assertEqual(command["mechanism"], "MONGODB-OIDC")
                                self.assertEqual(decode(command["payload"])[0], {"jwt": "token-2"})
                                sasl(stream, identifier)
                                observations.append("reauthenticated")
                                identifier, command = request(stream)
                                self.assertIn("ping", command)
                            reply(stream, identifier, {"ok": 1.0})
                            observations.append("ping")
                except EOFError:
                    pass
                except Exception as error:
                    failures.append(error)
            def accept():
                while not finished.is_set():
                    try:
                        peer, _ = listener.accept()
                    except socket.timeout:
                        continue
                    worker = threading.Thread(target=serve, args=(peer,), daemon=True)
                    workers.append(worker)
                    worker.start()
            accepting = threading.Thread(target=accept, daemon=True)
            accepting.start()
            env = dict(os.environ, AWS_ACCESS_KEY_ID="AKIDEXAMPLE",
                       AWS_SECRET_ACCESS_KEY="wJalrXUtnFEMI/K7MDENG+bPxRfiCYEXAMPLEKEY",
                       AWS_SESSION_TOKEN="SESSION", OIDC_TOKEN_FILE=str(token))
            try:
                process = subprocess.run([DRIVER, str(listener.getsockname()[1]), str(cert), str(key), mode],
                                         capture_output=True, text=True, timeout=20, env=env)
            finally:
                finished.set()
                accepting.join(2)
                listener.close()
                for worker in workers:
                    worker.join(6)
            self.assertEqual(process.returncode, 0, process.stdout + process.stderr)
            self.assertFalse(failures, repr(failures))
            self.assertIn("monitor", observations)
            self.assertIn("authenticated", observations)
            self.assertGreaterEqual(observations.count("ping"), 3)
            if mode == "oidc":
                self.assertIn("reauthenticated", observations)

    def test_aws_default_provider_through_application(self): self.run_case("aws")
    def test_oidc_workload_and_reauthentication_through_application(self): self.run_case("oidc")
    def test_x509_through_application(self): self.run_case("x509")

if __name__ == "__main__":
    if len(sys.argv) > 1:
        DRIVER = sys.argv.pop(1)
    unittest.main()
