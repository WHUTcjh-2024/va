// Local-only benchmark coordinator. The expected code never crosses the game page.
const http = require("node:http");
const fs = require("node:fs");
const path = require("node:path");
const root = __dirname;
let active = null;
const json = (res, value, status = 200) => { res.writeHead(status, { "content-type": "application/json", "cache-control": "no-store" }); res.end(JSON.stringify(value)); };
const code = () => Array.from({ length: 3 }, () => "ABCDEFGHIJKLMNOPQRSTUVWXYZ"[Math.floor(Math.random() * 26)]).join("") + Array.from({ length: 3 }, () => Math.floor(Math.random() * 10)).join("");
const read = request => new Promise(resolve => { let body = ""; request.on("data", part => body += part); request.on("end", () => { try { resolve(JSON.parse(body || "{}")); } catch { resolve({}); } }); });
http.createServer(async (req, res) => {
  const url = new URL(req.url, "http://localhost");
  if (req.method === "POST" && url.pathname === "/api/run") { const settings = await read(req); active = { id: crypto.randomUUID(), settings, round: null, records: [] }; return json(res, { runId: active.id }); }
  if (!active && url.pathname.startsWith("/api/")) return json(res, { error: "no active run" }, 409);
  if (req.method === "POST" && url.pathname === "/api/open") { const body = await read(req); active.round = { id: body.roundId, expected: code(), openAt: Date.now(), revealedAt: null, first: null }; return json(res, { roundId: active.round.id }); }
  if (req.method === "POST" && url.pathname === "/api/code") return json(res, { code: active.round.expected });
  if (req.method === "POST" && url.pathname === "/api/reveal") { active.round.revealedAt = Date.now(); return json(res, { t0: active.round.revealedAt }); }
  if (req.method === "GET" && url.pathname === "/api/active") return json(res, active.round ? { runId: active.id, roundId: active.round.id, open: true } : { open: false });
  if (req.method === "GET" && url.pathname === "/api/submitted") return json(res, { submitted: Boolean(active.round?.first) });
  if (req.method === "POST" && url.pathname === "/api/submit") { const body = await read(req); if (!active.round || body.runId !== active.id || body.roundId !== active.round.id || active.round.first) return json(res, { accepted: false }); active.round.first = { code: String(body.code || "").toUpperCase(), at: Date.now() }; return json(res, { accepted: true }); }
  if (req.method === "POST" && url.pathname === "/api/close") { const round = active.round; const submission = round.first; const latency = submission && round.revealedAt ? submission.at - round.revealedAt : null; const outcome = !submission ? "TIMEOUT" : !round.revealedAt || latency < 0 ? "EARLY" : submission.code === round.expected ? "CORRECT" : "WRONG"; active.records.push({ run_id: active.id, round_id: round.id, expected_code: round.expected, submitted_code: submission?.code || "", outcome, t0_unix_ms: round.revealedAt || "", t1_unix_ms: submission?.at || "", latency_ms: latency ?? "" }); active.round = null; return json(res, active.records.at(-1)); }
  const file = path.join(root, url.pathname === "/" ? "stream.html" : url.pathname.slice(1)); if (!file.startsWith(root) || !fs.existsSync(file)) { res.writeHead(404); return res.end(); } res.writeHead(200, { "content-type": file.endsWith(".html") ? "text/html" : "application/javascript" }); fs.createReadStream(file).pipe(res);
}).listen(8787, "127.0.0.1", () => console.log("Benchmark: http://127.0.0.1:8787/stream.html"));
