// Escaped HTML-Sonderzeichen zur XSS-Verhinderung (Fix #8/#9/#10)
String htmlEscape(const String& s) {
  String out;
  out.reserve(s.length() + 16);
  for (size_t i = 0; i < s.length(); i++) {
    char c = s.charAt(i);
    switch (c) {
      case '&':  out += "&amp;";  break;
      case '<':  out += "&lt;";   break;
      case '>':  out += "&gt;";   break;
      case '"':  out += "&quot;"; break;
      case '\'': out += "&#39;";  break;
      default:   out += c;        break;
    }
  }
  return out;
}

// Prüft, ob ein String nur sichere Zeichen für Bezeichnungen/Dateinamen enthält
bool isSafeLabel(const String& s) {
  if (s.length() == 0 || s.length() > 20) return false;
  for (size_t i = 0; i < s.length(); i++) {
    char c = s.charAt(i);
    if (!isalnum(c) && c != '-' && c != '_' && c != '.' && c != ' ') return false;
  }
  return true;
}

bool isSafeFilename(const String& s) {
  if (s.length() == 0 || s.length() > 64) return false;
  for (size_t i = 0; i < s.length(); i++) {
    char c = s.charAt(i);
    if (!isalnum(c) && c != '-' && c != '_' && c != '.' && c != '/') return false;
  }
  return true;
}

void setupWebserver() {
  handleDebugPage();
  handlePanelsPage();
  ws.onEvent(onWebSocketEvent);
  server.addHandler(&ws);
  server.serveStatic("/", SPIFFS, "/").setDefaultFile("index.html");

  // SPIFFS-Dateiliste
  server.on("/spiffs", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(200, "text/html", generateFileListHTML());
  });

  // Panel-Verwaltung
  server.on("/panel_set", HTTP_POST, [](AsyncWebServerRequest *request) {
    if (request->hasParam("longAddress", true) && request->hasParam("label", true)) {
      String la  = request->getParam("longAddress", true)->value();
      String lbl = request->getParam("label", true)->value();
      la.trim(); lbl.trim();
      // Fix #9: prüft, dass Bezeichnung nur sichere Zeichen enthält
      if (!isSafeLabel(lbl)) {
        request->send(400, "text/plain", "Ungültige Bezeichnung: nur Buchstaben, Ziffern, - _ . Leerzeichen (max. 20 Zeichen)");
        return;
      }
      // Aktualisiert, falls schon vorhanden, sonst hinzufügen
      bool found = false;
      for (int i = 0; i < panelMap_count; i++) {
        if (panelMap[i].longAddress == la) {
          panelMap[i].label = lbl;
          found = true; break;
        }
      }
      if (!found && panelMap_count < 150) {
        panelMap[panelMap_count].longAddress = la;
        panelMap[panelMap_count].label       = lbl;
        panelMap_count++;
      }
      savePanelMap();
      resetDiscoveryFlags();
    }
    request->redirect("/panels");
  });

  server.on("/panel_delete", HTTP_GET, [](AsyncWebServerRequest *request) {
    if (request->hasParam("longAddress")) {
      String la = request->getParam("longAddress")->value();
      for (int i = 0; i < panelMap_count; i++) {
        if (panelMap[i].longAddress == la) {
          for (int j = i; j < panelMap_count - 1; j++) panelMap[j] = panelMap[j+1];
          panelMap_count--;
          break;
        }
      }
      savePanelMap();
    }
    request->redirect("/panels");
  });

  handleFileUpload();

  // Datei löschen
  server.on("/delete", HTTP_GET, [](AsyncWebServerRequest *request) {
    if (request->hasParam("file")) {
      String fileToDelete = request->getParam("file")->value();
      if (SPIFFS.exists(fileToDelete)) {
        if (SPIFFS.remove(fileToDelete)) {
          request->redirect("/spiffs");
        } else {
          request->send(500, "text/plain", "Fehler beim Löschen der Datei");
        }
      } else {
        request->send(404, "text/plain", "Datei nicht gefunden");
      }
    } else {
      request->send(400, "text/plain", "Fehlender Parameter: file");
    }
  });

  // Datei-Download — Fix #1: nutzt request->send(SPIFFS), das den Lebenszyklus
  // der File-Objekte intern verwaltet, ohne Memory-Leak-Risiko bei vorzeitiger
  // Client-Trennung.
  server.on("/download", HTTP_GET, [](AsyncWebServerRequest *request) {
    if (!request->hasParam("file")) {
      request->send(400, "text/plain", "Fehlender Parameter: file");
      return;
    }
    String filePath = request->getParam("file")->value();
    if (!SPIFFS.exists(filePath)) {
      request->send(404, "text/plain", "Datei nicht gefunden");
      return;
    }
    request->send(SPIFFS, filePath, "application/octet-stream", true /*download*/);
  });

  // Neustart
  server.on("/reboot", HTTP_POST, [](AsyncWebServerRequest *request) {
    request->send(200, "text/plain", "♻️ ESP32 wird neu gestartet...");
    request->client()->close();
    delay(100);
    ESP.restart();
  });

  // NodeTable speichern
  server.on("/save", HTTP_GET, [](AsyncWebServerRequest *request) {
    saveNodeTable();
    NodeTable_changed = false;
    WebSerial.println("✅ NodeTable über /save-Endpunkt gespeichert");
    request->redirect("/debug");
  });

  // 404-Behandlung
  server.onNotFound([](AsyncWebServerRequest *request) {
    request->send(404, "text/plain", "404: Nicht gefunden");
  });
}

