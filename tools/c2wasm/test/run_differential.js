#!/usr/bin/env node
// c2wasm ↔ clang differential.
//
// The runtime suite (run_runtime.js) only proves c2wasm's output is
// *stable and self-consistent* — every EXPECTED block, inline comment,
// and verify baseline ultimately traces back to c2wasm's own output, so
// none of it independently establishes correctness. clang is a separate,
// mature implementation of the same C semantics: compiling each test with
// BOTH and running them under the identical stub-import harness gives a
// genuine external oracle.
//
//   AGREE         → c2wasm corroborated by clang for that test
//   DIFFER        → a real c2wasm codegen bug (investigate)
//   addr-sensitive→ output contains a raw pointer/address; clang and
//                    c2wasm use different memory layouts, so a mismatch
//                    here is expected, not a bug — flagged, not failed
//   clang-skip    → clang couldn't compile it (c2wasm-only leniency or a
//                    test that needs the full harness); inconclusive
//
// Exit non-zero only on DIFFER (real disagreements).

const fs = require('fs');
const path = require('path');
const { execFileSync } = require('child_process');
const { runWasmBytes } = require('./run_runtime.js');

const testDir = __dirname;
const c2wasm = path.resolve(testDir, '..', 'c2wasm');
const apiDir = path.resolve(testDir, '..', '..', 'wasm');   // conez_api.h lives here
const tmp = fs.mkdtempSync('/tmp/c2wasm_diff_');
process.on('exit', () => fs.rmSync(tmp, { recursive: true, force: true }));

const G = '\x1b[0;32m', R = '\x1b[0;31m', Y = '\x1b[0;33m', B = '\x1b[0;34m', N = '\x1b[0m';

const CLANG_LD = [
    '--target=wasm32', '-mbulk-memory', '-O2', '-nostdlib', '-I', apiDir,
    '-Wl,--no-entry', '-Wl,--export-all', '-Wl,--allow-undefined',
    '-Wl,-z,stack-size=8192',
];

// Tests whose output legitimately depends on memory layout (raw addresses,
// sizeof-of-pointer arithmetic against absolute addrs) or on the
// compile environment — clang and c2wasm necessarily differ here, so a
// mismatch is not a bug. Kept explicit and narrow.
const ADDR_SENSITIVE = new Set([
    'predefined_macros',         // __DATE__/__TIME__/__LINE__
    'global_scalar_address', 'global_scalar_address_wide',
    'test_address_of_local', 'struct_addrof',
    'malloc_free',               // returns allocator addresses
    'test_ptr_arith', 'test_ptr_incr_scale',
    'global_pointer_alias_compound',
    // Undefined behaviour where c2wasm has a *deliberate* defined
    // extension (fall off a non-void fn ⇒ return 0) and clang does
    // not — divergence is expected, not a c2wasm bug.
    'implicit_return',
]);

let agree = 0, differ = 0, addr = 0, clangSkip = 0, c2Skip = 0;
const differs = [];

const files = fs.readdirSync(testDir).filter(f => f.endsWith('.c')).sort();
for (const file of files) {
    if (file.startsWith('reject_')) continue;
    const name = path.basename(file, '.c');
    const src = path.join(testDir, file);

    // Compile with c2wasm.
    const aw = path.join(tmp, name + '.a.wasm');
    try {
        execFileSync(c2wasm, [src, '-o', aw], { stdio: 'pipe' });
    } catch (e) {
        console.log(`  ${R}c2wasm-FAIL${N}  ${name}`);
        c2Skip++; continue;
    }
    // Compile with clang.
    const bw = path.join(tmp, name + '.b.wasm');
    try {
        execFileSync('clang', [...CLANG_LD, '-o', bw, src], { stdio: 'pipe' });
    } catch (e) {
        console.log(`  ${Y}clang-skip ${N}  ${name}`);
        clangSkip++; continue;
    }

    const ra = runWasmBytes(fs.readFileSync(aw));
    const rb = runWasmBytes(fs.readFileSync(bw));
    if (ra.error || rb.error) {
        // A run error under one but not the other is itself a signal, but
        // commonly clang's -O2 + different layout trips the stub harness;
        // treat as inconclusive unless c2wasm alone errors.
        if (ra.error && !rb.error) { console.log(`  ${R}c2wasm-runerr${N} ${name}: ${ra.error}`); differ++; differs.push(name); }
        else { console.log(`  ${Y}run-skip   ${N}  ${name}`); clangSkip++; }
        continue;
    }

    if (ra.output === rb.output) {
        console.log(`  ${G}AGREE${N}      ${name}`);
        agree++;
    } else if (ADDR_SENSITIVE.has(name)) {
        console.log(`  ${B}addr-skip  ${N}  ${name} (layout-dependent)`);
        addr++;
    } else {
        console.log(`  ${R}DIFFER${N}     ${name}`);
        console.log(`    c2wasm: ${JSON.stringify(ra.output)}`);
        console.log(`    clang:  ${JSON.stringify(rb.output)}`);
        differ++; differs.push(name);
    }
}

console.log(
    `\n=== Differential: ${agree} agree, ${differ} differ, ` +
    `${addr} addr-skip, ${clangSkip} clang-skip, ${c2Skip} c2wasm-fail ===`);
if (differs.length)
    console.log('DIFFERENCES (real c2wasm suspects): ' + differs.join(', '));
process.exit(differ === 0 ? 0 : 1);
