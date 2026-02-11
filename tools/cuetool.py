#!/usr/bin/env python3
"""
cuetool.py — Compile YAML show descriptions to binary .cue files for ConeZ,
             or dump existing .cue files to human-readable text.

Usage:
    python3 cuetool.py build <input.yaml> [-o output.cue]
    python3 cuetool.py dump  <input.cue>
"""

import argparse
import os
import re
import struct
import sys

try:
    import yaml
except ImportError:
    print("Error: PyYAML is required.  Install with:  pip install pyyaml",
          file=sys.stderr)
    sys.exit(1)

# ── Binary format constants (must match cue.h) ──────────────────────────────

CUE_MAGIC       = 0x43554530   # "CUE0"
CUE_VERSION     = 0
HEADER_SIZE     = 64
ENTRY_SIZE      = 64

# struct cue_header  (64 bytes)
#   uint32  magic
#   uint16  version
#   uint16  num_cues
#   uint16  record_size
#   54 bytes reserved
HEADER_FMT = '<IHHHx54s'       # 4+2+2+2 + 54 = 64   (the 'x' is padding byte — but
                                # we'll pack field-by-field instead for clarity)

# struct cue_entry  (64 bytes)
#   uint8   cue_type          1
#   uint8   channel           1
#   uint16  group             2
#   uint32  start_ms          4
#   uint32  duration_ms       4
#   float   spatial_delay     4
#   float   spatial_param1    4
#   float   spatial_param2    4
#   uint16  spatial_angle     2
#   uint8   spatial_mode      1
#   uint8   flags             1
#   char[20] effect_file     20
#   uint8[16] params         16
ENTRY_FMT = '<BBHIIfffHBB20s16s'   # 1+1+2+4+4+4+4+4+2+1+1+20+16 = 64

assert struct.calcsize(ENTRY_FMT) == ENTRY_SIZE

# ── Lookup tables ────────────────────────────────────────────────────────────

CUE_TYPES = {
    'stop':     0,
    'effect':   1,
    'fill':     2,
    'blackout': 3,
    'global':   4,
}
CUE_TYPE_NAMES = {v: k for k, v in CUE_TYPES.items()}

SPATIAL_MODES = {
    'none':              0,
    'radial_config':     1,
    'radial_absolute':   2,
    'radial_relative':   3,
    'dir_config':        4,
    'dir_absolute':      5,
    'dir_relative':      6,
}
SPATIAL_MODE_NAMES = {v: k for k, v in SPATIAL_MODES.items()}

FLAG_BITS = {
    'fire_forget': 0x01,
    'loop':        0x02,
    'blend_add':   0x04,
}

# ── Time parsing ─────────────────────────────────────────────────────────────

def parse_time(value):
    """Parse a human-readable time string into integer milliseconds.

    Accepts:
        "500ms"         →  500
        "1.5s"          → 1500
        "1s500ms"       → 1500
        "1m30s"         → 90000
        "2m"            → 120000
        1500   (int)    → 1500
        1.5    (float)  → 1  (truncated — bare numbers are ms)
    """
    if isinstance(value, (int, float)):
        return int(value)

    s = str(value).strip().lower()

    # Try composite pattern: optional Nm, optional Ns or N.Ns, optional Nms
    pat = re.compile(
        r'^'
        r'(?:(\d+)\s*m(?!s))?'          # minutes
        r'\s*(?:(\d+(?:\.\d+)?)\s*s)?'   # seconds (may be fractional)
        r'\s*(?:(\d+)\s*ms)?'            # milliseconds
        r'$'
    )
    m = pat.match(s)
    if m and any(m.groups()):
        minutes = int(m.group(1)) if m.group(1) else 0
        seconds = float(m.group(2)) if m.group(2) else 0.0
        millis  = int(m.group(3)) if m.group(3) else 0
        return int(minutes * 60000 + seconds * 1000 + millis)

    # Bare integer string
    try:
        return int(s)
    except ValueError:
        pass

    raise ValueError(f"Cannot parse time: {value!r}")


