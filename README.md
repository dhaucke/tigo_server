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
- 🔁 **OTA** — Firmware-Update über das Netzwerk
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
- **Partition Scheme:** `No OTA (1MB APP / 3MB SPIFFS)` — **für OTA-Nutzung stattdessen ein Schema mit zwei App-Partitionen wählen** (z. B. „Minimal SPIFFS (Large APPS with OTA)"), sonst schlägt der Netzwerk-Upload fehl
- **Upload Speed:** 921600 (bei Verbindungsabbrüchen auf 115200 reduzieren)

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
- `/` — Dashboard + Diagramme
- `/debug` — Rohdaten + NodeTable
- `/panels` — Bezeichnungs-Zuordnung
- `/spiffs` — Dateimanager

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
