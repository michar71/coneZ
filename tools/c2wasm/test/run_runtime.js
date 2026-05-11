#!/usr/bin/env node
// c2wasm runtime test runner
// Compiles each .c file that has an `// EXPECTED:` comment block, instantiates
// the wasm with stub imports, calls setup(), captures the printed output, and
// diffs against the expected lines. Catches semantic regressions that
// wasm-validate's structural pass can't (e.g. a fold that produces a valid
// but wrong-valued constant).
//
// Test format:
//
//   // EXPECTED:
//   // 42
//   // 3.14
//   #include "conez_api.h"
//   void setup(void) { print_i32(42); print_f32(3.14f); }
//   void loop(void) {}
//
// Files without an EXPECTED block are skipped (the wasm-validate runner
// covers those structurally).

const fs = require('fs');
const path = require('path');
const { execFileSync } = require('child_process');

const testDir = __dirname;
const c2wasm = path.resolve(testDir, '..', 'c2wasm');
const tmpDir = fs.mkdtempSync('/tmp/c2wasm_runtime_');
process.on('exit', () => fs.rmSync(tmpDir, { recursive: true, force: true }));

const GREEN = '\x1b[0;32m', RED = '\x1b[0;31m', YELLOW = '\x1b[0;33m', NC = '\x1b[0m';

if (!fs.existsSync(c2wasm)) {
    console.error(`c2wasm not found at ${c2wasm} — run 'make' first`);
    process.exit(1);
}

function parseExpected(src) {
    const m = src.match(/^\/\/\s*EXPECTED:\s*\n((?:\/\/.*\n)+)/m);
    if (!m) return null;
    return m[1].split('\n')
        .filter(l => l.startsWith('//'))
        .map(l => l.replace(/^\/\/\s?/, ''))
        .join('\n').trim();
}

// Format floats to match what print_f32/print_f64 typically render.
// c2wasm prints via host_printf with %g-like behavior; JS Number.toString
// renders 3.0 as "3", 1.5 as "1.5" — close enough for these tests if the
// expected output uses the same rendering.
function fmtFloat(v) { return String(v); }

