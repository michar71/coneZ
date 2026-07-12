#!/bin/zsh

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"

MQTT_HOST="${MQTT_HOST:-sewerpipe.local}"
MQTT_PORT="${MQTT_PORT:-1883}"
HTTP_HOST="${HTTP_HOST:-127.0.0.1}"
HTTP_PORT="${HTTP_PORT:-8080}"
WS_PORT="${WS_PORT:-0}"
KEEPALIVE="${KEEPALIVE:-30}"
DB_PATH="${DB_PATH:-}"
DATA_DIR="${DATA_DIR:-}"
ADVERTISE_HOST="${ADVERTISE_HOST:-}"
PI_MODE="${PI_MODE:-0}"

cd "$SCRIPT_DIR"

args=(
  --mqtt-host "$MQTT_HOST"
  --mqtt-port "$MQTT_PORT"
  --http-host "$HTTP_HOST"
  --http-port "$HTTP_PORT"
  --ws-port "$WS_PORT"
  --keepalive "$KEEPALIVE"
)

if [[ "$PI_MODE" != "0" ]]; then
  args+=(--pi)
fi

if [[ -n "$DB_PATH" ]]; then
  args+=(--db-path "$DB_PATH")
fi

if [[ -n "$DATA_DIR" ]]; then
  args+=(--data-dir "$DATA_DIR")
fi

if [[ -n "$ADVERTISE_HOST" ]]; then
  args+=(--advertise-host "$ADVERTISE_HOST")
fi

existing_pid=""
if command -v lsof >/dev/null 2>&1; then
  existing_pid="$(lsof -tiTCP:"$HTTP_PORT" -sTCP:LISTEN 2>/dev/null | head -n 1 || true)"
fi

if [[ -n "$existing_pid" ]]; then
  existing_cmd="$(ps -p "$existing_pid" -o command= 2>/dev/null || true)"
  if [[ "$existing_cmd" == *"tracker_shim.py"* ]]; then
    echo "Stopping existing tracker shim on port $HTTP_PORT (pid $existing_pid)..."
    kill "$existing_pid"
    for _ in {1..20}; do
      if ! kill -0 "$existing_pid" 2>/dev/null; then
        break
      fi
      sleep 0.1
    done
  else
    echo "Port $HTTP_PORT is already in use by another process:"
    echo "  $existing_cmd"
    echo "Stop that process or set HTTP_PORT to a different value."
    exit 1
  fi
fi

exec python3 "$SCRIPT_DIR/tracker_shim.py" "${args[@]}"
