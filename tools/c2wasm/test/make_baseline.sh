#!/bin/bash
# Snapshot all test .wasm outputs for binary comparison
set -e
cd "$(dirname "$0")/.."
mkdir -p test/baseline

for f in test/*.c test/examples/*.c; do
    [ -f "$f" ] || continue
    base=$(basename "$f" .c)
    # Skip reject tests (they should fail)
    case "$base" in reject_*) continue;; esac
    if [ -d "test/examples" ] && echo "$f" | grep -q "examples/"; then
        ./c2wasm "$f" -o "test/baseline/ex_${base}.wasm" 2>/dev/null
        echo "  examples/$base"
    else
        ./c2wasm "$f" -o "test/baseline/${base}.wasm" 2>/dev/null
        echo "  $base"
    fi
done
echo "Baseline created in test/baseline/"
