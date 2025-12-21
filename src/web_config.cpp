#include "web_config.h"

#include <WebServer.h>

#include "ticker_persistence.h"

namespace {
  WebServer server(80);
  TickerManager* gMgr = nullptr;
  WebConfig::OnTickersChangedFn gOnChanged = nullptr;

  const char kIndexHtml[] PROGMEM = R"HTML(
<!doctype html>
<html>
<head>
  <meta charset="utf-8"/>
  <meta name="viewport" content="width=device-width, initial-scale=1"/>
  <title>Stock Ticker Setup</title>
  <style>
    body { font-family: -apple-system, system-ui, Segoe UI, Roboto, Arial, sans-serif; margin: 0; padding: 16px; background: #0b0f1a; color: #e8eefc; }
    .card { max-width: 520px; margin: 0 auto; background: #121a2a; border: 1px solid #24304a; border-radius: 12px; padding: 16px; }
    h1 { font-size: 18px; margin: 0 0 12px; }
    label { display: block; font-size: 13px; opacity: .9; margin: 12px 0 6px; }
    input { width: 100%; font-size: 16px; padding: 12px; border-radius: 10px; border: 1px solid #2a3a5c; background: #0b0f1a; color: #e8eefc; }
    button { margin-top: 12px; width: 100%; padding: 12px; font-size: 16px; border: 0; border-radius: 10px; background: #2b66ff; color: white; }
    .hint { margin-top: 10px; font-size: 12px; opacity: .8; line-height: 1.4; }
    .status { margin-top: 12px; font-size: 13px; white-space: pre-wrap; }
    code { background: #0b0f1a; padding: 2px 6px; border-radius: 6px; border: 1px solid #24304a; }
  </style>
</head>
<body>
  <div class="card">
    <h1>Configure tickers</h1>
    <div class="hint">Enter up to 10 symbols, comma-separated (example: <code>AAPL,MSFT,NVDA</code>).</div>
    <label for="tickers">Tickers</label>
    <input id="tickers" placeholder="AAPL,MSFT,NVDA" autocomplete="off" autocapitalize="characters" spellcheck="false"/>
    <button id="save">Save</button>
    <div class="status" id="status"></div>
  </div>
<script>
async function load() {
  const r = await fetch('/api/tickers');
  const j = await r.json();
  document.getElementById('tickers').value = (j.tickers || []).join(',');
}
async function save() {
  const status = document.getElementById('status');
  status.textContent = 'Saving...';
  const tickers = document.getElementById('tickers').value || '';
  const r = await fetch('/api/tickers', { method:'POST', headers:{'Content-Type':'application/json'}, body: JSON.stringify({ tickers })});
  const j = await r.json().catch(()=>({ok:false,error:'bad_json'}));
  if (r.ok && j.ok) status.textContent = 'Saved: ' + (j.tickers||[]).join(',');
  else status.textContent = 'Error: ' + (j.error || ('http_' + r.status));
}
document.getElementById('save').addEventListener('click', save);
load().catch(()=>{ document.getElementById('status').textContent = 'Failed to load'; });
</script>
</body>
</html>
)HTML";

  void sendJson(int code, const String& body) {
    server.sendHeader("Access-Control-Allow-Origin", "*");
    server.send(code, "application/json", body);
  }

  String jsonEscape(const String& in) {
    String out;
    out.reserve(in.length() + 8);
    for (size_t i = 0; i < in.length(); i++) {
      char c = in[i];
      if (c == '\\' || c == '"') { out += '\\'; out += c; }
      else if (c == '\n') out += "\\n";
      else if (c == '\r') out += "\\r";
      else out += c;
    }
    return out;
  }

  void handleIndex() {
    server.send(200, "text/html", FPSTR(kIndexHtml));
  }

  void handleGetTickers() {
    if (!gMgr) return sendJson(500, "{\"ok\":false,\"error\":\"no_mgr\"}");
    String out = "{\"ok\":true,\"tickers\":[";
    for (uint8_t i = 0; i < gMgr->count(); i++) {
      if (i) out += ",";
      out += "\"";
      out += jsonEscape(String(gMgr->symbolAt(i)));
      out += "\"";
    }
    out += "]}";
    sendJson(200, out);
  }

  // Very small JSON extraction: expects {"tickers":"AAPL,MSFT"} or {"tickers":"..."}.
  bool extractTickersCsvFromJson(const String& body, String& csvOut) {
    int k = body.indexOf("\"tickers\"");
    if (k < 0) return false;
    int colon = body.indexOf(':', k);
    if (colon < 0) return false;
    int q1 = body.indexOf('"', colon + 1);
    if (q1 < 0) return false;
    int q2 = body.indexOf('"', q1 + 1);
    if (q2 < 0) return false;
    csvOut = body.substring(q1 + 1, q2);
    return true;
  }

  void handlePostTickers() {
    if (!gMgr) return sendJson(500, "{\"ok\":false,\"error\":\"no_mgr\"}");

    String csv;
    if (server.hasArg("plain")) {
      String body = server.arg("plain");
      if (!extractTickersCsvFromJson(body, csv)) {
        // fallback: treat raw body as CSV
        csv = body;
      }
    } else if (server.hasArg("tickers")) {
      csv = server.arg("tickers");
    } else {
      return sendJson(400, "{\"ok\":false,\"error\":\"missing_tickers\"}");
    }

    String err;
    if (!gMgr->setFromCsv(csv, err)) {
      String out = String("{\"ok\":false,\"error\":\"") + jsonEscape(err) + "\"}";
      return sendJson(400, out);
    }

    (void)TickerPersistence::save(*gMgr);
    if (gOnChanged) gOnChanged();
    handleGetTickers();
  }
}

void WebConfig::begin(TickerManager& mgr, OnTickersChangedFn onChanged) {
  gMgr = &mgr;
  gOnChanged = onChanged;
  server.on("/", HTTP_GET, handleIndex);
  server.on("/api/tickers", HTTP_GET, handleGetTickers);
  server.on("/api/tickers", HTTP_POST, handlePostTickers);
  server.onNotFound([]() { server.send(404, "text/plain", "Not found"); });
  server.begin();
}

void WebConfig::tick() {
  server.handleClient();
}


