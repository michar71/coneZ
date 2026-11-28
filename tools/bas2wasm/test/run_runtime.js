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

// C printf("%g") of an f32 -- what both hosts use for `>` (print_f32) and
// STR$ (basic_str_from_float): six significant digits, trailing zeros
// stripped, exponent form below 1e-4 or at/above 1e6. So 1.5 prints "1.5",
// 40.762173 prints "40.7622" and 1234567 prints "1.23457e+06".
function fmtFloat(v) {
    v = Math.fround(v);
    if (Number.isNaN(v)) return 'nan';
    if (!isFinite(v)) return v < 0 ? '-inf' : 'inf';
    if (v === 0) return Object.is(v, -0) ? '-0' : '0';
    const strip = t => t.includes('.') ? t.replace(/0+$/, '').replace(/\.$/, '') : t;
    const [mant, ex] = v.toExponential(5).split('e');
    const e = parseInt(ex, 10);
    if (e < -4 || e >= 6)
        return strip(mant) + 'e' + (e < 0 ? '-' : '+') + String(Math.abs(e)).padStart(2, '0');
    return strip(v.toFixed(5 - e));
}

// C printf("%f") of a double argument (FORMAT passes floats as f64): always
// six decimals -- 1.5 prints "1.500000" -- WITHOUT rounding to f32 first, since
// an integer printed under & arrives as an exact double. (This used to strip
// the trailing zeros, which no real host does; running the tests on the
// firmware's own code -- `make test-host` -- showed it.)
function fmtDouble(v) {
    if (Number.isNaN(v)) return 'nan';
    if (!isFinite(v)) return v < 0 ? '-inf' : 'inf';
    // toFixed switches to exponent form at 1e21; %f never does.
    if (Math.abs(v) >= 1e21) return BigInt(v).toString() + '.000000';
    return v.toFixed(6);
}

