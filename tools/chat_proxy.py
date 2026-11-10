import argparse
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.error import HTTPError, URLError
from urllib.request import Request, urlopen


class ProxyHandler(BaseHTTPRequestHandler):
    esp32_url = ""

    def do_GET(self):
        target = self.esp32_url + self.path
        try:
            with urlopen(Request(target, method="GET"), timeout=10) as response:
                body = response.read()
                status = response.status
                content_type = response.headers.get("Content-Type", "text/html")
        except HTTPError as error:
            body = error.read()
            status = error.code
            content_type = error.headers.get("Content-Type", "text/plain")
        except URLError as error:
            message = (
                "Khong ket noi duoc ESP32 tai "
                + self.esp32_url
                + ": "
                + str(error.reason)
            )
            body = message.encode("utf-8")
            status = 502
            content_type = "text/plain; charset=utf-8"

        self.send_response(status)
        self.send_header("Content-Type", content_type)
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def log_message(self, format_string, *args):
        print(format_string % args)


def main():
    parser = argparse.ArgumentParser(description="Local microphone proxy for ESP32 Chat")
    parser.add_argument("--esp32", default="http://192.168.1.42")
    parser.add_argument("--port", type=int, default=8080)
    args = parser.parse_args()

    ProxyHandler.esp32_url = args.esp32.rstrip("/")
    server = ThreadingHTTPServer(("127.0.0.1", args.port), ProxyHandler)
    print("Mo http://127.0.0.1:%d tren trinh duyet." % args.port)
    print("Chuyen tiep den %s" % ProxyHandler.esp32_url)
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        print("\nDa dung proxy.")
    finally:
        server.server_close()


if __name__ == "__main__":
    main()