String urlEncode(const String &str) {
  String encoded = "";
  char buf[4];
  for (size_t i = 0; i < str.length(); i++) {
    char c = str.charAt(i);
    if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
      encoded += c;
    } else {
      sprintf(buf, "%%%02X", (unsigned char)c);
      encoded += buf;
    }
  }
  return encoded;
}

String fixPath(const String &path) {
  if (!path.startsWith("/")) {
    return "/" + path;
  }
  return path;
}

String generateFileListHTML() {
  size_t total    = SPIFFS.totalBytes();
  size_t used     = SPIFFS.usedBytes();
  size_t freeBytes = total - used;
  size_t freeHeap = ESP.getFreeHeap();
  float usedKB    = used   / 1024.0;
  float totalKB   = total  / 1024.0;
  float usedPct   = total  > 0 ? (used * 100.0 / total) : 0;

  String html = "<!DOCTYPE html><html lang='de'><head>"
    "<meta charset='UTF-8'>"
    "<meta name='viewport' content='width=device-width,initial-scale=1'>"
    "<title>SPIFFS · TigoServer</title>"
    "<style>"
    "*,*::before,*::after{box-sizing:border-box;margin:0;padding:0}"
    ":root{--bg:#0d0d0d;--surface:#1a1a1a;--border:#2e2e2e;--accent:#f0a500;--green:#4caf50;--blue:#42a5f5;--red:#ef5350;--text:#e0e0e0;--muted:#777;--radius:12px}"
    "body{background:var(--bg);color:var(--text);font-family:'Segoe UI',system-ui,sans-serif;padding:14px}"
    "nav{display:flex;align-items:center;gap:4px;background:var(--surface);border:1px solid var(--border);border-radius:var(--radius);padding:10px 16px;margin-bottom:16px;flex-wrap:wrap}"
    "nav a{color:var(--text);text-decoration:none;font-size:.85rem;font-weight:600;padding:5px 11px;border-radius:7px;transition:background .18s}"
    "nav a:hover{background:var(--border)}"
    "nav a.danger{color:var(--red)}"
    "nav a.danger:hover{background:rgba(239,83,80,.13)}"
    "h2{font-size:1rem;font-weight:700;color:var(--muted);text-transform:uppercase;letter-spacing:.07em;margin:20px 0 10px}"
    ".card{background:var(--surface);border:1px solid var(--border);border-radius:var(--radius);padding:16px;margin-bottom:16px}"
    "table{width:100%;border-collapse:collapse;font-size:.83rem}"
    "th{text-align:left;padding:8px 10px;color:var(--muted);font-size:.7rem;text-transform:uppercase;letter-spacing:.06em;border-bottom:1px solid var(--border)}"
    "td{padding:8px 10px;border-bottom:1px solid var(--border);vertical-align:middle;word-break:break-all}"
    "tr:last-child td{border-bottom:none}"
    "tr:hover td{background:rgba(255,255,255,.025)}"
    ".fname{color:var(--blue);font-weight:600}"
    ".fsize{color:var(--muted);white-space:nowrap}"
    ".btn{display:inline-block;padding:3px 10px;border-radius:5px;font-size:.75rem;font-weight:700;text-decoration:none;transition:background .17s}"
    ".btn-dl{background:rgba(66,165,245,.15);color:var(--blue)}"
    ".btn-dl:hover{background:rgba(66,165,245,.3)}"
    ".btn-del{background:rgba(239,83,80,.12);color:var(--red)}"
    ".btn-del:hover{background:rgba(239,83,80,.25)}"
    ".bar-wrap{height:8px;background:var(--border);border-radius:4px;overflow:hidden;margin:6px 0 4px}"
    ".bar{height:100%;border-radius:4px;background:linear-gradient(90deg,var(--green),var(--accent))}"
    ".stat-row{display:flex;gap:20px;flex-wrap:wrap;font-size:.82rem;color:var(--muted);margin-top:4px}"
    ".stat-row b{color:var(--text)}"
    "label{font-size:.8rem;color:var(--muted);display:block;margin-bottom:6px}"
    "input[type=file]{color:var(--text);font-size:.82rem;background:var(--border);border:1px solid var(--border);border-radius:6px;padding:5px 8px;width:100%}"
    ".btn-up{margin-top:10px;display:inline-block;padding:7px 20px;border-radius:7px;font-weight:700;font-size:.85rem;background:var(--accent);color:#000;border:none;cursor:pointer;transition:opacity .18s}"
    ".btn-up:hover{opacity:.82}"
    "</style></head><body>";

  html += "<nav>"
    "<a href='/'>🏠 Home</a>"
    "<a href='/debug'>🧠 Debug</a>"
    "<a href='/panels'>🔖 Panels</a>"
    "<a href='/spiffs'>💾 SPIFFS</a>"
    "<a href='#' class='danger' onclick=\"if(confirm('Neu starten?'))fetch('/reboot',{method:'POST'}).then(()=>setTimeout(()=>location.reload(),4000))\">🔁 Reboot</a>"
    "</nav>";

  // Dateitabelle
  html += "<div class='card'>";
  html += "<h2>Dateien</h2>";
  html += "<table><thead><tr><th>Name</th><th>Größe</th><th></th></tr></thead><tbody>";

  File root = SPIFFS.open("/");
  File file = root.openNextFile();
  bool hasFiles = false;
  while (file) {
    hasFiles = true;
    String fname = fixPath(file.name());
    String fnameEsc = htmlEscape(fname); // Fix #8: escaped zur XSS-Verhinderung
    size_t fsize  = file.size();
    html += "<tr>";
    html += "<td class='fname'>" + fnameEsc + "</td>";
    html += "<td class='fsize'>" + String(fsize) + " B &nbsp;(" + String(fsize / 1024.0, 1) + " KB)</td>";
    html += "<td style='white-space:nowrap'>"
            "<a class='btn btn-dl' href='/download?file=" + urlEncode(fname) + "'>⬇ Download</a> "
            "<a class='btn btn-del' href='/delete?file=" + urlEncode(fname) + "' "
            "onclick=\"return confirm('&quot;" + fnameEsc + "&quot; löschen?')\">&#x2715; Löschen</a>";
    html += "</td></tr>";
    file = root.openNextFile();
  }
  if (!hasFiles) html += "<tr><td colspan='3' style='color:var(--muted);text-align:center;padding:20px'>Keine Dateien</td></tr>";
  html += "</tbody></table></div>";

  // Speicher
  html += "<div class='card'>";
  html += "<h2>Speicher</h2>";
  html += "<div class='bar-wrap'><div class='bar' style='width:" + String(usedPct, 0) + "%'></div></div>";
  html += "<div class='stat-row'>";
  html += "<span><b>" + String(usedPct, 0) + "%</b> belegt</span>";
  html += "<span><b>" + String(usedKB, 1) + " KB</b> / " + String(totalKB, 1) + " KB</span>";
  html += "<span>frei: <b>" + String(freeBytes / 1024.0, 1) + " KB</b></span>";
  html += "<span>freier RAM: <b>" + String(freeHeap / 1024) + " KB</b></span>";
  html += "</div></div>";

  // Upload
  html += "<div class='card'>";
  html += "<h2>Datei hochladen</h2>";
  html += "<form method='POST' action='/save_upload' enctype='multipart/form-data'>";
  html += "<label>Datei zum Hochladen auf SPIFFS auswählen:</label>";
  html += "<input type='file' name='upload'>";
  html += "<button class='btn-up' type='submit'>⬆ Hochladen</button>";
  html += "</form></div>";

  html += "</body></html>";
  return html;
}



