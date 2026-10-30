"""Independent TLS peer validates complete MongoDB cloud SASL conversations."""
import base64
import hashlib
import hmac
import os
from pathlib import Path
import socket
import ssl
import struct
import subprocess
import sys
import tempfile
import threading
import unittest

DRIVER = os.environ.get("CNETMOD_MONGODB_CLOUD_HANDSHAKE_DRIVER")

def bson(document):
    result = bytearray()
    for key, value in document.items():
        name = key.encode() + b"\0"
        if isinstance(value, bool): result += b"\x08" + name + bytes([value])
        elif isinstance(value, int): result += b"\x10" + name + struct.pack("<i", value)
        elif isinstance(value, float): result += b"\x01" + name + struct.pack("<d", value)
        elif isinstance(value, str):
            text = value.encode() + b"\0"; result += b"\x02" + name + struct.pack("<i", len(text)) + text
        elif isinstance(value, bytes): result += b"\x05" + name + struct.pack("<i", len(value)) + b"\0" + value
        elif isinstance(value, dict): result += b"\x03" + name + bson(value)
        else: raise TypeError(type(value))
    return struct.pack("<i", len(result) + 5) + result + b"\0"

def decode(data):
    size = struct.unpack_from("<i", data)[0]
    assert 5 <= size <= len(data)
    output, pos = {}, 4
    while pos < size - 1:
        kind = data[pos]; pos += 1
        end = data.index(b"\0", pos); key = data[pos:end].decode(); pos = end + 1
        if kind == 1: value = struct.unpack_from("<d", data, pos)[0]; pos += 8
        elif kind == 16: value = struct.unpack_from("<i", data, pos)[0]; pos += 4
        elif kind == 18: value = struct.unpack_from("<q", data, pos)[0]; pos += 8
        elif kind == 8: value = bool(data[pos]); pos += 1
        elif kind == 2:
            length = struct.unpack_from("<i", data, pos)[0]; pos += 4
            value = data[pos:pos+length-1].decode(); pos += length
        elif kind in (3, 4): value, length = decode(data[pos:]); pos += length
        elif kind == 5:
            length = struct.unpack_from("<i", data, pos)[0]; pos += 4
            assert data[pos] == 0; pos += 1; value = data[pos:pos+length]; pos += length
        else: raise AssertionError(f"unexpected BSON tag {kind}")
        assert key not in output; output[key] = value
    assert pos == size - 1 and data[pos] == 0
    return output, size

def exact(connection, count):
    data = bytearray()
    while len(data) < count:
        part = connection.recv(count - len(data))
        if not part: raise EOFError()
        data += part
    return bytes(data)

def request(connection):
    head = exact(connection, 16)
    size, identifier, _, opcode = struct.unpack("<iiii", head)
    assert opcode == 2013 and 21 <= size <= 1048576
    payload = exact(connection, size - 16)
    assert payload[:5] == b"\0" * 5
    return identifier, decode(payload[5:])[0]

def reply(connection, request_id, document, server_id=100):
    payload = struct.pack("<I", 0) + b"\0" + bson(document)
    connection.sendall(struct.pack("<iiii", len(payload) + 16, server_id, request_id, 2013) + payload)

def sasl(connection, identifier, conversation=7, done=True, payload=b""):
    reply(connection, identifier, {"ok": 1.0, "conversationId": conversation, "done": done, "payload": payload})

