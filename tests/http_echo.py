import sys
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

KEEP = ["Host", "User-Agent", "Accept", "Content-Type", "Content-Length",
        "X-Custom", "X-T"]

FULL = [b": heartbeat comment\r\n\r\n",
        b"id: 1\r\nevent: ping\r\ndata: first\r\n\r\n",
        b"data: line1\r\ndata: line2\r\n\r\n",
        b"data:no-space\r\n\r\n",
        b"retry: 5\r\ndata: last\r\n\r\n"]

SHORT = [b"data: bye\r\n\r\n"]


class H(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.0"

    def log_message(self, fmt, *a):
        pass

    def head(self):
        return "\n".join("%s: %s" % (k, self.headers[k]) for k in KEEP
                         if k in self.headers)

    def body(self):
        n = int(self.headers.get("Content-Length") or 0)
        return self.rfile.read(n) if n else b""

    def reply(self, data, ct="text/plain; charset=utf-8", extra=()):
        b = data.encode() if isinstance(data, str) else data
        self.send_response(200)
        self.send_header("Content-Type", ct)
        self.send_header("Content-Length", str(len(b)))
        self.send_header("X-Peek", "yes")
        for k, v in extra:
            self.send_header(k, v)
        self.end_headers()
        self.wfile.write(b)

    def stream(self, blocks, keep):
        self.send_response(200)
        self.send_header("Content-Type", "text/event-stream")
        self.end_headers()
        try:
            for b in blocks:
                self.wfile.write(b)
            self.wfile.flush()
            if not keep:
                return
            for _ in range(30):
                time.sleep(0.2)
                self.wfile.write(b": keep\r\n\r\n")
                self.wfile.flush()
        except Exception:
            pass

    def route(self):
        p = self.path.split("?")[0]
        if p == "/text":
            self.reply("plain-body")
        elif p == "/hdr":
            self.reply(self.head())
        elif p == "/json":
            self.reply('{"a":1,"b":[2,3]}', "application/json")
        elif p == "/raw":
            self.reply(self.body(), "application/octet-stream",
                       [("X-Req-CT", self.headers.get("Content-Type", ""))])
        elif p == "/fail":
            self.send_response(404)
            self.send_header("Content-Length", "9")
            self.end_headers()
            self.wfile.write(b"not-found")
        elif p == "/sse":
            self.stream(FULL, True)
        elif p == "/sseend":
            self.stream(SHORT, False)
        else:
            self.reply("root")

    def do_GET(self):
        self.route()

    def do_POST(self):
        self.route()

    def do_PUT(self):
        self.route()


def main():
    srv = ThreadingHTTPServer(("127.0.0.1", int(sys.argv[1])), H)
    sys.stdout.write("ready\n")
    sys.stdout.flush()
    srv.serve_forever()


main()