void handlePanelsPage() {
  server.on("/panels", HTTP_GET, [](AsyncWebServerRequest *request) {

    String h = "<!DOCTYPE html><html lang='de'><head>"
      "<meta charset='UTF-8'>"
      "<meta name='viewport' content='width=device-width,initial-scale=1'>"
      "<title>Panels · TigoServer</title>"
      "<style>"
      "*,*::before,*::after{box-sizing:border-box;margin:0;padding:0}"
      ":root{--bg:#0d0d0d;--surface:#1a1a1a;--border:#2e2e2e;--accent:#f0a500;--green:#4caf50;--blue:#42a5f5;--red:#ef5350;--text:#e0e0e0;--muted:#777;--radius:12px}"
      "body{background:var(--bg);color:var(--text);font-family:'Segoe UI',system-ui,sans-serif;padding:14px}"
      "nav{display:flex;align-items:center;gap:4px;background:var(--surface);border:1px solid var(--border);border-radius:var(--radius);padding:10px 16px;margin-bottom:16px;flex-wrap:wrap}"
      "nav a{color:var(--text);text-decoration:none;font-size:.85rem;font-weight:600;padding:5px 11px;border-radius:7px;transition:background .18s}"
      "nav a:hover{background:var(--border)}"
      "nav a.danger{color:var(--red)}"
      "nav a.danger:hover{background:rgba(239,83,80,.13)}"
      "h2{font-size:1rem;font-weight:700;color:var(--muted);text-transform:uppercase;letter-spacing:.07em;margin:20px 0 10px}"
      ".card{background:var(--surface);border:1px solid var(--border);border-radius:var(--radius);padding:16px;margin-bottom:16px;overflow-x:auto}"
      "table{width:100%;border-collapse:collapse;font-size:.83rem}"
      "th{text-align:left;padding:8px 10px;color:var(--muted);font-size:.7rem;text-transform:uppercase;letter-spacing:.06em;border-bottom:1px solid var(--border)}"
      "td{padding:8px 10px;border-bottom:1px solid var(--border);vertical-align:middle;white-space:nowrap}"
      "tr:last-child td{border-bottom:none}"
      "tr:hover td{background:rgba(255,255,255,.025)}"
      ".la{font-family:monospace;font-size:.82rem;color:var(--blue)}"
      ".lbl{font-weight:700;color:var(--accent)}"
      ".lbl-empty{color:var(--muted);font-style:italic}"
      "input[type=text]{background:#111;border:1px solid var(--border);color:var(--text);border-radius:6px;padding:5px 9px;font-size:.82rem;width:90px;transition:border .18s}"
      "input[type=text]:focus{outline:none;border-color:var(--accent)}"
      ".btn{display:inline-block;padding:4px 12px;border-radius:6px;font-size:.77rem;font-weight:700;border:none;cursor:pointer;transition:opacity .17s;text-decoration:none}"
      ".btn:hover{opacity:.8}"
      ".btn-save{background:var(--accent);color:#000}"
      ".btn-del{background:rgba(239,83,80,.15);color:var(--red)}"
      ".note{font-size:.75rem;color:var(--muted);margin-top:8px}"
      "</style></head><body>";

    h += "<nav>"
      "<a href='/'>🏠 Home</a>"
      "<a href='/debug'>🧠 Debug</a>"
      "<a href='/panels'>🔖 Panels</a>"
      "<a href='/spiffs'>💾 SPIFFS</a>"
      "<a href='#' class='danger' onclick=\"if(confirm('Neu starten?'))fetch('/reboot',{method:'POST'}).then(()=>setTimeout(()=>location.reload(),4000))\">🔁 Reboot</a>"
      "</nav>";

    // Tabelle bekannter Knoten (aus NodeTable) mit Bezeichnungen
    h += "<div class='card'>";
    h += "<h2>Panel-Zuordnung (&nbsp;" + String(NodeTable_count) + "&nbsp; Knoten)</h2>";
    h += "<p class='note' style='margin-bottom:12px'>Weise jedem Panel-Long-Address eine Bezeichnung zu (z.B. A4). "
         "Die Bezeichnung wird per WebSocket im Feld <code style='color:var(--blue)'>panel</code> übertragen.</p>";

    if (NodeTable_count == 0) {
      h += "<p style='color:var(--muted);padding:16px'>Kein Knoten in NodeTable. Warte, bis sich die Panels verbinden.</p>";
    } else {
      h += "<table><thead><tr><th>#</th><th>Long Address</th><th>Addr</th><th>Aktuelle Bezeichnung</th><th>Festlegen</th><th></th></tr></thead><tbody>";
      for (int i = 0; i < NodeTable_count; i++) {
        String la  = NodeTable[i].longAddress;
        String sa  = NodeTable[i].addr;
        String lbl = getPanelLabel(la);
        h += "<tr>";
        h += "<td style='color:var(--muted)'>" + String(i+1) + "</td>";
        h += "<td class='la'>" + la + "</td>";
        h += "<td style='color:var(--muted)'>" + sa + "</td>";
        if (lbl != "") {
          h += "<td class='lbl'>" + lbl + "</td>";
        } else {
          h += "<td class='lbl-empty'>nicht zugewiesen</td>";
        }
        // Formular festlegen
        h += "<td><form method='POST' action='/panel_set' style='display:flex;gap:6px;align-items:center'>"
             "<input type='hidden' name='longAddress' value='" + la + "'>"
             "<input type='text' name='label' placeholder='z.B. A4' value='" + lbl + "' maxlength='10'>"
             "<button class='btn btn-save' type='submit'>✓ Speichern</button>"
             "</form></td>";
        // Löschen
        if (lbl != "") {
          h += "<td><a class='btn btn-del' href='/panel_delete?longAddress=" + urlEncode(la) + "' "
               "onclick=\"return confirm('Bezeichnung &quot;" + lbl + "&quot; entfernen?')\">× Entfernen</a></td>";
        } else {
          h += "<td></td>";
        }
        h += "</tr>";
      }
      h += "</tbody></table>";
    }
    h += "</div>";

    // Zeigt den Status des aktuellen Mappings (zugeordnete Panels)
    h += "<div class='card'>";
    h += "<h2>Gespeicherte Zuordnung (&nbsp;" + String(panelMap_count) + "&nbsp; Einträge)</h2>";
    if (panelMap_count == 0) {
      h += "<p style='color:var(--muted);padding:8px'>Keine Zuordnung gespeichert.</p>";
    } else {
      h += "<table><thead><tr><th>Bezeichnung</th><th>Long Address</th><th></th></tr></thead><tbody>";
      for (int i = 0; i < panelMap_count; i++) {
        String lblEsc = htmlEscape(panelMap[i].label);      // Fix #9
        String laEsc  = htmlEscape(panelMap[i].longAddress); // Fix #9
        h += "<tr>";
        h += "<td class='lbl'>" + lblEsc + "</td>";
        h += "<td class='la'>" + laEsc + "</td>";
        h += "<td><a class='btn btn-del' href='/panel_delete?longAddress=" + urlEncode(panelMap[i].longAddress) + "' "
             "onclick=\"return confirm('Entfernen?')\">×</a></td>";
        h += "</tr>";
      }
      h += "</tbody></table>";
    }
    h += "</div>";

    h += "</body></html>";
    request->send(200, "text/html", h);
  });
}

