#!/usr/bin/env bash
# Kompiliert den Sketch und lädt ihn per OTA auf den ESP32 hoch.
# Nutzung: ./flash_ota.sh <ESP32-IP>
set -euo pipefail

IP="${1:?Usage: ./flash_ota.sh <ESP32-IP>}"
FQBN="esp32:esp32:esp32"
OTA_PORT=3232

cd "$(dirname "$0")"

SECRETS="TigoServer/secrets.h"
if [ ! -f "$SECRETS" ]; then
  echo "Fehlt: $SECRETS (siehe TigoServer/secrets.h.example)" >&2
  exit 1
fi
OTA_PASSWORD="$(sed -n 's/.*OTA_PASSWORD[^"]*"\([^"]*\)".*/\1/p' "$SECRETS")"
if [ -z "$OTA_PASSWORD" ]; then
  echo "Konnte OTA_PASSWORD nicht aus $SECRETS lesen." >&2
  exit 1
fi

BUILD_DIR="$(mktemp -d)"
trap 'rm -rf "$BUILD_DIR"' EXIT

echo "==> Kompiliere..."
arduino-cli compile --fqbn "$FQBN" --build-path "$BUILD_DIR" TigoServer

ESPOTA="$(find "$HOME/Library/Arduino15/packages/esp32/hardware/esp32" -maxdepth 3 -name espota.py | head -1)"
BIN="$BUILD_DIR/TigoServer.ino.bin"

echo "==> Lade OTA-Update auf $IP hoch..."
python3 "$ESPOTA" -i "$IP" -p "$OTA_PORT" "--auth=$OTA_PASSWORD" -f "$BIN"
