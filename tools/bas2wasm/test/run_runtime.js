#!/usr/bin/env node
// bas2wasm runtime test runner — mirrors c2wasm's runner but with the
// bas2wasm-specific imports (host_printf with %/&/$ format spec, the
// basic_str_* string pool API, print_i32/f32/i64, etc.).
//
// Test format:
//
//   ' EXPECTED:
//   ' 14
//   ' 21
//   A = 2 + 3 * 4
//   > A
//   B = (1+2) * (3+4)
//   > B
//
// Files without an EXPECTED block are skipped (the wasm-validate runner
// covers those structurally).

const fs = require('fs');
const path = require('path');
const { execFileSync } = require('child_process');

const testDir = __dirname;
const bas2wasm = path.resolve(testDir, '..', 'bas2wasm');
const tmpDir = fs.mkdtempSync('/tmp/bas2wasm_runtime_');
process.on('exit', () => fs.rmSync(tmpDir, { recursive: true, force: true }));

const GREEN = '\x1b[0;32m', RED = '\x1b[0;31m', NC = '\x1b[0m';

if (!fs.existsSync(bas2wasm)) {
    console.error(`bas2wasm not found at ${bas2wasm} — run 'make' first`);
    process.exit(1);
}

function parseExpected(src) {
    // BASIC comments start with ' — match `' EXPECTED:` then consecutive
    // block lines. A block line is an apostrophe followed by a space (or a
    // bare apostrophe). This deliberately excludes `'$INCLUDE:` and other
    // `'<nonspace>` metacommands so they terminate the block.
    const m = src.match(/^'\s*EXPECTED:\s*\n((?:'(?: [^\n]*)?\n)+)/m);
    if (!m) return null;
    return m[1].split('\n')
        .filter(l => l.startsWith("'"))
        .map(l => l.replace(/^'\s?/, ''))
        .join('\n').trim();
}

// String pool layout from firmware/src/wasm/wasm_imports_string.cpp:
//   [0x0000, 0x8000)  → constants/data (set by compiler at boot)
//   [0x8000, 0xF000)  → pool (host-managed, bump allocator here)
//   [0xF000, 0xF100)  → FMT_BUF used by host_printf
const STR_POOL_START = 0x8000;
const STR_POOL_END   = 0xF000;
const FMT_BUF        = 0xF000;

// Match firmware's printf("%f", val) then strip trailing zeros — effectively
// printf "%g" rendering at f32 precision. Math.fround(3.14).toString() in JS
// gives "3.140000104904175" which doesn't match what users see on the device,
// so we round to 6 decimal places (default %f precision) and strip trailing
// zeros to get back to the natural short form (3.14, 3.5, 6, etc.).
function fmtFloat(v) {
    v = Math.fround(v);
    if (!isFinite(v)) return v.toString();
    if (v === 0) return '0';
    let s = v.toFixed(6);
    if (s.includes('.')) s = s.replace(/0+$/, '').replace(/\.$/, '');
    return s;
}

function makeImports(state) {
    const memU8 = () => new Uint8Array(state.instance.exports.memory.buffer);
    const memDV = () => new DataView(state.instance.exports.memory.buffer);
    const readStr = (p) => {
        const mem = memU8();
        let s = '';
        while (p < mem.length && mem[p] !== 0) s += String.fromCharCode(mem[p++]);
        return s;
    };
    const writeStr = (p, s) => {
        const mem = memU8();
        for (let i = 0; i < s.length; i++) mem[p + i] = s.charCodeAt(i) & 0xff;
        mem[p + s.length] = 0;
    };
    const strAlloc = (size) => {
        // bump allocator in pool region
        if (state.strBump + size > STR_POOL_END) return 0;
        const p = state.strBump;
        state.strBump += size;
        return p;
    };
    const strCopy = (src) => {
        const s = readStr(src);
        const p = strAlloc(s.length + 1);
        writeStr(p, s);
        return p;
    };

    const noop = () => 0;
    const ret0 = () => 0;

    return {
        env: {
            // ---- Output ----
            print_i32: n => { state.output += (n | 0).toString() + '\n'; },
            print_i64: l => { state.output += l.toString() + '\n'; },
            print_f32: f => { state.output += fmtFloat(Math.fround(f)) + '\n'; },
            print_str: (p, len) => {
                const mem = memU8();
                let s = '';
                for (let i = 0; i < len; i++) s += String.fromCharCode(mem[p + i]);
                state.output += s + '\n';
            },
            host_printf: (fmt_ptr, buf_ptr) => {
                // Read format string, interpret %d/%f/%s reading from buf_ptr.
                // Each conversion consumes one 4-byte cell from the buffer.
                const fmt = readStr(fmt_ptr);
                const dv = memDV();
                let out = '';
                let arg = 0;
                for (let i = 0; i < fmt.length; i++) {
                    const c = fmt.charCodeAt(i);
                    if (c === 0x25 /* % */ && i + 1 < fmt.length) {
                        const spec = fmt[i + 1];
                        const cell = buf_ptr + arg * 4;
                        if (spec === 'd') {
                            out += dv.getInt32(cell, true).toString();
                            arg++; i++;
                        } else if (spec === 'f') {
                            out += fmtFloat(dv.getFloat32(cell, true));
                            arg++; i++;
                        } else if (spec === 's') {
                            const sp = dv.getInt32(cell, true);
                            out += readStr(sp);
                            arg++; i++;
                        } else if (spec === '%') {
                            out += '%'; i++;
                        } else {
                            out += '%';   // unknown spec — pass through
                        }
                    } else {
                        out += fmt[i];
                    }
                }
                state.output += out;
                return 0;
            },

            // ---- String pool ----
            basic_str_alloc: (n) => strAlloc(Number(n) + 1),
            basic_str_free: noop,
            basic_str_len: (p) => readStr(p).length,
            basic_str_copy: (p) => strCopy(p),
            basic_str_concat: (a, b) => {
                const s = readStr(a) + readStr(b);
                const p = strAlloc(s.length + 1);
                writeStr(p, s);
                return p;
            },
            basic_str_cmp: (a, b) => {
                const sa = readStr(a), sb = readStr(b);
                if (sa < sb) return -1;
                if (sa > sb) return 1;
                return 0;
            },
            basic_str_mid: (src, start, len) => {
                const s = readStr(src).substring(start - 1, start - 1 + len);
                const p = strAlloc(s.length + 1); writeStr(p, s); return p;
            },
            basic_str_left: (src, n) => {
                const s = readStr(src).substring(0, n);
                const p = strAlloc(s.length + 1); writeStr(p, s); return p;
            },
            basic_str_right: (src, n) => {
                const full = readStr(src);
                const s = full.substring(Math.max(0, full.length - n));
                const p = strAlloc(s.length + 1); writeStr(p, s); return p;
            },
            basic_str_chr: (code) => {
                const p = strAlloc(2);
                const mem = memU8(); mem[p] = code & 0xff; mem[p+1] = 0; return p;
            },
            basic_str_asc: (src) => readStr(src).charCodeAt(0) || 0,
            basic_str_from_int: (n) => {
                const s = (n | 0).toString();
                const p = strAlloc(s.length + 1); writeStr(p, s); return p;
            },
            basic_str_from_i64: (n) => {
                const s = n.toString();
                const p = strAlloc(s.length + 1); writeStr(p, s); return p;
            },
            basic_str_from_float: (f) => {
                const s = fmtFloat(Math.fround(f));
                const p = strAlloc(s.length + 1); writeStr(p, s); return p;
            },
            basic_str_to_int: (src) => parseInt(readStr(src), 10) | 0,
            basic_str_to_i64: (src) => BigInt(parseInt(readStr(src), 10) || 0),
            basic_str_to_float: (src) => Math.fround(parseFloat(readStr(src)) || 0),
            basic_str_upper: (src) => {
                const s = readStr(src).toUpperCase();
                const p = strAlloc(s.length + 1); writeStr(p, s); return p;
            },
            basic_str_lower: (src) => {
                const s = readStr(src).toLowerCase();
                const p = strAlloc(s.length + 1); writeStr(p, s); return p;
            },
            basic_str_instr: (src, sub, start) => {
                const s = readStr(src), sb = readStr(sub);
                const idx = s.indexOf(sb, Math.max(0, start - 1));
                return idx < 0 ? 0 : idx + 1;
            },
            basic_str_trim: (src) => {
                const s = readStr(src).trim();
                const p = strAlloc(s.length + 1); writeStr(p, s); return p;
            },
            basic_str_ltrim: (src) => {
                const s = readStr(src).replace(/^\s+/, '');
                const p = strAlloc(s.length + 1); writeStr(p, s); return p;
            },
            basic_str_rtrim: (src) => {
                const s = readStr(src).replace(/\s+$/, '');
                const p = strAlloc(s.length + 1); writeStr(p, s); return p;
            },
            // basic_str_repeat(n, char_code) -> n copies of CHR$(char_code)
            // (matches firmware m3_str_repeat; STRING$(n,code) maps here).
            basic_str_repeat: (n, code) => {
                const cnt = Math.max(0, Math.min(4096, n));
                const s = String.fromCharCode(code & 0xFF).repeat(cnt);
                const p = strAlloc(s.length + 1); writeStr(p, s); return p;
            },
            basic_str_space: (n) => {
                const s = ' '.repeat(Math.max(0, n));
                const p = strAlloc(s.length + 1); writeStr(p, s); return p;
            },
            basic_str_hex: (n) => {
                const s = ((n >>> 0)).toString(16).toUpperCase();
                const p = strAlloc(s.length + 1); writeStr(p, s); return p;
            },
            basic_str_oct: (n) => {
                const s = ((n >>> 0)).toString(8);
                const p = strAlloc(s.length + 1); writeStr(p, s); return p;
            },
            basic_str_mid_assign: (target, start, len, replacement) => {
                // Replace in place; return target pointer
                const mem = memU8();
                const rep = readStr(replacement);
                let pos = start - 1;
                for (let i = 0; i < len && i < rep.length; i++) {
                    if (mem[target + pos + i] === 0) break;
                    mem[target + pos + i] = rep.charCodeAt(i) & 0xff;
                }
                return target;
            },

            // ---- Math ----
            sinf: Math.sin, cosf: Math.cos, tanf: Math.tan,
            atan2f: Math.atan2, powf: Math.pow,
            expf: Math.exp, logf: Math.log, log2f: Math.log2,
            fmodf: (a, b) => a % b,

            // ---- Allocators (calloc/realloc/free for DIM arrays) ----
            calloc: (n, sz) => {
                const total = Number(n) * Number(sz);
                // Allocate from a separate heap region (below pool)
                const p = state.dimBump;
                state.dimBump = (state.dimBump + total + 7) & ~7;
                memU8().fill(0, p, p + total);
                return p;
            },
            realloc: (p, sz) => {
                // crude: alloc new, copy from old. We don't know old size, so
                // copy up to sz bytes (might read garbage at the end but it'll
                // be valid for tests that grow their arrays).
                const newP = state.dimBump;
                state.dimBump = (state.dimBump + Number(sz) + 7) & ~7;
                const mem = memU8();
                for (let i = 0; i < Number(sz); i++) mem[newP + i] = mem[p + i] || 0;
                return newP;
            },
            free: noop,

            // ---- Timing / control ----
            delay_ms: noop, millis: () => 0, millis64: () => 0n,
            get_epoch_ms: () => 0n,
            should_stop: ret0, wait_pps: ret0, wait_param: ret0,

            // ---- Stubs for unused-in-tests imports ----
            led_set_pixel: noop, led_fill: noop, led_show: noop,
            led_count: ret0, led_set_gamma: noop, led_set_buffer: noop,
            led_shift: noop, led_rotate: noop, led_reverse: noop,
            led_set_pixel_hsv: noop, led_fill_hsv: noop,
            hsv_to_rgb: ret0, rgb_to_hsv: ret0, led_gamma8: n => n & 0xff,

            get_param: ret0, set_param: noop,
            get_lat: () => 0, get_lon: () => 0, get_alt: () => 0,
            get_speed: () => 0, get_dir: () => 0, gps_valid: ret0,
            has_origin: ret0, origin_dist: () => 0, origin_bearing: () => 0,
            gps_present: ret0, imu_present: ret0,
            get_roll: () => 0, get_pitch: () => 0, get_yaw: () => 0,
            get_acc_x: () => 0, get_acc_y: () => 0, get_acc_z: () => 0,
            imu_valid: ret0,
            get_temp: () => 0, get_humidity: () => 0,
            get_brightness: () => 0,
            get_battery_percentage: () => 0, get_battery_runtime: () => 0,
            get_sun_azimuth: () => 0, get_sun_elevation: () => 0,
            get_year: ret0, get_month: ret0, get_day: ret0,
            get_hour: ret0, get_minute: ret0, get_second: ret0,
            get_day_of_week: ret0, get_day_of_year: ret0,
            get_is_leap_year: ret0, time_valid: ret0,
            cue_playing: ret0, cue_elapsed: () => 0n,

            lut_load: ret0, lut_get: ret0, lut_size: ret0,
            lut_set: noop, lut_save: ret0, lut_check: ret0,

            pin_set: noop, pin_clear: noop, pin_read: ret0, analog_read: ret0,

            file_open: ret0, file_close: noop, file_read: ret0,
            file_write: ret0, file_size: ret0, file_seek: ret0,
            file_tell: ret0, file_exists: ret0, file_delete: ret0,
            file_rename: ret0, file_print: ret0, file_input: ret0,
            file_eof: () => 1,   // EOF immediately so loops terminate
            file_input_line: ret0, basic_file_input_num: ret0,
        }
    };
}

// Run a single test file; returns { ok, output } or { error }.
function runFile(fullpath, name) {
    const wasmPath = path.join(tmpDir, name + '.wasm');
    try {
        execFileSync(bas2wasm, [fullpath, '-o', wasmPath], { stdio: 'pipe' });
    } catch (e) {
        return { error: 'compile error' };
    }
    const bytes = fs.readFileSync(wasmPath);
    const state = { output: '', instance: null, strBump: STR_POOL_START, dimBump: 0x100 };
    try {
        const module = new WebAssembly.Module(bytes);
        const inst = new WebAssembly.Instance(module, makeImports(state));
        state.instance = inst;
        if (inst.exports.setup) inst.exports.setup();   // bas2wasm exports setup()
    } catch (e) {
        return { error: 'run error: ' + e.message };
    }
    return { ok: true, output: state.output.trim() };
}

// --generate: snapshot current output into a `' EXPECTED:` block for any
// non-reject test that lacks one, runs cleanly, and prints something.
// Inserted at the top of the file (BASIC has no __LINE__-style sensitivity).
if (process.argv.includes('--generate')) {
    let added = 0, skipped = 0;
    const files = fs.readdirSync(testDir).filter(f => f.endsWith('.bas')).sort();
    for (const file of files) {
        if (file.startsWith('reject_')) continue;
        const fullpath = path.join(testDir, file);
        let src = fs.readFileSync(fullpath, 'utf8');
        if (parseExpected(src) !== null) continue;
        const name = path.basename(file, '.bas');
        const r = runFile(fullpath, name);
        if (r.error || !r.output) { skipped++; continue; }
        const block = "' EXPECTED:\n" +
            r.output.split('\n').map(l => l === '' ? "'" : "' " + l).join('\n') +
            '\n';
        // Blank line separates the block from the program's own leading
        // `' comment`, so parseExpected's `'`-line run terminates there.
        fs.writeFileSync(fullpath, block + '\n' + src);
        console.log(`  ${GREEN}gen${NC}   ${name}`);
        added++;
    }
    console.log(`\n=== Generated ${added} EXPECTED blocks; ${skipped} skipped (no clean output) ===`);
    process.exit(0);
}

let pass = 0, fail = 0, skip = 0;

console.log('=== bas2wasm runtime test suite ===\n');

const files = fs.readdirSync(testDir).filter(f => f.endsWith('.bas')).sort();
for (const file of files) {
    if (file.startsWith('reject_')) { skip++; continue; }
    const fullpath = path.join(testDir, file);
    const src = fs.readFileSync(fullpath, 'utf8');
    const expected = parseExpected(src);
    if (expected === null) { skip++; continue; }

    const name = path.basename(file, '.bas');
    const r = runFile(fullpath, name);
    if (r.error) {
        console.log(`  ${RED}FAIL${NC}  ${name}  (${r.error})`);
        fail++;
        continue;
    }
    if (r.output === expected) {
        console.log(`  ${GREEN}PASS${NC}  ${name}`);
        pass++;
    } else {
        console.log(`  ${RED}FAIL${NC}  ${name}`);
        console.log(`    expected: ${JSON.stringify(expected)}`);
        console.log(`    got:      ${JSON.stringify(r.output)}`);
        fail++;
    }
}

console.log(`\n=== Runtime results: ${pass} passed, ${fail} failed, ${skip} skipped ===`);
process.exit(fail === 0 ? 0 : 1);