def expected_signature(host, nonce, date):
    headers = {"content-length":"43", "content-type":"application/x-www-form-urlencoded", "host":host,
        "x-amz-date":date, "x-amz-security-token":"SESSION", "x-mongodb-gs2-cb-flag":"n",
        "x-mongodb-server-nonce":base64.b64encode(nonce).decode()}
    names = ";".join(sorted(headers))
    canonical = "".join(f"{name}:{headers[name]}\n" for name in sorted(headers))
    digest = lambda value: hashlib.sha256(value.encode()).hexdigest()
    canonical_request = "POST\n/\n\n" + canonical + "\n" + names + "\n" + digest("Action=GetCallerIdentity&Version=2011-06-15")
    scope = date[:8] + "/us-east-1/sts/aws4_request"
    to_sign = "AWS4-HMAC-SHA256\n" + date + "\n" + scope + "\n" + digest(canonical_request)
    key = b"AWS4wJalrXUtnFEMI/K7MDENG+bPxRfiCYEXAMPLEKEY"
    for step in (date[:8], "us-east-1", "sts", "aws4_request"):
        key = hmac.new(key, step.encode(), hashlib.sha256).digest()
    signature = hmac.new(key, to_sign.encode(), hashlib.sha256).hexdigest()
    return f"AWS4-HMAC-SHA256 Credential=AKIDEXAMPLE/{scope}, SignedHeaders={names}, Signature={signature}"

