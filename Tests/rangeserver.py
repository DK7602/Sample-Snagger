import http.server, os, re, sys
class H(http.server.SimpleHTTPRequestHandler):
    def send_head(self):
        path = self.translate_path(self.path)
        if not os.path.isfile(path):
            self.send_error(404, "File not found"); return None
        size = os.path.getsize(path)
        rng = self.headers.get("Range")
        f = open(path, "rb")
        if rng:
            m = re.match(r"bytes=(\d*)-(\d*)", rng)
            start = int(m.group(1) or 0); end = int(m.group(2)) if m.group(2) else size - 1
            end = min(end, size - 1)
            self.send_response(206)
            self.send_header("Content-Range", f"bytes {start}-{end}/{size}")
            self.send_header("Content-Length", str(end - start + 1))
            f.seek(start); self._remaining = end - start + 1
        else:
            self.send_response(200); self.send_header("Content-Length", str(size)); self._remaining = size
        self.send_header("Content-Type", self.guess_type(path)); self.send_header("Accept-Ranges", "bytes"); self.end_headers()
        return f
    def copyfile(self, src, dst):
        left = getattr(self, "_remaining", None)
        while left is None or left > 0:
            chunk = src.read(65536 if left is None else min(65536, left))
            if not chunk: break
            dst.write(chunk)
            if left is not None: left -= len(chunk)
    def log_message(self, *a): pass
os.chdir(sys.argv[2]); http.server.ThreadingHTTPServer(("127.0.0.1", int(sys.argv[1])), H).serve_forever()