void handleDebugPage() {
  server.on("/debug", HTTP_GET, [](AsyncWebServerRequest *request){

    String h = "<!DOCTYPE html><html lang='de'><head>"
      "<meta charset='UTF-8'>"
      "<meta name='viewport' content='width=device-width,initial-scale=1'>"
      "<title>Debug · TigoServer</title>"
      "<style>"
      "*,*::before,*::after{box-sizing:border-box;margin:0;padding:0}"
      ":root{--bg:#0d0d0d;--surface:#1a1a1a;--border:#2e2e2e;--accent:#f0a500;--green:#4caf50;--blue:#42a5f5;--red:#ef5350;--text:#e0e0e0;--muted:#777;--radius:12px}"
      "body{background:var(--bg);color:var(--text);font-family:'Segoe UI',system-ui,sans-serif;padding:14px}"
      "nav{display:flex;align-items:center;gap:4px;background:var(--surface);border:1px solid var(--border);border-radius:var(--radius);padding:10px 16px;margin-bottom:16px;flex-wrap:wrap}"
      "nav a{color:var(--text);text-decoration:none;font-size:.85rem;font-weight:600;padding:5px 11px;border-radius:7px;transition:background .18s}"
      "nav a:hover{background:var(--border)}"
      "nav a.danger{color:var(--red)}"
      "nav a.danger:hover{background:rgba(239,83,80,.13)}"
      "h2{font-size:1rem;font-weight:700;color:var(--muted);text-transform:uppercase;letter-spacing:.07em;margin:20px 0 10px}"
      ".card{background:var(--surface);border:1px solid var(--border);border-radius:var(--radius);padding:16px;margin-bottom:16px;overflow-x:auto}"
      "table{width:100%;border-collapse:collapse;font-size:.8rem;min-width:600px}"
      "th{text-align:center;padding:7px 10px;color:var(--muted);font-size:.68rem;text-transform:uppercase;letter-spacing:.06em;border-bottom:1px solid var(--border)}"
      "td{padding:7px 10px;border-bottom:1px solid var(--border);text-align:center;white-space:nowrap}"
      "tr:last-child td{border-bottom:none}"
      "tr:hover td{background:rgba(255,255,255,.025)}"
      ".hl-blue{color:var(--blue);font-weight:600}"
      ".hl-green{color:var(--green);font-weight:700}"
      ".hl-muted{color:var(--muted)}"
      ".warn-box{background:rgba(239,83,80,.1);border:1px solid rgba(239,83,80,.3);border-radius:8px;padding:10px 14px;color:var(--red);font-size:.85rem;margin-bottom:12px}"
      ".btn-save{display:inline-block;padding:8px 22px;border-radius:8px;font-weight:700;font-size:.85rem;background:var(--accent);color:#000;text-decoration:none;transition:opacity .18s}"
      ".btn-save:hover{opacity:.82}"
      "</style></head><body>";

    h += "<nav>"
      "<a href='/'>🏠 Home</a>"
      "<a href='/debug'>🧠 Debug</a>"
      "<a href='/panels'>🔖 Panels</a>"
      "<a href='/spiffs'>💾 SPIFFS</a>"
      "<a href='#' class='danger' onclick=\"if(confirm('Neu starten?'))fetch('/reboot',{method:'POST'}).then(()=>setTimeout(()=>location.reload(),4000))\">🔁 Reboot</a>"
      "</nav>";

    // Ermittelt die Panel-Bezeichnung anhand der Geräte-Adresse
    auto getPanelLabel = [](const String& addr) -> String {
      // 1) longAddress in NodeTable suchen
      String la = "";
      for (int k = 0; k < NodeTable_count; k++) {
        if (NodeTable[k].addr == addr) { la = NodeTable[k].longAddress; break; }
      }
      if (la.isEmpty()) return "";
      // 2) in panelMap suchen
      for (int k = 0; k < panelMap_count; k++) {
        if (panelMap[k].longAddress == la) return panelMap[k].label;
      }
      return "";
    };

    // Sortiert die Geräteindizes nach Panel-Bezeichnung, ohne Bezeichnung ans Ende
    int order[100];
    for (int i = 0; i < deviceCount; i++) order[i] = i;
    // Bubble-Sort (deviceCount typischerweise klein)
    for (int a = 0; a < deviceCount - 1; a++) {
      for (int b = a + 1; b < deviceCount; b++) {
        String la = getPanelLabel(devices[order[a]].addr);
        String lb = getPanelLabel(devices[order[b]].addr);
        // wer keine Bezeichnung hat, kommt ans Ende
        if (la.isEmpty() && !lb.isEmpty()) { int t = order[a]; order[a] = order[b]; order[b] = t; }
        else if (!la.isEmpty() && !lb.isEmpty() && lb < la) { int t = order[a]; order[a] = order[b]; order[b] = t; }
      }
    }

    // Gerätetabelle
    h += "<div class='card'>";
    h += "<h2>Tigo-Geräte (&nbsp;" + String(deviceCount) + "&nbsp;)</h2>";
    h += "<table><thead><tr>"
      "<th>#</th><th>Panel</th><th>Node ID</th><th>Addr</th>"
      "<th>Vin</th><th>Vout</th><th>Duty</th>"
      "<th>Strom</th><th>Watt</th><th>Temp</th>"
      "<th>Slot</th><th>RSSI</th><th>Barcode</th>"
      "</tr></thead><tbody>";

    for (int ii = 0; ii < deviceCount; ii++) {
      int i = order[ii];
      int watt = round(devices[i].voltage_out * devices[i].current_in);
      String label = getPanelLabel(devices[i].addr);
      h += "<tr>";
      h += "<td class='hl-muted'>" + String(ii+1) + "</td>";
      h += "<td class='hl-green'>" + (label.isEmpty() ? "<span class='hl-muted'>—</span>" : htmlEscape(label)) + "</td>"; // Fix #10
      h += "<td>" + devices[i].pv_node_id + "</td>";
      h += "<td class='hl-blue'>" + devices[i].addr + "</td>";
      h += "<td>" + String(devices[i].voltage_in, 2)  + " V</td>";
      h += "<td>" + String(devices[i].voltage_out, 2) + " V</td>";
      h += "<td class='hl-muted'>" + String(devices[i].duty_cycle, HEX) + "</td>";
      h += "<td>" + String(devices[i].current_in, 2)  + " A</td>";
      h += "<td class='hl-green'>" + String(watt) + " W</td>";
      h += "<td>" + String(devices[i].temperature, 1) + " &deg;C</td>";
      h += "<td class='hl-muted'>" + devices[i].slot_counter + "</td>";
      h += "<td>" + String(devices[i].rssi) + "</td>";
      h += "<td>" + devices[i].barcode + "</td>";
      h += "</tr>";
    }
    h += "</tbody></table></div>";

    // NodeTable-Tabelle
    h += "<div class='card'>";
    h += "<h2>Node Table (&nbsp;" + String(NodeTable_count) + "&nbsp;)</h2>";
    if (NodeTable_changed) {
      h += "<div class='warn-box'>⚠️ NodeTable geändert und noch nicht gespeichert</div>";
    }
    h += "<table><thead><tr><th>#</th><th>Addr</th><th>Long Address</th><th>Checksum</th></tr></thead><tbody>";
    for (int i = 0; i < NodeTable_count; i++) {
      h += "<tr>";
      h += "<td class='hl-muted'>" + String(i+1) + "</td>";
      h += "<td class='hl-blue'>" + NodeTable[i].addr + "</td>";
      h += "<td>" + NodeTable[i].longAddress + "</td>";
      h += "<td class='hl-muted'>" + NodeTable[i].checksum + "</td>";
      h += "</tr>";
    }
    h += "</tbody></table>";
    h += "<br><a class='btn-save' href='/save'>💾 NodeTable jetzt speichern</a>";
    h += "</div>";

    h += "</body></html>";
    request->send(200, "text/html", h);
  });
}


