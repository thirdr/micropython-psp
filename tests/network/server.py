#!/usr/bin/env python3
"""Test server for tests/network, run on the host next to PPSSPPHeadless
(whose emulated sockets are the host's): HTTP on 8765 (GET /hello,
POST /echo) and a TCP echo server on 8766. Stdlib only.

    python3 tests/network/server.py &      # stops itself after 10 minutes
"""
import http.server
import socketserver
import threading
import time


class Handler(http.server.BaseHTTPRequestHandler):
    def _reply(self, body, kind="text/plain"):
        self.send_response(200)
        self.send_header("Content-Type", kind)
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def do_GET(self):
        if self.path == "/hello":
            self._reply(b"hello from the host\n")
        else:
            self.send_error(404)

    def do_POST(self):
        body = self.rfile.read(int(self.headers.get("Content-Length", 0)))
        self._reply(body, self.headers.get("Content-Type", "application/octet-stream"))

    def log_message(self, *args):
        pass


class Echo(socketserver.BaseRequestHandler):
    def handle(self):
        while True:
            data = self.request.recv(1024)
            if not data:
                return
            self.request.sendall(data)


class Threads(socketserver.ThreadingMixIn, socketserver.TCPServer):
    allow_reuse_address = True
    daemon_threads = True


http_server = Threads(("127.0.0.1", 8765), Handler)
echo_server = Threads(("127.0.0.1", 8766), Echo)
for server in (http_server, echo_server):
    threading.Thread(target=server.serve_forever, daemon=True).start()
print("network test server: http 8765, echo 8766", flush=True)
time.sleep(600)
