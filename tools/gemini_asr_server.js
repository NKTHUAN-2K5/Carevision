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
  const requestBody = {
    contents: [{
      parts: [
        {
          text: "Chuyen am thanh tieng Viet nay thanh van ban. Chi tra ve dung cau noi, khong giai thich.",
        },
        {
          inline_data: {
            mime_type: "audio/pcm;rate=16000",
            data: audio.toString("base64"),
          },
        },
      ],
    }],
    generationConfig: { temperature: 0 },
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
  const text = result.candidates?.[0]?.content?.parts
    ?.map((part) => part.text || "")
    .join("")
    .trim();
  if (!text) {
    throw new Error("Gemini returned no transcript.");
  }
  return text;
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
    sendJson(response, 200, { text: await transcribe(audio) });
  } catch (error) {
    console.error(error.message);
    sendJson(response, 502, { error: error.message });
  }
});

server.listen(port, "0.0.0.0", () => {
  console.log(`Gemini ASR server listening on http://0.0.0.0:${port}`);
});
