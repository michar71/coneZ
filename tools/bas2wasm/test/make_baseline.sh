#!/bin/bash
# Snapshot all test .wasm outputs for binary comparison
set -e
cd "$(dirname "$0")/.."
mkdir -p test/baseline

for f in test/*.bas; do
    base=$(basename "$f" .bas)
    # Skip reject tests (they should fail)
    case "$base" in reject_*) continue;; esac
    ./bas2wasm "$f" -o "test/baseline/${base}.wasm" 2>/dev/null
    echo "  $base"
done
echo "Baseline created in test/baseline/"
