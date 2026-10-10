import os
from http.server import HTTPServer
from api.webhook import handler

if __name__ == "__main__":
    port = int(os.environ.get("PORT", "10000"))
    server = HTTPServer(("0.0.0.0", port), handler)
    print(f"TGJU Telegram webhook listening on port {port}", flush=True)
    server.serve_forever()
