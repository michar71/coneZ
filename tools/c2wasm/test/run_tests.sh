#!/bin/bash
# c2wasm test runner
# Compiles all test .c files and validates the output WASM.
# reject_*.c files are expected to produce errors (nonzero exit).

set -u

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
C2WASM="$SCRIPT_DIR/../c2wasm"
TMPDIR=$(mktemp -d)
trap "rm -rf $TMPDIR" EXIT

pass=0
fail=0
skip=0

GREEN='\033[0;32m'
RED='\033[0;31m'
YELLOW='\033[0;33m'
NC='\033[0m'

# Check compiler exists
if [ ! -x "$C2WASM" ]; then
    echo "c2wasm not found at $C2WASM — run 'make' first"
    exit 1
fi

# Check for wasm-validate
VALIDATE=""
if command -v wasm-validate &>/dev/null; then
    VALIDATE="wasm-validate"
fi

echo "=== c2wasm test suite ==="
echo ""

# --- Positive tests: should compile and validate ---
for src in "$SCRIPT_DIR"/*.c; do
    name=$(basename "$src" .c)

    # Skip reject_* tests in this pass
    case "$name" in
        reject_*) continue ;;
    esac

    wasm="$TMPDIR/${name}.wasm"
    out=$("$C2WASM" "$src" -o "$wasm" 2>&1)
    rc=$?

    if [ $rc -ne 0 ]; then
        echo -e "  ${RED}FAIL${NC}  $name  (compile error, exit $rc)"
        [ -n "$out" ] && echo "        $out" | head -5
        fail=$((fail + 1))
        continue
    fi

    if [ ! -f "$wasm" ]; then
        echo -e "  ${RED}FAIL${NC}  $name  (no output file)"
        fail=$((fail + 1))
        continue
    fi

    size=$(stat -c%s "$wasm" 2>/dev/null || stat -f%z "$wasm" 2>/dev/null)

    # Validate WASM if tool is available
    if [ -n "$VALIDATE" ]; then
        vout=$($VALIDATE "$wasm" 2>&1)
        vrc=$?
        if [ $vrc -ne 0 ]; then
            echo -e "  ${RED}FAIL${NC}  $name  (wasm-validate failed)"
            echo "        $vout" | head -5
            fail=$((fail + 1))
            continue
        fi
    fi

    echo -e "  ${GREEN}PASS${NC}  $name  (${size} bytes)"
    pass=$((pass + 1))
done

echo ""

# --- Negative tests: should fail to compile ---
for src in "$SCRIPT_DIR"/reject_*.c; do
    [ -f "$src" ] || continue
    name=$(basename "$src" .c)
    wasm="$TMPDIR/${name}.wasm"

    out=$("$C2WASM" "$src" -o "$wasm" 2>&1)
    rc=$?

    if [ $rc -ne 0 ]; then
        # Extract first error line for display
        err=$(echo "$out" | head -1)
        echo -e "  ${GREEN}PASS${NC}  $name  (rejected: $err)"
        pass=$((pass + 1))
    else
        echo -e "  ${RED}FAIL${NC}  $name  (should have been rejected but compiled OK)"
        fail=$((fail + 1))
    fi
done

echo ""

# --- Also compile the examples from tools/wasm/examples/ ---
echo "--- examples ---"
EXAMPLES_DIR="$SCRIPT_DIR/../../wasm/examples"
if [ -d "$EXAMPLES_DIR" ]; then
    for src in "$EXAMPLES_DIR"/*.c; do
        [ -f "$src" ] || continue
        name=$(basename "$src" .c)
        wasm="$TMPDIR/${name}.wasm"

        out=$("$C2WASM" "$src" -o "$wasm" 2>&1)
        rc=$?

        if [ $rc -ne 0 ]; then
            echo -e "  ${RED}FAIL${NC}  examples/$name  (compile error, exit $rc)"
            [ -n "$out" ] && echo "        $out" | head -5
            fail=$((fail + 1))
            continue
        fi

        size=$(stat -c%s "$wasm" 2>/dev/null || stat -f%z "$wasm" 2>/dev/null)

        if [ -n "$VALIDATE" ]; then
            vout=$($VALIDATE "$wasm" 2>&1)
            vrc=$?
            if [ $vrc -ne 0 ]; then
                echo -e "  ${RED}FAIL${NC}  examples/$name  (wasm-validate failed)"
                echo "        $vout" | head -5
                fail=$((fail + 1))
                continue
            fi
        fi

        echo -e "  ${GREEN}PASS${NC}  examples/$name  (${size} bytes)"
        pass=$((pass + 1))
    done
fi

echo ""
echo "=== Results: $pass passed, $fail failed ==="

[ $fail -eq 0 ] && exit 0 || exit 1