class CloudHandshake(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        if not DRIVER: raise unittest.SkipTest("set CNETMOD_MONGODB_CLOUD_HANDSHAKE_DRIVER")
        cls.directory = tempfile.TemporaryDirectory(prefix="cnetmod-cloud-tls-")
        root = Path(cls.directory.name); cls.cert = root / "cert.pem"; cls.key = root / "key.pem"
        subprocess.run([DRIVER,"--write-test-certificate",str(cls.cert),str(cls.key)],check=True,capture_output=True)
    @classmethod
    def tearDownClass(cls): cls.directory.cleanup()

    def run_case(self, mode, expect_failure=False):
        context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
        context.load_cert_chain(self.cert, self.key)
        if mode == "x509":
            context.load_verify_locations(self.cert); context.verify_mode = ssl.CERT_REQUIRED
        listener = socket.socket(); listener.bind(("127.0.0.1", 0)); listener.listen(4); listener.settimeout(15)
        port = listener.getsockname()[1]; failures = []
        def server():
            try:
                rounds = 2 if mode in ("oidc_reauth", "oidc_cached_retry") else 1
                for round_ in range(rounds):
                    stream, _ = listener.accept()
                    with context.wrap_socket(stream, server_side=True) as connection:
                        connection.settimeout(10)
                        identifier, hello = request(connection)
                        self.assertEqual(hello["hello"], 1)
                        response = {"ok":1.0,"minWireVersion":0,"maxWireVersion":25,"isWritablePrimary":True}
                        if round_ and mode == "oidc_reauth":
                            speculative = hello["speculativeAuthenticate"]
                            self.assertEqual(decode(speculative["payload"])[0], {"jwt":"token-2"})
                            response["speculativeAuthenticate"] = {"ok":1.0,"conversationId":7,"done":True,"payload":b""}
                        if round_ and mode == "oidc_cached_retry":
                            response["speculativeAuthenticate"] = {"ok":0.0,"code":18,"conversationId":7,"done":True,"payload":b""}
                        reply(connection, identifier, response)
                        if mode.startswith("aws"):
                            identifier, first = request(connection)
                            self.assertEqual(first["mechanism"], "MONGODB-AWS"); self.assertEqual(first["$db"], "$external")
                            client = decode(first["payload"])[0]; self.assertEqual(client["p"], 110); self.assertEqual(len(client["r"]),32)
                            nonce = client["r"] + bytes(range(32))
                            if mode == "aws_bad_nonce": nonce = bytes(64)
                            host = "sts..amazonaws.com" if mode == "aws_bad_host" else "sts.us-east-1.amazonaws.com"
                            sasl(connection, identifier, done=False, payload=bson({"s":nonce,"h":host}))
                            if expect_failure and mode != "aws_wrong_conversation":
                                with self.assertRaises(EOFError): request(connection)
                                continue
                            identifier, final = request(connection); signed = decode(final["payload"])[0]
                            self.assertEqual(final["conversationId"],7); self.assertEqual(signed["t"],"SESSION")
                            self.assertEqual(signed["a"], expected_signature(host, nonce, signed["d"]))
                            sasl(connection, identifier, conversation=8 if mode == "aws_wrong_conversation" else 7)
                            if expect_failure:
                                with self.assertRaises(EOFError): request(connection)
                                continue
                        elif mode == "x509":
                            self.assertTrue(connection.getpeercert())
                            identifier, authenticate = request(connection)
                            self.assertEqual(authenticate["mechanism"],"MONGODB-X509")
                            self.assertEqual(authenticate["$db"],"$external"); reply(connection,identifier,{"ok":1.0})
                        elif not (round_ and mode == "oidc_reauth"):
                            identifier, start = request(connection)
                            self.assertEqual(start["mechanism"],"MONGODB-OIDC"); self.assertEqual(start["$db"],"$external")
                            self.assertEqual(decode(start["payload"])[0],{"jwt":"token-1"})
                            if round_ and mode == "oidc_cached_retry":
                                reply(connection,identifier,{"ok":0.0,"code":18,"errmsg":"cached token rejected"})
                                identifier, start = request(connection)
                                self.assertEqual(decode(start["payload"])[0],{"jwt":"token-2"})
                            sasl(connection,identifier)
                        identifier, ping = request(connection); self.assertEqual(ping["ping"],1)
                        if round_ == 0 and mode in ("oidc_reauth","oidc_twice_391","oidc_reauth_denied"):
                            reply(connection,identifier,{"ok":0.0,"code":391,"errmsg":"reauthenticate"})
                            identifier, start = request(connection)
                            self.assertIn("saslStart",start); self.assertNotIn("hello",start)
                            self.assertEqual(decode(start["payload"])[0],{"jwt":"token-2"})
                            if mode == "oidc_reauth_denied":
                                reply(connection,identifier,{"ok":0.0,"code":18,"errmsg":"fresh token rejected"})
                                with self.assertRaises(EOFError): request(connection)
                                continue
                            sasl(connection,identifier)
                            identifier, retried = request(connection); self.assertEqual(retried,ping)
                            if mode == "oidc_twice_391":
                                reply(connection,identifier,{"ok":0.0,"code":391,"errmsg":"still reauthenticate"})
                                with self.assertRaises(EOFError): request(connection)
                                continue
                        reply(connection,identifier,{"ok":1.0})
                        with self.assertRaises(EOFError): request(connection)
            except BaseException as error: failures.append(error)
            finally: listener.close()
        thread = threading.Thread(target=server,daemon=True); thread.start()
        try:
            process = subprocess.run([DRIVER,str(port),str(self.cert),str(self.key),mode,"failure" if expect_failure else "success"],capture_output=True,text=True,timeout=20)
            self.assertEqual(process.returncode,0,process.stdout+process.stderr)
        finally:
            thread.join(2); listener.close()
        self.assertFalse(thread.is_alive(),"TLS peer did not finish")
        if failures: raise failures[0]

    def test_aws_signature_conversation(self): self.run_case("aws")
    def test_aws_rejects_nonce_mismatch(self): self.run_case("aws_bad_nonce",True)
    def test_aws_rejects_invalid_host(self): self.run_case("aws_bad_host",True)
    def test_aws_rejects_conversation_change(self): self.run_case("aws_wrong_conversation",True)
    def test_oidc_reauth_and_speculation(self): self.run_case("oidc_reauth")
    def test_oidc_cached_auth_failure_fetches_once(self): self.run_case("oidc_cached_retry")
    def test_oidc_second_391_is_not_retried(self): self.run_case("oidc_twice_391")
    def test_oidc_reauth_failure_retires_connection(self): self.run_case("oidc_reauth_denied")
    def test_x509_certificate_authentication(self): self.run_case("x509")

if __name__ == "__main__":
    if len(sys.argv) > 1: DRIVER = sys.argv.pop(1)
    unittest.main()
