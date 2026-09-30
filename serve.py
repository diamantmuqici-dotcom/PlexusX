#!/usr/bin/env python3
"""Static server for the ChromaX site.
Forces .exe files to download as Content-Disposition: attachment (so the
filename is exact, which the SHA-256 instructions depend on)."""
import os
import sys
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "site")


class Handler(SimpleHTTPRequestHandler):
    def __init__(self, *a, **kw):
        super().__init__(*a, directory=ROOT, **kw)

    def end_headers(self):
        path = self.path.split("?", 1)[0].lower()
        if path.endswith(".exe"):
            self.send_header("Content-Disposition", 'attachment; filename="ChromaX.exe"')
            self.send_header("Content-Type", "application/octet-stream")
        super().end_headers()

    def log_message(self, fmt, *args):
        sys.stderr.write("%s - %s\n" % (self.address_string(), fmt % args))


if __name__ == "__main__":
    port = int(sys.argv[1]) if len(sys.argv) > 1 else 8080
    ThreadingHTTPServer(("0.0.0.0", port), Handler).serve_forever()