function makeImports(state) {
    const memU8 = () => new Uint8Array(state.instance.exports.memory.buffer);
    const memDV = () => new DataView(state.instance.exports.memory.buffer);
    const readStr = (p) => {
        if (p === 0) return '';     // pointer 0 = "no string", as on both real hosts
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
    // ---- Allocators: a model of the firmware's (wasm_imports_string.cpp) ----
    // Block tables with first-fit, splitting and coalescing; the string pool has
    // 128 records and the low heap 32, like the device. Frees are REAL and the
    // freed bytes are poisoned, so a program that uses a string after freeing
    // it, frees one twice, or leaks until the pool is exhausted FAILS here.
    // (This harness used to bump-allocate and ignore every free, which hid all
    // three: `B$ = A$` aliasing and per-statement string leaks passed.)
    const mkTable = (max, lo, hi) => ({ a: [], max, bump: lo, lo, hi });
    if (!state.pool) state.pool = mkTable(128, STR_POOL_START, STR_POOL_END);
    const spare = (t) => {
        for (let i = 0; i < t.a.length; i++) if (!t.a[i].in_use && t.a[i].size === 0) return i;
        if (t.a.length < t.max) { t.a.push({ offset: 0, size: 0, in_use: false }); return t.a.length - 1; }
        return -1;
    };
    const talloc = (t, size) => {
        if (size <= 0) size = 1;
        if (size > t.hi - t.lo) return 0;
        size = (size + 3) & ~3;
        for (let i = 0; i < t.a.length; i++) {
            const r = t.a[i];
            if (!r.in_use && r.size >= size) {
                const rest = r.size - size;
                r.in_use = true;
                if (rest >= 4) {
                    const sl = spare(t);
                    if (sl >= 0) { t.a[sl] = { offset: r.offset + size, size: rest, in_use: false }; r.size = size; }
                }
                memU8().fill(0, r.offset, r.offset + size);
                return r.offset;
            }
        }
        if (t.bump + size > t.hi) return 0;
        const sl = spare(t);
        if (sl < 0) return 0;
        const off = t.bump; t.bump += size;
        t.a[sl] = { offset: off, size, in_use: true };
        memU8().fill(0, off, off + size);
        return off;
    };
    const tfree = (t, ptr, what) => {
        let idx = t.a.findIndex(r => r.offset === ptr && r.in_use);
        if (idx < 0) { state.problems.push(`${what} of a pointer that is not allocated (0x${ptr.toString(16)}): double or invalid free`); return; }
        memU8().fill(0x23, ptr, ptr + t.a[idx].size);    // poison: '#'
        t.a[idx].in_use = false;
        for (let i = 0; i < t.a.length; i++) if (i !== idx && !t.a[i].in_use && t.a[i].size > 0 && t.a[idx].offset + t.a[idx].size === t.a[i].offset) { t.a[idx].size += t.a[i].size; t.a[i].size = 0; break; }
        for (let i = 0; i < t.a.length; i++) if (i !== idx && !t.a[i].in_use && t.a[i].size > 0 && t.a[i].offset + t.a[i].size === t.a[idx].offset) { t.a[i].size += t.a[idx].size; t.a[idx].size = 0; idx = i; break; }
        if (t.a[idx].offset + t.a[idx].size === t.bump) { t.bump = t.a[idx].offset; t.a[idx].size = 0; }
        while (t.a.length > 0 && t.a[t.a.length - 1].size === 0) t.a.pop();
    };
    const lowHeap = () => {
        if (!state.low) {
            const hp = state.instance.exports._heap_ptr;
            state.low = mkTable(32, hp ? hp.value : 0, STR_POOL_START);
        }
        return state.low;
    };
    const strAlloc = (size) => {
        const p = talloc(state.pool, size);
        if (!p) state.problems.push('string pool exhausted (128 blocks / 28 KB) -- a string leak?');
        return p;
    };
    const strFree = (p) => {
        p >>>= 0;
        if (p < STR_POOL_START || p >= STR_POOL_END) return;   // literal or null: no-op, as on the hosts
        tfree(state.pool, p, 'str_free');
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
            print_str: (p, len) => {         // no newline, as on the hosts
                const mem = memU8();
                let s = '';
                for (let i = 0; i < len; i++) s += String.fromCharCode(mem[p + i]);
                state.output += s;
            },
            host_printf: (fmt_ptr, buf_ptr) => {
                // Read format string, interpret %d/%f/%s reading from buf_ptr.
                // The argument layout is the one the real hosts read
                // (firmware wasm_format.cpp, simulator sim_wasm_imports_format.cpp):
                // %d and %s take a 4-byte cell; %f takes a DOUBLE -- 8 bytes at
                // an 8-byte-aligned address. This harness used to read a
                // Float32 from a 4-byte cell, which matched what bas2wasm
                // emitted but not what any real host does, so FORMAT "&"
                // passed here while printing garbage on a device.
                const fmt = readStr(fmt_ptr);
                const dv = memDV();
                let out = '';
                let ap = buf_ptr;
                for (let i = 0; i < fmt.length; i++) {
                    const c = fmt.charCodeAt(i);
                    if (c === 0x25 /* % */ && i + 1 < fmt.length) {
                        const spec = fmt[i + 1];
                        if (spec === 'd') {
                            out += dv.getInt32(ap, true).toString();
                            ap += 4; i++;
                        } else if (spec === 'f') {
                            ap = (ap + 7) & ~7;
                            out += fmtDouble(dv.getFloat64(ap, true));
                            ap += 8; i++;
                        } else if (spec === 's') {
                            const sp = dv.getInt32(ap, true);
                            out += readStr(sp);
                            ap += 4; i++;
                        } else if (spec === '%') {
                            out += '%'; i++;
                        } else {
                            out += '%';   // unknown spec — pass through
                        }
                    } else {
                        out += fmt[i];
                    }
                }
                // The hosts format into a 256-byte buffer: a FORMAT line is
                // cut off after 255 characters.
                if (out.length > 255) out = out.slice(0, 255);
                state.output += out;
                return out.length;
            },

            // ---- String pool ----
            basic_str_alloc: (n) => strAlloc(Number(n) + 1),
            basic_str_free: strFree,
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
            // As the hosts: only the first 255 characters are searched, for a
            // needle of at most 127, and a start past the end finds nothing.
            basic_str_instr: (src, sub, start) => {
                if (src === 0 || sub === 0) return 0;
                const s = readStr(src).slice(0, 255), sb = readStr(sub).slice(0, 127);
                const from = Math.max(0, start - 1);
                if (from >= s.length) return 0;
                const idx = s.indexOf(sb, from);
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
            // Low heap first (from the module's exported _heap_ptr), then the
            // string pool -- the same order as the firmware.
            calloc: (n, sz) => {
                const total = Number(n) * Number(sz);
                if (total <= 0) return 0;
                const lh = lowHeap();
                let p = lh.lo ? talloc(lh, total) : 0;
                if (!p) p = talloc(state.pool, total);
                if (!p) state.problems.push('calloc failed: out of heap -- an array leak?');
                return p;
            },
            realloc: (p, sz) => {
                p >>>= 0; sz = Number(sz);
                const tbl = (p < STR_POOL_START) ? lowHeap() : state.pool;
                const rec = tbl.a.find(r => r.offset === p && r.in_use);
                let np = lowHeap().lo ? talloc(lowHeap(), sz) : 0;
                if (!np) np = talloc(state.pool, sz);
                if (!np) { state.problems.push('realloc failed: out of heap'); return 0; }
                if (rec) {
                    const mem = memU8();
                    mem.copyWithin(np, p, p + Math.min(rec.size, sz));
                    tfree(tbl, p, 'realloc');
                }
                return np;
            },
            free: (p) => {
                p >>>= 0;
                if (p === 0) return;
                if (p < STR_POOL_START) tfree(lowHeap(), p, 'free'); else strFree(p);
            },

            // ---- Timing / control ----
            delay_ms: noop, millis: () => 0, millis64: () => 0n,
            get_epoch_ms: () => 0n,
            should_stop: () => state.params[0] === 1 ? 1 : 0,
            wait_pps: () => -1,                      // -1 = no GPS on this host
            // condition 0 >, 1 <, 2 =, 3 <>. Nothing else runs here, so a param
            // that does not match now never will: 0 after the timeout, and an
            // untimed wait would hang a device -- report that.
            wait_param: (id, cond, value, timeout) => {
                const p = (id >= 0 && id < 16) ? state.params[id] : 0;
                if ([p > value, p < value, p === value, p !== value][cond]) return 1;
                if (timeout <= 0) state.problems.push('WAITFOR on a param with no timeout would never return');
                return 0;
            },

            // ---- Stubs for unused-in-tests imports ----
            led_set_pixel: noop, led_fill: noop, led_show: noop,
            led_count: ret0, led_set_gamma: noop, led_set_buffer: noop,
            led_shift: noop, led_rotate: noop, led_reverse: noop,
            led_set_pixel_hsv: noop, led_fill_hsv: noop,
            hsv_to_rgb: ret0, rgb_to_hsv: ret0, led_gamma8: n => n & 0xff,

            // 16 script params, as on the hosts: an out-of-range id reads 0
            // and is not written.
            get_param: (id) => (id >= 0 && id < 16) ? state.params[id] : 0,
            set_param: (id, v) => { if (id >= 0 && id < 16) state.params[id] = v | 0; },
            get_lat: () => 0, get_lon: () => 0, get_alt: () => 0,
            get_speed: () => 0, get_dir: () => 0, gps_valid: ret0,
            has_origin: ret0, origin_dist: () => 0, origin_bearing: () => 0,
            gps_present: ret0, imu_present: () => 1,   // 1 so tests can see IMUPRESENT() become -1
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
            lut_set: noop, lut_save: ret0,
            lut_check: () => -1,                     // LUTSIZE(n): -1 = no such LUT

            pin_set: noop, pin_clear: noop,
            // deterministic levels: odd-numbered pins read high, even read low
            pin_read: p => p & 1, analog_read: ret0,

            // ---- Files: a small in-memory filesystem (per test) ----
            // Line-oriented, like the BASIC statements that use it: OPEN ...
            // FOR INPUT/OUTPUT/APPEND, PRINT #, INPUT #, EOF(), KILL, NAME,
            // MKDIR, RMDIR. file_eof is "no more data to read".
            file_open: (path_ptr, mode) => {
                const path = readStr(path_ptr);
                if (mode === 0) { if (!state.fs.has(path)) return -1; }
                else if (mode === 1) state.fs.set(path, '');
                else if (mode === 2) { if (!state.fs.has(path)) state.fs.set(path, ''); }
                else return -1;
                const h = state.fh.findIndex(x => x === null);
                if (h < 0) return -1;
                state.fh[h] = { path, mode, pos: 0 };
                return h;
            },
            file_close: (h) => { if (state.fh[h]) state.fh[h] = null; },
            file_writeln: (h, str_ptr) => {
                const f = state.fh[h];
                if (!f || f.mode === 0) return -1;
                state.fs.set(f.path, state.fs.get(f.path) + readStr(str_ptr) + '\n');
                return 0;
            },
            file_readln_str: (h) => {
                const f = state.fh[h];
                if (!f || f.mode !== 0) return 0;
                const data = state.fs.get(f.path) || '';
                if (f.pos >= data.length) return 0;
                let end = data.indexOf('\n', f.pos);
                if (end < 0) end = data.length;
                const line = data.substring(f.pos, end);
                f.pos = Math.min(end + 1, data.length);
                const p = strAlloc(line.length + 1);
                if (p) writeStr(p, line);
                return p;
            },
            file_eof: (h) => {
                const f = state.fh[h];
                if (!f || f.mode !== 0) return 1;
                return f.pos >= (state.fs.get(f.path) || '').length ? 1 : 0;
            },
            file_delete: (path_ptr) => state.fs.delete(readStr(path_ptr)) ? 0 : -1,
            file_rename: (a, b) => {
                const from = readStr(a), to = readStr(b);
                if (!state.fs.has(from)) return -1;
                state.fs.set(to, state.fs.get(from)); state.fs.delete(from);
                return 0;
            },
            file_mkdir: (path_ptr) => { const d = readStr(path_ptr); if (state.dirs.has(d)) return -1; state.dirs.add(d); return 0; },
            file_rmdir: (path_ptr) => state.dirs.delete(readStr(path_ptr)) ? 0 : -1,
            file_exists: (path_ptr) => state.fs.has(readStr(path_ptr)) ? 1 : 0,
            file_read: ret0, file_write: ret0, file_size: ret0, file_seek: ret0,
            file_tell: ret0, file_print: ret0, file_input: ret0,
            file_input_line: ret0, basic_file_input_num: ret0,

            // Deterministic stand-in for the hardware RNG: [min, max), like the
            // firmware; min when the range is empty.
            random_int: (min, max) => {
                if (min >= max) return min;
                state.rng = (Math.imul(state.rng, 1103515245) + 12345) >>> 0;
                return min + ((state.rng >>> 8) % (max - min));
            },
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
    const state = { output: '', instance: null, pool: null, low: null, problems: [],
                    fs: new Map(), dirs: new Set(), fh: [null, null, null, null, null, null, null, null], rng: 12345,
                    params: new Array(16).fill(0) };
    try {
        const module = new WebAssembly.Module(bytes);
        const inst = new WebAssembly.Instance(module, makeImports(state));
        state.instance = inst;
        if (inst.exports.setup) inst.exports.setup();   // bas2wasm exports setup()
    } catch (e) {
        // A trap is observable behaviour (bounds checks, READ past the end of
        // DATA, out of memory): record it as a final output line so a test can
        // expect it -- "' TRAP: unreachable". Anything else is a real failure.
        if (e instanceof WebAssembly.RuntimeError)
            state.output += (state.output && !state.output.endsWith('\n') ? '\n' : '') + 'TRAP: ' + e.message + '\n';
        else
            return { error: 'run error: ' + e.message };
    }
    if (state.problems.length)
        return { error: 'host: ' + [...new Set(state.problems)].join('; ') };
    return { ok: true, output: state.output.trim() };
}

// --host <binary>: run each test on a REAL host instead of this file's JavaScript
// imports. <binary> is tools/fwhost/fwhost-dram or fwhost-psram: the firmware's
// own wasm3 fork, run sequence and string / FORMAT / print / file / math imports
// compiled for this machine (with sanitizers), so the code that ships is what
// produces the output. Called as `<binary> <fs-root> /prog.wasm`; each test gets
// an empty <fs-root>.
const hostArg = process.argv.indexOf('--host');
const hostBin = hostArg >= 0 ? path.resolve(process.argv[hostArg + 1] || '') : null;
if (hostArg >= 0 && !fs.existsSync(hostBin)) {
    console.error(`host binary not found: ${process.argv[hostArg + 1]} -- run 'make -C ../fwhost' first`);
    process.exit(1);
}

const hostKind = !hostBin ? 'node' : /wasm-run/.test(path.basename(hostBin)) ? 'sim' : 'fwhost';

// wasm3's trap text -> the text V8 uses (which the EXPECTED blocks record).
const TRAP_TEXT = {
    '[trap] unreachable executed': 'unreachable',
    '[trap] integer overflow': 'divide result unrepresentable',
    '[trap] integer divide by zero': 'divide by zero',
    '[trap] out of bounds memory access': 'memory access out of bounds',
};

function runFileOnHost(fullpath, name) {
    const fsRoot = path.join(tmpDir, 'fs_' + name);
    fs.mkdirSync(fsRoot);
    try {
        execFileSync(bas2wasm, [fullpath, '-o', path.join(fsRoot, 'prog.wasm')], { stdio: 'pipe' });
    } catch (e) {
        return { error: 'compile error' };
    }
    const r = require('child_process').spawnSync(hostBin, [fsRoot, '/prog.wasm'],
        { encoding: 'latin1', timeout: 20000, maxBuffer: 16 << 20 });
    if (r.error) return { error: 'host: ' + (r.error.code === 'ETIMEDOUT' ? 'timed out (20 s)' : r.error.message) };
    if (r.status !== 0 || r.stderr) {
        // a sanitizer report, an abort from the PSRAM bounds check, or a crash
        const why = (r.stderr || '').split('\n').filter(l => l.trim()).slice(0, 4).join(' | ');
        return { error: `host exited ${r.status === null ? 'on ' + r.signal : r.status}: ${why}` };
    }
    let out = r.stdout;
    // The firmware brackets a run with its own lines; anything else is the program's.
    const head = out.match(/^wasm: running [^\n]*\n/);
    if (!head) return { error: 'host: no "wasm: running" line: ' + JSON.stringify(out.slice(0, 120)) };
    out = out.slice(head[0].length);
    if (!out.endsWith('wasm: DONE\n')) return { error: 'host: no "wasm: DONE" line' };
    out = out.slice(0, -'wasm: DONE\n'.length);
    // "wasm: setup() error: [trap] ... (BASIC line N)" -> "TRAP: ..." on a line of its own
    out = out.replace(/(^|\n|[^\n])wasm: \S+ error: (.*?)(?: \(BASIC line \d+\))?\n$/,
        (m, pre, msg) => (pre === '' || pre === '\n' ? pre : pre + '\n') + 'TRAP: ' + (TRAP_TEXT[msg] || msg) + '\n');
    return { ok: true, output: out.trim() };
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

// --update a,b,c: rewrite the EXPECTED block of the named tests with what this
// run printed (works with --host too). For deliberate output changes only --
// read the diff before committing it.
const updArg = process.argv.indexOf('--update');
const updNames = updArg >= 0 ? new Set((process.argv[updArg + 1] || '').split(',').filter(Boolean)) : null;
function rewriteExpected(fullpath, src, output) {
    const block = "' EXPECTED:\n" + output.split('\n').map(l => l === '' ? "'" : "' " + l).join('\n') + '\n';
    fs.writeFileSync(fullpath, src.replace(/^'\s*EXPECTED:\s*\n((?:'(?: [^\n]*)?\n)+)/m, () => block));
}

let pass = 0, fail = 0, skip = 0;

console.log(hostBin ? `=== bas2wasm runtime tests on ${path.basename(hostBin)} ===\n`
                    : '=== bas2wasm runtime test suite ===\n');

const files = fs.readdirSync(testDir).filter(f => f.endsWith('.bas')).sort();
for (const file of files) {
    if (file.startsWith('reject_')) { skip++; continue; }
    const fullpath = path.join(testDir, file);
    const src = fs.readFileSync(fullpath, 'utf8');
    const expected = parseExpected(src);
    if (expected === null) { skip++; continue; }
    // "' HOSTS: <kinds>" limits a test to some hosts: node (this file's
    // JavaScript imports), fwhost (the firmware's code, tools/fwhost), sim (the
    // simulator's, conez-wasm-run); "real" means fwhost and sim. Used where the
    // answer depends on the host: a limit only real hosts have (the wasm3
    // stack), or the simulator's mock hardware (it has a GPS fix, a real clock
    // and real delays, where node and fwhost return fixed values).
    const hm = src.match(/^'\s*HOSTS:\s*([^(\n]+)/m);     // a "(why)" note may follow
    if (hm) {
        const kinds = new Set(hm[1].trim().split(/[\s,]+/).flatMap(k => k === 'real' ? ['fwhost', 'sim'] : [k]));
        if (!kinds.has(hostKind)) { skip++; continue; }
    }

    const name = path.basename(file, '.bas');
    const r = hostBin ? runFileOnHost(fullpath, name) : runFile(fullpath, name);
    if (r.error) {
        console.log(`  ${RED}FAIL${NC}  ${name}  (${r.error})`);
        fail++;
        continue;
    }
    if (r.output === expected) {
        console.log(`  ${GREEN}PASS${NC}  ${name}`);
        pass++;
    } else if (updNames && updNames.has(name)) {
        rewriteExpected(fullpath, src, r.output);
        console.log(`  ${GREEN}upd${NC}   ${name}`);
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