void handleFileUpload() {
  server.on(
    "/save_upload", HTTP_POST,
    [](AsyncWebServerRequest *request) {
      request->redirect("/spiffs");
    },
    [](AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final) {
      static bool spaceChecked = false;

      if (!index) {
        spaceChecked = false;
        Serial.printf("Upload beginnt: %s\n", filename.c_str());
        // Fix #8: validiert Dateiname — nur alphanumerische Zeichen und . - _ erlaubt
        for (size_t ci = 0; ci < filename.length(); ci++) {
          char c = filename.charAt(ci);
          if (!isalnum(c) && c != '-' && c != '_' && c != '.') {
            Serial.println("❌ Ungültiger Dateiname.");
            return;
          }
        }
        if (filename.length() > 64) {
          Serial.println("❌ Dateiname zu lang.");
          return;
        }
        String path = "/" + filename;
        if (SPIFFS.exists(path)) {
          SPIFFS.remove(path);
        }
        uploadFile = SPIFFS.open(path, FILE_WRITE);
      }

      if (!spaceChecked) {
        if (SPIFFS.totalBytes() - SPIFFS.usedBytes() < len) {
          Serial.println("❌ Nicht genug Speicherplatz auf SPIFFS.");
          if (uploadFile) uploadFile.close();
          return;
        }
        spaceChecked = true;
      }

      if (uploadFile) {
        uploadFile.write(data, len);
      }

      if (final) {
        Serial.printf("Upload abgeschlossen: %s (%u Bytes)\n", filename.c_str(), index + len);
        if (uploadFile) uploadFile.close();
      }
    }
  );
}
