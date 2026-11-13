const http = require("http");

const port = Number(process.env.PORT || 8787);
const apiKey = process.env.GEMINI_API_KEY;
const model = process.env.GEMINI_MODEL || "gemini-2.0-flash";
const maxBodySize = 512 * 1024;

function sendJson(response, status, body) {
  const payload = JSON.stringify(body);
  response.writeHead(status, {
    "Content-Type": "application/json; charset=utf-8",
    "Content-Length": Buffer.byteLength(payload),
  });
  response.end(payload);
}

function readBody(request) {
  return new Promise((resolve, reject) => {
    const chunks = [];
    let size = 0;
    request.on("data", (chunk) => {
      size += chunk.length;
      if (size > maxBodySize) {
        reject(new Error("Audio payload is too large."));
        request.destroy();
        return;
      }
      chunks.push(chunk);
    });
    request.on("end", () => resolve(Buffer.concat(chunks)));
    request.on("error", reject);
  });
}

async function transcribe(audio) {
  const endpoint =
    `https://generativelanguage.googleapis.com/v1beta/models/${model}:generateContent` +
    `?key=${encodeURIComponent(apiKey)}`;
  const wav = Buffer.alloc(44 + audio.length);
  wav.write("RIFF", 0);
  wav.writeUInt32LE(36 + audio.length, 4);
  wav.write("WAVEfmt ", 8);
  wav.writeUInt32LE(16, 16);
  wav.writeUInt16LE(1, 20);
  wav.writeUInt16LE(1, 22);
  wav.writeUInt32LE(16000, 24);
  wav.writeUInt32LE(32000, 28);
  wav.writeUInt16LE(2, 32);
  wav.writeUInt16LE(16, 34);
  wav.write("data", 36);
  wav.writeUInt32LE(audio.length, 40);
  audio.copy(wav, 44);
  const requestBody = {
    contents: [{
      parts: [
        {
          text:
            "Nghe am thanh tieng Viet. Tra ve dung 2 dong:\n" +
            "USER: <noi dung nguoi dung vua noi>\n" +
            "AI: [cam_xuc:<nhan>] <cau tra loi ngan gon than thien duoi 20 tu>\n" +
            "Trong do <nhan> thuoc: vui, buon, gian, batngo, yeu, so, phan_khich, binh_yen, boi_roi, xau_ho, tu_hao, co_don, hy_vong, biet_on, chan, lo_lang, nhe_nhom, to_mo, quyet_tam, dau, cuoi.",
        },
        {
          inline_data: {
            mime_type: "audio/wav",
            data: wav.toString("base64"),
          },
        },
      ],
    }],
    generationConfig: { temperature: 0.4 },
  };
  const response = await fetch(endpoint, {
    method: "POST",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify(requestBody),
  });
  const result = await response.json();
  if (!response.ok) {
    throw new Error(result.error?.message || `Gemini HTTP ${response.status}`);
  }
  const raw = result.candidates?.[0]?.content?.parts
    ?.map((part) => part.text || "")
    .join("")
    .trim();
  if (!raw) {
    throw new Error("Gemini returned no transcript.");
  }
  const userMatch = raw.match(/USER:\s*(.+)/i);
  const aiMatch = raw.match(/AI:\s*(.+)/i);
  return {
    text: userMatch ? userMatch[1].trim() : raw,
    reply: aiMatch ? aiMatch[1].trim() : "",
  };
}

const server = http.createServer(async (request, response) => {
  if (request.method === "GET" && request.url === "/health") {
    sendJson(response, 200, { ok: true });
    return;
  }
  if (request.method !== "POST" || request.url !== "/transcribe") {
    sendJson(response, 404, { error: "Not found." });
    return;
  }
  if (!apiKey) {
    sendJson(response, 500, { error: "GEMINI_API_KEY is not configured." });
    return;
  }
  try {
    const audio = await readBody(request);
    if (audio.length === 0) {
      sendJson(response, 400, { error: "Audio payload is empty." });
      return;
    }
    const result = await transcribe(audio);
    sendJson(response, 200, result);
  } catch (error) {
    console.error(error.message);
    sendJson(response, 502, { error: error.message });
  }
});

server.listen(port, "0.0.0.0", () => {
  console.log(`Gemini ASR server listening on http://0.0.0.0:${port}`);
});
