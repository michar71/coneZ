# Export File Specification (`.spc`)

This document defines the Mayhem timeline export format written by the **Export** button.

## File Extension

- Extension: `.spc`
- Encoding: UTF-8 JSON

## Top-Level JSON Structure

The file is a JSON object with one required property:

- `PIECE`: array of entries

Example:

```json
{
  "PIECE": [
    { "STARTTIME": "0", "DURATION": "123456", "TYPE": "header", "MEDIA": "/abs/path/media.mp3" }
  ]
}
```

## Entry Ordering

Entries inside `PIECE` are ordered as follows:

1. First entry is always the `header` entry.
2. All effect entries follow, sorted by:
   1. `STARTTIME` ascending
   2. `CHANNEL` ascending (0, 1, 2, ...)
   3. Stable insertion order for ties

## Common Fields (Effect Entries)

All non-header entries include:

- `STARTTIME` (string)
- `DURATION` (string)
- `TYPE` (lowercase string)
- `CHANNEL` (integer)

### Time Encoding

- Time values are exported as **milliseconds**.
- Values are encoded as **strings** (to avoid JSON integer-size issues).
- `STARTTIME` is computed as:
  - `media_start_epoch_ms + effect_start_ms`
- Current behavior uses `media_start_epoch_ms = 0`.

## Header Entry

The first entry in `PIECE` has:

- `STARTTIME`: `"0"`
- `DURATION`: media length in milliseconds (string)
- `TYPE`: `"header"`
- `MEDIA`: absolute media path (string)

## Supported `TYPE` Values and Fields

`TYPE` is always lowercase.

### `header`

- `STARTTIME`, `DURATION`, `TYPE`, `MEDIA`

### `color`

- Common fields +
- `STARTCOLOR` (uint32)
- `ENDCOLOR` (uint32)
- `OFFSET` (int)
- `WINDOW` (int)

### `script`

- Common fields +
- `SCRIPTLINK` (string)

### `paramset`

- Common fields +
- `PARAMNAME` (string, up to 16 chars in editor)
- `VALUE` (float)

### `paramchange`

- Common fields +
- `PARAMNAME` (string, up to 16 chars in editor)
- `STARTVALUE` (float)
- `ENDVALUE` (float)
- `OFFSET` (int)
- `WINDOW` (int)

### `fx` (legacy/internal support)

- Common fields +
- `FXID` (uint)
- `PARAMS` (float array)

### `media` (legacy/internal support)

- Common fields +
- `MEDIALINK` (string)

## Color Encoding

Color values are exported as 32-bit integers in:

- `AABBGGRR` layout
- Alpha in highest 8 bits
- Red in lowest 8 bits
- Current export uses alpha = `255` (`0xFF`)

## Notes

- Field names are uppercase in exported JSON.
- `TYPE` values are lowercase.
- Header is always present, even if no effects exist.