# ── Group parsing ────────────────────────────────────────────────────────────

def parse_group(value):
    """Parse group targeting string into uint16.

    Accepts:
        None / "all"            → 0x0000
        "cone:5"                → 0x1000 | 5
        "group:2"               → 0x2000 | 2
        "mask:0x00F"            → 0x3000 | 0x00F
        "not_cone:5"            → 0x4000 | 5
        "not_group:2"           → 0x5000 | 2
        "not_mask:0x00F"        → 0x6000 | 0x00F
        integer                 → raw value
    """
    if value is None:
        return 0x0000

    if isinstance(value, int):
        return value & 0xFFFF

    s = str(value).strip().lower()
    if s == 'all':
        return 0x0000

    prefixes = {
        'cone:':      0x1000,
        'group:':     0x2000,
        'mask:':      0x3000,
        'not_cone:':  0x4000,
        'not_group:': 0x5000,
        'not_mask:':  0x6000,
    }

    for prefix, mode_bits in prefixes.items():
        if s.startswith(prefix):
            num_str = s[len(prefix):]
            num = int(num_str, 0)  # handles 0x hex
            if num > 0x0FFF:
                raise ValueError(f"Group value too large (max 0xFFF): {num:#x}")
            return mode_bits | num

    raise ValueError(f"Cannot parse group: {value!r}")


# ── Spatial parsing ──────────────────────────────────────────────────────────

def parse_spatial(spatial_dict):
    """Parse spatial dict into (mode, delay, param1, param2, angle)."""
    if spatial_dict is None:
        return (0, 0.0, 0.0, 0.0, 0)

    mode_name = str(spatial_dict.get('mode', 'none')).lower()
    if mode_name not in SPATIAL_MODES:
        raise ValueError(f"Unknown spatial mode: {mode_name!r}  "
                         f"(valid: {', '.join(SPATIAL_MODES)})")

    return (
        SPATIAL_MODES[mode_name],
        float(spatial_dict.get('delay', 0.0)),
        float(spatial_dict.get('param1', 0.0)),
        float(spatial_dict.get('param2', 0.0)),
        int(spatial_dict.get('angle', 0)),
    )


# ── Flags parsing ────────────────────────────────────────────────────────────

def parse_flags(flags_list):
    """Parse list of flag name strings into uint8 bitmask."""
    if not flags_list:
        return 0
    result = 0
    for name in flags_list:
        key = str(name).strip().lower()
        if key not in FLAG_BITS:
            raise ValueError(f"Unknown flag: {key!r}  (valid: {', '.join(FLAG_BITS)})")
        result |= FLAG_BITS[key]
    return result


# ── Build command ────────────────────────────────────────────────────────────

def build(args):
    with open(args.input, 'r') as f:
        doc = yaml.safe_load(f)

    if not isinstance(doc, dict) or 'cues' not in doc:
        print("Error: YAML must have a top-level 'cues' list.", file=sys.stderr)
        sys.exit(1)

    raw_cues = doc['cues']
    if not isinstance(raw_cues, list) or len(raw_cues) == 0:
        print("Error: 'cues' must be a non-empty list.", file=sys.stderr)
        sys.exit(1)

    entries = []
    for i, cue in enumerate(raw_cues):
        try:
            entry = compile_cue(cue)
            entries.append(entry)
        except (ValueError, KeyError) as e:
            print(f"Error in cue #{i}: {e}", file=sys.stderr)
            sys.exit(1)

    # Sort by start_ms
    entries.sort(key=lambda e: e['start_ms'])

    # Determine output path
    if args.output:
        out_path = args.output
    else:
        stem = os.path.splitext(args.input)[0]
        out_path = stem + '.cue'

    # Write binary file
    with open(out_path, 'wb') as f:
        # Header
        reserved = b'\x00' * 54
        hdr = struct.pack('<IHHH', CUE_MAGIC, CUE_VERSION, len(entries), ENTRY_SIZE)
        hdr += reserved
        assert len(hdr) == HEADER_SIZE
        f.write(hdr)

        # Entries
        for entry in entries:
            effect_bytes = entry['effect_file'].encode('utf-8')[:20].ljust(20, b'\x00')
            params_bytes = bytes(entry['params'][:16]).ljust(16, b'\x00')

            data = struct.pack(
                ENTRY_FMT,
                entry['cue_type'],
                entry['channel'],
                entry['group'],
                entry['start_ms'],
                entry['duration_ms'],
                entry['spatial_delay'],
                entry['spatial_param1'],
                entry['spatial_param2'],
                entry['spatial_angle'],
                entry['spatial_mode'],
                entry['flags'],
                effect_bytes,
                params_bytes,
            )
            assert len(data) == ENTRY_SIZE
            f.write(data)

    print(f"Wrote {len(entries)} cues to {out_path} "
          f"({os.path.getsize(out_path)} bytes)")