function makeImports(state) {
    const memU8 = () => new Uint8Array(state.instance.exports.memory.buffer);
    const readStr = (p) => {
        const mem = memU8();
        let s = '';
        while (mem[p] !== 0 && p < mem.length) s += String.fromCharCode(mem[p++]);
        return s;
    };
    const alloc = (n) => {
        const p = state.heap;
        state.heap = (state.heap + n + 7) & ~7;
        return p;
    };
    const noop = () => 0;
    const ret0 = () => 0;

    return {
        env: {
            print_i32: n => { state.output += (n | 0) + '\n'; },
            print_f32: f => { state.output += fmtFloat(Math.fround(f)) + '\n'; },
            print_f64: d => { state.output += fmtFloat(d) + '\n'; },
            print_i64: l => { state.output += l.toString() + '\n'; },
            print_str: (p, len) => {
                const mem = memU8();
                let s = '';
                for (let i = 0; i < len; i++) s += String.fromCharCode(mem[p + i]);
                state.output += s + '\n';
            },
            host_printf: (fmt, args) => { state.output += readStr(fmt); return 0; },
            host_snprintf: () => 0,
            host_sscanf: () => 0,
            malloc: n => alloc(Number(n)),
            free: noop,
            calloc: (n, sz) => {
                const total = Number(n) * Number(sz);
                const p = alloc(total);
                memU8().fill(0, p, p + total);
                return p;
            },
            realloc: (p, sz) => alloc(Number(sz)),
            // math (float and double)
            sinf: Math.sin, cosf: Math.cos, tanf: Math.tan,
            asinf: Math.asin, acosf: Math.acos, atanf: Math.atan,
            atan2f: Math.atan2, powf: Math.pow,
            expf: Math.exp, logf: Math.log, log2f: Math.log2,
            fmodf: (a, b) => a % b,
            sin: Math.sin, cos: Math.cos, tan: Math.tan,
            asin: Math.asin, acos: Math.acos, atan: Math.atan,
            atan2: Math.atan2, pow: Math.pow,
            exp: Math.exp, log: Math.log, log2: Math.log2,
            fmod: (a, b) => a % b,
            // timing / control
            delay_ms: noop, millis: () => 0, millis64: () => 0n,
            get_epoch_ms: () => 0n,
            should_stop: ret0, wait_pps: ret0, wait_param: ret0,
            // curve
            lerp: (a, b, t) => Math.round(a + (b - a) * t / 256),
            larp: (a, b, t) => a + Math.round((b - a) * t / 256),
            larpf: (a, b, t) => a + (b - a) * t,
            // LED / sensors / GPS / IMU / params — stubbed to 0
            led_set_pixel: noop, led_fill: noop, led_show: noop,
            led_count: ret0, led_set_gamma: noop, led_set_buffer: noop,
            led_shift: noop, led_rotate: noop, led_reverse: noop,
            led_set_pixel_hsv: noop, led_fill_hsv: noop,
            hsv_to_rgb: ret0, rgb_to_hsv: ret0, led_gamma8: ret0,
            pin_set: noop, pin_clear: noop, pin_read: ret0, analog_read: ret0,
            get_lat: () => 0, get_lon: () => 0, get_alt: () => 0,
            get_speed: () => 0, get_dir: () => 0, gps_valid: ret0,
            get_origin_lat: () => 0, get_origin_lon: () => 0,
            has_origin: ret0, origin_dist: () => 0, origin_bearing: () => 0,
            get_roll: () => 0, get_pitch: () => 0, get_yaw: () => 0,
            get_acc_x: () => 0, get_acc_y: () => 0, get_acc_z: () => 0,
            imu_valid: ret0,
            get_temp: () => 0, get_humidity: () => 0,
            get_brightness: () => 0, get_bat_voltage: () => 0,
            get_solar_voltage: () => 0,
            get_sunrise: () => 0n, get_sunset: () => 0n,
            sun_valid: ret0, is_daylight: ret0,
            cue_playing: ret0, cue_elapsed: () => 0n,
            get_year: ret0, get_month: ret0, get_day: ret0,
            get_hour: ret0, get_minute: ret0, get_second: ret0,
            get_day_of_week: ret0, get_day_of_year: ret0,
            get_is_leap_year: ret0, time_valid: ret0,
            get_param: ret0, set_param: noop,
            // LUT
            lut_load: ret0, lut_get: ret0, lut_size: ret0,
            lut_set: noop, lut_save: ret0, lut_check: ret0,
            // file I/O (no-op stubs — file_io.c-style tests will misbehave
            // but those aren't fold tests)
            file_open: ret0, file_close: ret0, file_read: ret0,
            file_write: ret0, file_size: ret0, file_seek: ret0,
            file_tell: ret0, file_exists: ret0, file_delete: ret0,
            file_rename: ret0,
            // compression — stubs
            inflate_file: ret0, inflate_file_to_mem: ret0, inflate_mem: ret0,
            deflate_file: ret0, deflate_mem_to_file: ret0, deflate_mem: ret0,
        }
    };
}

let pass = 0, fail = 0, skip = 0;

console.log('=== c2wasm runtime test suite ===\n');

const files = fs.readdirSync(testDir).filter(f => f.endsWith('.c')).sort();
for (const file of files) {
    if (file.startsWith('reject_')) { skip++; continue; }
    const fullpath = path.join(testDir, file);
    const src = fs.readFileSync(fullpath, 'utf8');
    const expected = parseExpected(src);
    if (expected === null) { skip++; continue; }

    const name = path.basename(file, '.c');
    const wasmPath = path.join(tmpDir, name + '.wasm');
    try {
        execFileSync(c2wasm, [fullpath, '-o', wasmPath], { stdio: 'pipe' });
    } catch (e) {
        console.log(`  ${RED}FAIL${NC}  ${name}  (compile error)`);
        fail++;
        continue;
    }

    const bytes = fs.readFileSync(wasmPath);
    const state = { output: '', heap: 0xC000, instance: null };
    try {
        const module = new WebAssembly.Module(bytes);
        const imports = makeImports(state);
        const inst = new WebAssembly.Instance(module, imports);
        state.instance = inst;
        if (inst.exports.setup) inst.exports.setup();
        if (inst.exports.loop) inst.exports.loop();
    } catch (e) {
        console.log(`  ${RED}FAIL${NC}  ${name}  (run error: ${e.message})`);
        fail++;
        continue;
    }

    const got = state.output.trim();
    if (got === expected) {
        console.log(`  ${GREEN}PASS${NC}  ${name}`);
        pass++;
    } else {
        console.log(`  ${RED}FAIL${NC}  ${name}`);
        console.log(`    expected: ${JSON.stringify(expected)}`);
        console.log(`    got:      ${JSON.stringify(got)}`);
        fail++;
    }
}

console.log(`\n=== Runtime results: ${pass} passed, ${fail} failed, ${skip} skipped ===`);
process.exit(fail === 0 ? 0 : 1);
