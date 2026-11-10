import json
import os
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from io import BytesIO
import wave

from faster_whisper import WhisperModel


HOST = "0.0.0.0"
PORT = int(os.environ.get("PORT", "8787"))
MODEL_SIZE = os.environ.get("WHISPER_MODEL", "small")
model = WhisperModel(MODEL_SIZE, device="cpu", compute_type="int8")


def json_response(handler: BaseHTTPRequestHandler, status: int, body: dict[str, object]) -> None:
    payload = json.dumps(body, ensure_ascii=False).encode("utf-8")
    handler.send_response(status)
    handler.send_header("Content-Type", "application/json; charset=utf-8")
    handler.send_header("Content-Length", str(len(payload)))
    handler.end_headers()
    handler.wfile.write(payload)


class WhisperHandler(BaseHTTPRequestHandler):
    def do_GET(self) -> None:
        if self.path == "/health":
            json_response(self, 200, {"ok": True, "model": MODEL_SIZE})
            return
        json_response(self, 404, {"error": "Not found."})

    def do_POST(self) -> None:
        if self.path != "/transcribe":
            json_response(self, 404, {"error": "Not found."})
            return

        content_length = int(self.headers.get("Content-Length", "0"))
        if content_length <= 0 or content_length > 512 * 1024:
            json_response(self, 400, {"error": "Invalid audio payload size."})
            return

        pcm = self.rfile.read(content_length)
        wav_buffer = BytesIO()
        with wave.open(wav_buffer, "wb") as wav_file:
            wav_file.setnchannels(1)
            wav_file.setsampwidth(2)
            wav_file.setframerate(16000)
            wav_file.writeframes(pcm)

        try:
            segments, _ = model.transcribe(
                BytesIO(wav_buffer.getvalue()),
                language="vi",
                vad_filter=True,
                beam_size=5,
            )
            text = " ".join(segment.text.strip() for segment in segments).strip()
            if not text:
                json_response(self, 422, {"error": "No speech detected."})
                return
            json_response(self, 200, {"text": text})
        except Exception as error:
            print(f"Whisper transcription failed: {error}")
            json_response(self, 502, {"error": "Transcription failed."})

    def log_message(self, format_string: str, *args: object) -> None:
        print(format_string % args)


if __name__ == "__main__":
    print(f"Whisper ASR server listening on http://{HOST}:{PORT}")
    ThreadingHTTPServer((HOST, PORT), WhisperHandler).serve_forever()