def compile_cue(cue):
    """Compile a single YAML cue dict into a field dictionary."""
    type_name = str(cue.get('type', '')).lower()
    if type_name not in CUE_TYPES:
        raise ValueError(f"Unknown cue type: {type_name!r}  "
                         f"(valid: {', '.join(CUE_TYPES)})")

    cue_type = CUE_TYPES[type_name]
    channel  = int(cue.get('channel', 0))
    group    = parse_group(cue.get('group'))
    start_ms = parse_time(cue.get('time', 0))
    duration_ms = parse_time(cue.get('duration', 0))

    spatial_mode, spatial_delay, spatial_param1, spatial_param2, spatial_angle = \
        parse_spatial(cue.get('spatial'))

    flags = parse_flags(cue.get('flags'))

    effect_file = str(cue.get('file', ''))

    # Build params array
    params = [0] * 16
    if cue_type == CUE_TYPES['fill']:
        color = cue.get('color', [0, 0, 0])
        if isinstance(color, list) and len(color) >= 3:
            params[0] = int(color[0]) & 0xFF
            params[1] = int(color[1]) & 0xFF
            params[2] = int(color[2]) & 0xFF
    elif 'params' in cue:
        raw_params = cue['params']
        if isinstance(raw_params, list):
            for j, v in enumerate(raw_params[:16]):
                params[j] = int(v) & 0xFF

    return {
        'cue_type':       cue_type,
        'channel':        channel,
        'group':          group,
        'start_ms':       start_ms,
        'duration_ms':    duration_ms,
        'spatial_delay':  spatial_delay,
        'spatial_param1': spatial_param1,
        'spatial_param2': spatial_param2,
        'spatial_angle':  spatial_angle,
        'spatial_mode':   spatial_mode,
        'flags':          flags,
        'effect_file':    effect_file,
        'params':         params,
    }


# ── Dump command ─────────────────────────────────────────────────────────────

