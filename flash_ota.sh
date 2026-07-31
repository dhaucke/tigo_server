#!/usr/bin/env bash
# Kompiliert den Sketch und lädt ihn per OTA auf den ESP32 hoch.
# Nutzung: ./flash_ota.sh <ESP32-IP>
set -euo pipefail

IP="${1:?Usage: ./flash_ota.sh <ESP32-IP>}"
FQBN="esp32:esp32:esp32"
OTA_PASSWORD="Tigo\$olar"

cd "$(dirname "$0")"

echo "==> Kompiliere..."
arduino-cli compile --fqbn "$FQBN" TigoServer

echo "==> Lade OTA-Update auf $IP hoch..."
arduino-cli upload -p "$IP" --fqbn "$FQBN" --upload-field "password=$OTA_PASSWORD" TigoServer
