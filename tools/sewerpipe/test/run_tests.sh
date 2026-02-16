#!/bin/bash
# Integration tests for sewerpipe MQTT broker
# Requires: Python 3
set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
BROKER="$SCRIPT_DIR/../sewerpipe"
PORT=11883
PASS=0
FAIL=0
BROKER_PID=

cleanup() {
    [ -n "$BROKER_PID" ] && kill "$BROKER_PID" 2>/dev/null; wait "$BROKER_PID" 2>/dev/null
    true
}
trap cleanup EXIT

if [ ! -x "$BROKER" ]; then
    echo "Build sewerpipe first: make"
    exit 1
fi

# Find a free port
for p in 11883 11884 11885 11886; do
    if ! ss -tlnp 2>/dev/null | grep -q ":$p "; then PORT=$p; break; fi
done

"$BROKER" -v -p "$PORT" &
BROKER_PID=$!
sleep 0.3

run_test() {
    local name="$1"
    shift
    if "$@" 2>&1; then
        echo "  OK  $name"
        PASS=$((PASS + 1))
    else
        echo "  FAIL $name"
        FAIL=$((FAIL + 1))
    fi
}

run_test "basic"              python3 "$SCRIPT_DIR/test_basic.py" "$PORT"
run_test "retained"           python3 "$SCRIPT_DIR/test_retained.py" "$PORT"
run_test "wildcards"          python3 "$SCRIPT_DIR/test_wildcards.py" "$PORT"
run_test "qos1"               python3 "$SCRIPT_DIR/test_qos1.py" "$PORT"
run_test "client_takeover"    python3 "$SCRIPT_DIR/test_takeover.py" "$PORT"

echo ""
echo "$PASS passed, $FAIL failed"
[ "$FAIL" -eq 0 ] || exit 1