def dump(args):
    with open(args.input, 'rb') as f:
        data = f.read()

    if len(data) < HEADER_SIZE:
        print("Error: file too small for header.", file=sys.stderr)
        sys.exit(1)

    # Parse header
    magic, version, num_cues, record_size = struct.unpack_from('<IHHH', data, 0)

    if magic != CUE_MAGIC:
        print(f"Error: bad magic 0x{magic:08X} (expected 0x{CUE_MAGIC:08X})",
              file=sys.stderr)
        sys.exit(1)

    print(f"Magic:       0x{magic:08X} (CUE0)")
    print(f"Version:     {version}")
    print(f"Num cues:    {num_cues}")
    print(f"Record size: {record_size}")
    print()

    expected_size = HEADER_SIZE + num_cues * record_size
    if len(data) < expected_size:
        print(f"Warning: file is {len(data)} bytes, expected {expected_size}",
              file=sys.stderr)

    # Column headers
    print(f"{'#':>3}  {'Time':>10}  {'Type':<8}  {'Ch':>2}  {'Group':<14}  "
          f"{'Spatial':<16}  {'Flags':<20}  {'Effect':<20}  {'Params'}")
    print("-" * 120)

    for i in range(num_cues):
        offset = HEADER_SIZE + i * record_size
        if offset + ENTRY_SIZE > len(data):
            print(f"  (truncated at entry {i})")
            break

        fields = struct.unpack_from(ENTRY_FMT, data, offset)
        (cue_type, channel, group, start_ms, duration_ms,
         spatial_delay, spatial_param1, spatial_param2, spatial_angle,
         spatial_mode, flags, effect_file_raw, params_raw) = fields

        # Format time
        time_str = format_time(start_ms)
        if duration_ms:
            time_str += f"+{format_time(duration_ms)}"

        # Format type
        type_str = CUE_TYPE_NAMES.get(cue_type, f"?{cue_type}")

        # Format group
        group_str = format_group(group)

        # Format spatial
        spatial_str = format_spatial(spatial_mode, spatial_delay, spatial_param1,
                                     spatial_param2, spatial_angle)

        # Format flags
        flag_names = []
        for name, bit in FLAG_BITS.items():
            if flags & bit:
                flag_names.append(name)
        flags_str = ','.join(flag_names) if flag_names else '-'

        # Format effect file
        effect_str = effect_file_raw.rstrip(b'\x00').decode('utf-8', errors='replace')
        if not effect_str:
            effect_str = '-'

        # Format params
        params_list = list(params_raw)
        # Trim trailing zeros for display
        while params_list and params_list[-1] == 0:
            params_list.pop()
        params_str = ','.join(str(b) for b in params_list) if params_list else '-'

        print(f"{i:>3}  {time_str:>10}  {type_str:<8}  {channel:>2}  {group_str:<14}  "
              f"{spatial_str:<16}  {flags_str:<20}  {effect_str:<20}  {params_str}")


def format_time(ms):
    """Format milliseconds into human-readable string."""
    if ms == 0:
        return "0s"
    parts = []
    if ms >= 60000:
        parts.append(f"{ms // 60000}m")
        ms %= 60000
    if ms >= 1000:
        s = ms // 1000
        rem = ms % 1000
        if rem:
            parts.append(f"{s}s{rem}ms")
        else:
            parts.append(f"{s}s")
        ms = 0
    elif ms > 0:
        parts.append(f"{ms}ms")
    return ''.join(parts)


def format_group(group):
    """Format uint16 group into human-readable string."""
    mode = group >> 12
    value = group & 0x0FFF
    names = {
        0: 'all',
        1: f'cone:{value}',
        2: f'group:{value}',
        3: f'mask:0x{value:03X}',
        4: f'not_cone:{value}',
        5: f'not_group:{value}',
        6: f'not_mask:0x{value:03X}',
    }
    return names.get(mode, f'0x{group:04X}')


def format_spatial(mode, delay, param1, param2, angle):
    """Format spatial fields into compact string."""
    if mode == 0:
        return '-'
    name = SPATIAL_MODE_NAMES.get(mode, f'?{mode}')
    parts = [name]
    if delay != 0:
        parts.append(f"d={delay:.1f}")
    if param1 != 0 or param2 != 0:
        parts.append(f"p=({param1:.2f},{param2:.2f})")
    if angle != 0:
        parts.append(f"a={angle}")
    return ' '.join(parts)


# ── Main ─────────────────────────────────────────────────────────────────────

def main():
    parser = argparse.ArgumentParser(
        description='ConeZ cue file compiler and inspector')
    sub = parser.add_subparsers(dest='command')

    build_p = sub.add_parser('build', help='Compile YAML to binary .cue')
    build_p.add_argument('input', help='Input YAML file')
    build_p.add_argument('-o', '--output', help='Output .cue file '
                         '(default: input stem + .cue)')

    dump_p = sub.add_parser('dump', help='Dump binary .cue to text')
    dump_p.add_argument('input', help='Input .cue file')

    args = parser.parse_args()
    if args.command == 'build':
        build(args)
    elif args.command == 'dump':
        dump(args)
    else:
        parser.print_help()
        sys.exit(1)


if __name__ == '__main__':
    main()
