import importlib.util
import pathlib
import sys
import threading
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer


module_path = pathlib.Path(sys.argv[1])
spec = importlib.util.spec_from_file_location("cnetmod_native", module_path)
module = importlib.util.module_from_spec(spec)
assert spec.loader is not None
spec.loader.exec_module(module)

assert module.version() == "2.0.0"
try:
    module.request("INVALID", "https://example.com/")
except ValueError:
    pass
else:
    raise AssertionError("invalid method must be rejected")
try:
    module.request("GET", "https://example.com/", version="invalid")
except ValueError:
    pass
else:
    raise AssertionError("invalid version preference must be rejected")


class Handler(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"

    def log_message(self, format, *args):
        pass

    def do_GET(self):
        if self.path != "/native":
            self.send_error(404)
            return
        payload = b"python-native-e2e"
        self.send_response(200)
        self.send_header("Content-Length", str(len(payload)))
        self.end_headers()
        self.wfile.write(payload)

    def do_POST(self):
        length = int(self.headers.get("Content-Length", "0"))
        payload = self.rfile.read(length)
        self.send_response(201)
        self.send_header("Content-Length", str(len(payload)))
        self.end_headers()
        self.wfile.write(payload)


server = ThreadingHTTPServer(("127.0.0.1", 0), Handler)
thread = threading.Thread(target=server.serve_forever, daemon=True)
thread.start()
try:
    endpoint = f"http://127.0.0.1:{server.server_port}"
    assert module.request("GET", endpoint + "/native", version="http1") == (
        200,
        b"python-native-e2e",
    )
    assert module.request("POST", endpoint + "/echo", b"request-body", version="http1") == (
        201,
        b"request-body",
    )
finally:
    server.shutdown()
    server.server_close()
    thread.join(timeout=5)
