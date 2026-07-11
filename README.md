# Tigo RS485 ESP32 Monitor

Echtzeit-Monitor für Tigo-Solar-Optimierer auf Basis eines ESP32.
Liest den RS-485-Bus (TAP-Protokoll) aus und stellt ein Web-Dashboard, WebSocket und Datenpersistenz über SPIFFS bereit.

> **Hinweis:** Dies ist ein Fork von [Bobsilvio/tigo_server](https://github.com/Bobsilvio/tigo_server), der wiederum auf [tictactom/tigo_server](https://github.com/tictactom/tigo_server) basiert.

<img src="images/home.png" alt="dashboard" width="1000"/>
<img src="images/spiffs.png" alt="spiffs" width="1000"/>

---

## 🛠️ Hardware

| Komponente | Hinweise |
|---|---|
| **ESP32 / ESP32-S3** | Mindestens 4 MB Flash mit SPIFFS-Partition |
| **TTL→RS485-Konverter** | MAX485 oder ähnlich |
| **5V-Regler** | Falls über den RS-485-Bus versorgt |

---

## ⚙️ Funktionen

- 📡 **RS-485-Bus auslesen** (Tigo TAP/CCA, 38400 Baud 8N1)
- 🔍 **Frame-Parsing**: Leistung (0x31), Topologie (0x09), NodeTable (0x27)
- 🌐 **Integrierter Webserver**:
  - `/` – Live-Dashboard mit Diagrammen (ApexCharts) und Karte pro Modul
  - `/debug` – Rohdaten-Tabelle (Spannung, Strom, Temperatur, RSSI, …)
  - `/panels` – Manuelle Zuordnung Long-Address → Bezeichnung (z. B. A4)
  - `/spiffs` – Dateimanager (Hochladen/Herunterladen/Löschen von SPIFFS-Dateien)
- 🔌 **WebSocket `/ws`** — Echtzeit-Update; Payload pro Modul:
  ```json
  {
    "id":       "A4",
    "panel":    "A4",
    "longaddr": "04C05B4000B1A688",
    "barcode":  "04C05B4000B1A688",
    "addr":     "001A",
    "watt":     250,
    "vin":      34.70,
    "vout":     34.40,
    "amp":      6.94,
    "temp":     34.4,
    "rssi":     126
  }
  ```
- 💾 **NodeTable** — automatische Speicherung mit 30 s Debounce (`/nodetable.json`)
- 🔖 **Panel Map** — dauerhafte Zuordnung Long-Address → Bezeichnung (`/panel_map.json`)
- 🕒 **NTP-Sync** — genaue Uhrzeit nach Synchronisierung
- 🔁 **OTA** — Firmware-Update über das Netzwerk (Partition Scheme mit zwei App-Slots erforderlich, siehe unten)
- 📡 **MQTT mit Home-Assistant-Auto-Discovery**:
  - Für jedes erkannte Panel wird automatisch ein Home-Assistant-Gerät mit 6 Sensoren angelegt (Leistung, Vin, Vout, Strom, Temperatur, Signal) — kein manuelles YAML in Home Assistant nötig
  - State-Topic pro Panel: `tigo/<addr>/state` (stabil, ändert sich nicht bei Panel-Umbenennung)
  - Discovery-Topic: `homeassistant/sensor/tigo_<addr>_<key>/config` (retained)
  - Übergeordnetes Hub-Gerät "TigoServer", alle Panels per `via_device` verknüpft — sichtbar über die Geräteseite des Hubs in Home Assistant
  - Beim Umbenennen eines Panels über `/panels` werden die Discovery-Nachrichten automatisch mit dem neuen Namen erneut gesendet
- 📊 **Grafisches Dashboard**:
  - Flächendiagramm — Gesamtleistung im Zeitverlauf (letzte 60 Messwerte, im Speicher)
  - Horizontales Balkendiagramm — Momentanleistung pro Panel, Farbverlauf Weiß→Grün
  - Karte pro Modul mit Fortschrittsbalken, Vin/Vout, Strom, Temperatur, RSSI

---

## 📂 SPIFFS-Dateien

| Datei | Beschreibung |
|---|---|
| `index.html` | Web-Dashboard |
| `nodetable.json` | Zuordnung Kurzadresse ↔ Long-Address (automatisch generiert) |
| `panel_map.json` | Zuordnung Long-Address ↔ Panel-Bezeichnung (verwaltet über `/panels`) |

---

## 🔖 Panel-Zuordnung

Geh auf `/panels`: Jede Zeile zeigt die Long-Address des Moduls (z. B. `04C05B4000B1A688`).
Trag die Bezeichnung ein (z. B. `A4`) und klick **Speichern**.
Ab diesem Zeitpunkt überträgt der WebSocket `"panel": "A4"` und `"id": "A4"` für dieses Modul.
Die Long-Address bleibt weiterhin in den Feldern `barcode` und `longaddr` verfügbar.

---

## 🔄 Automatische Speicherung der NodeTable

Die `loop()` prüft in jedem Zyklus: Wenn `NodeTable_changed == true` ist und ≥ 30 s
seit der letzten Speicherung vergangen sind → wird `saveNodeTable()` aufgerufen und das Flag zurückgesetzt.
Manuelles Speichern ist über `/debug` → Button **NodeTable jetzt speichern** möglich.

---

## 🧰 Installation (Arduino IDE)

### 1. Voraussetzungen
- **Arduino IDE 2.x**
- ESP32-Board-Unterstützung:
  *Datei → Voreinstellungen → Zusätzliche Boardverwalter-URLs*:
  ```
  https://espressif.github.io/arduino-esp32/package_esp32_index.json
  ```
  Danach: *Werkzeuge → Board → Boardverwalter → "esp32" → Installieren*

### 2. Benötigte Bibliotheken
Installieren über *Sketch → Bibliothek einbinden → Bibliotheken verwalten*:
- **ArduinoJson** (Benoit Blanchon)
- **ESPAsyncWebServer** + **AsyncTCP**
- **PubSubClient** (MQTT)
- **WebSerial**

### 3. Board-Konfiguration
*Werkzeuge*:
- **Board:** `ESP32 Dev Module` (oder S3-Variante)
- **Partition Scheme:** siehe Tabelle unten — die Wahl entscheidet, ob OTA überhaupt funktioniert und wie viel Platz für SPIFFS bleibt
- **Upload Speed:** 921600 (bei Verbindungsabbrüchen auf 115200 reduzieren)

| Partition Scheme | App-Slots | SPIFFS-Größe | OTA möglich? |
|---|---|---|---|
| `No OTA (1MB APP / 3MB SPIFFS)` | 1 | ~3 MB | ❌ Nein — nur ein App-Slot, OTA-Uploads schlagen mit Timeout fehl |
| `Default 4MB with spiffs (1.2MB APP/1.5MB SPIFFS)` | 1 | ~1,5 MB | ❌ Nein — ebenfalls nur ein App-Slot |
| `Minimal SPIFFS (1.9MB APP with OTA/128KB SPIFFS)` | 2 | ~128 KB | ✅ Ja — empfohlen, wenn OTA genutzt werden soll |

**Wichtig:** Die für dieses Projekt benötigten SPIFFS-Dateien (`index.html`, `nodetable.json`, `panel_map.json`) sind zusammen nur wenige KB groß — die 128 KB von `Minimal SPIFFS` reichen dafür problemlos aus.

**⚠️ Beim Wechsel des Partition Scheme:** Die SPIFFS-Partition wird dabei neu angelegt, der bisherige Inhalt geht verloren. Vorher unbedingt über `/spiffs` alle Dateien herunterladen (insbesondere `nodetable.json` und `panel_map.json`, da deren Neuaufbau je nach Sendeintervall der CCA Stunden dauern kann) und nach dem Flashen mit neuem Schema wieder hochladen. Ein Partition-Scheme-Wechsel erfordert außerdem immer einen Flash per USB — OTA kann die Partitionstabelle selbst nicht ändern.

### 4. WLAN-Zugangsdaten
In der Datei `TigoServer.ino` (ganz oben):
```cpp
const char* ssid     = "DeinWLANName";
const char* password = "DeinPasswort";
```

### 5. Firmware flashen
1. ESP32 per USB anschließen
2. **→ Hochladen** klicken
3. Serial Monitor öffnen (115200 Baud)

### 6. SPIFFS-Dateien hochladen
Plugin **ESP32 Sketch Data Upload** installieren:
→ [https://github.com/me-no-dev/arduino-esp32fs-plugin](https://github.com/me-no-dev/arduino-esp32fs-plugin)
Danach: *Werkzeuge → ESP32 Sketch Data Upload*

### 7. Erster Zugriff
IP-Adresse im Serial Monitor oder im Router-DHCP finden:
```
http://<ESP32-IP>
```

**⚠️ Wichtig — 404 beim allerersten Aufruf ist normal:** Direkt nach dem Flashen ist SPIFFS noch leer, es gibt keine `index.html` — der Aufruf von `http://<ESP32-IP>/` liefert deshalb zunächst **404: Not Found**. Das liegt daran, dass die `index.html` eine separate Datei ist, die nicht im Sketch-Code steckt, sondern eigenständig auf den SPIFFS-Speicher hochgeladen werden muss (siehe Schritt 6).

So gehst du vor, wenn der 404-Fehler auftritt:
1. Ruf stattdessen direkt `http://<ESP32-IP>/spiffs` auf — dieser Endpunkt funktioniert bereits ohne `index.html`, da er Teil des Sketches selbst ist
2. Dort im Bereich **„Datei hochladen"** die `index.html` aus dem Repository auswählen und hochladen
3. Anschließend `http://<ESP32-IP>/` erneut aufrufen — jetzt sollte das Dashboard erscheinen

Danach stehen alle Seiten zur Verfügung:
- `/` — Dashboard + Diagramme (erst nutzbar nach Upload der `index.html`)
- `/debug` — Rohdaten + NodeTable
- `/panels` — Bezeichnungs-Zuordnung
- `/spiffs` — Dateimanager (**von Anfang an nutzbar**, auch vor dem `index.html`-Upload)

---

## ⚡ Verkabelungsschema

<img src="images/esp32-rs485.png" alt="wiring" width="400"/>
<img src="images/stepdown-5v.png" alt="stepdown" width="400"/>

| RS-485 | ESP32 | Hinweis |
|---|---|---|
| RO → RX | GPIO 16 | RS-485 → ESP32 |
| DI ← TX | GPIO 17 | ESP32 → RS-485 |
| RE/DE | GND (LOW) | Nur-Empfangs-Modus |
| A / B | Bus TAP ↔ CCA | Parallel angeschlossen |
| 5 V / GND | Gemeinsame Versorgung | |

---

## 📎 Danksagung

- Protokoll-Reverse-Engineering: [willglynn/taptap](https://github.com/willglynn/taptap)
- Ursprüngliches Projekt: [tictactom/tigo_server](https://github.com/tictactom/tigo_server)
- Vorlage dieses Forks: [Bobsilvio/tigo_server](https://github.com/Bobsilvio/tigo_server) — fügt grafisches Dashboard, Panel-Mapping, dauerhafte NodeTable und Dark-Theme-Seiten hinzu.

---

## 🔓 Lizenz

MIT — freie Anpassung für die eigene Anlage.
