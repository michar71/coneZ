#!/usr/bin/env python3
"""
Create (or refresh) LoRa distribution manifest(s).

  • walks   firmware/   for  *.bin  files
  • walks   rsync/       for regular files (skips names that start with '.' or '_')
  • keeps   rsync/_serial.txt  with two counters:
        next_file_id=<int>
        next_manifest_serial=<int>
  • re-uses file-IDs from the previous manifest so they stay stable
  • only writes a new _manifest_<N>.txt when the contents actually change
"""

from __future__ import annotations

import hashlib
import re
import sys
from datetime import datetime
from pathlib import Path
from typing import Dict, List, Tuple

ROOT         = Path(__file__).resolve().parent
FIRMWARE_DIR = ROOT / "firmware"
DIST_DIR     = ROOT / "rsync"
SERIAL_FILE  = DIST_DIR / "_serial.txt"
MANIFEST_RE  = re.compile(r"_manifest_(\d+)\.txt$")

###############################################################################
# helpers
###############################################################################


def md5_8(path: Path) -> str:
    """return first 8 hex digits of MD5(path)"""
    h = hashlib.md5()
    with path.open("rb") as fp:
        for chunk in iter(lambda: fp.read(8192), b""):
            h.update(chunk)
    return h.hexdigest()[:8]


def load_serials() -> Tuple[int, int]:
    """Return (next_file_id, next_manifest_serial).  Initialise if missing."""
    if not SERIAL_FILE.exists():
        return 1, 1
    data = SERIAL_FILE.read_text().splitlines()
    vals = {}
    for line in data:
        if "=" in line:
            k, v = line.split("=", 1)
            vals[k.strip()] = int(v.strip())
    return vals.get("next_file_id", 1), vals.get("next_manifest_serial", 1)


def save_serials(next_file_id: int, next_manifest_serial: int) -> None:
    SERIAL_FILE.write_text(
        f"next_file_id={next_file_id}\nnext_manifest_serial={next_manifest_serial}\n"
    )


def latest_manifest() -> Tuple[Path | None, int]:
    """Return (Path, serial) of the newest manifest, or (None, 0)."""
    best, best_n = None, 0
    for p in DIST_DIR.iterdir():
        m = MANIFEST_RE.fullmatch(p.name)
        if m:
            n = int(m.group(1))
            if n > best_n:
                best, best_n = p, n
    return best, best_n


def parse_existing_ids(manifest_path: Path | None) -> Dict[str, Tuple[int, str]]:
    """
    Return mapping  rel_path → (file_id, md5_8)
    """
    if manifest_path is None:
        return {}

    out: Dict[str, Tuple[int, str]] = {}
    with manifest_path.open() as fp:
        section = None
        for line in fp:
            line = line.rstrip("\n")
            if line == "[firmware]":
                section = "fw"
                continue
            elif line == "[files]":
                section = "files"
                continue
            if not line or line.startswith("#"):
                continue

            parts = line.split("\t")
            if section == "fw" and len(parts) >= 5:
                fid = int(parts[0])
                product, ver, md5 = parts[1], parts[2], parts[4]
                rel = f"firmware/{product}/{ver}.bin"
                out[rel] = (fid, md5)
            elif section == "files" and len(parts) >= 4:
                fid = int(parts[0])
                fname, md5 = parts[1], parts[3]
                rel = f"rsync/{fname}"
                out[rel] = (fid, md5)
    return out


###############################################################################
# collection
###############################################################################


def gather_firmware() -> List[Tuple[str, str, int, str, Path]]:
    """
    Return list of tuples:
        (product, version, size, md5_8, path)
    """
    entries = []
    for product_dir in FIRMWARE_DIR.iterdir():
        if not product_dir.is_dir():
            continue
        for binfile in product_dir.glob("*.bin"):
            version = binfile.stem  # e.g. 0.01.0027
            size = binfile.stat().st_size
            md5 = md5_8(binfile)
            entries.append((product_dir.name, version, size, md5, binfile))
    entries.sort()  # deterministic
    return entries


def gather_rsync_files() -> List[Tuple[str, int, str, Path]]:
    """
    Return list of tuples:
        (filename, size, md5_8, path)
    """
    entries = []
    for p in DIST_DIR.iterdir():
        if p.is_file() and not p.name.startswith(".") and not p.name.startswith("_"):
            size = p.stat().st_size
            md5 = md5_8(p)
            entries.append((p.name, size, md5, p))
    entries.sort()
    return entries


###############################################################################
# main build
###############################################################################


def build_manifest() -> None:
    # load counters & previous manifest info
    next_file_id, next_manifest_serial = load_serials()
    old_manifest_path, old_serial = latest_manifest()
    id_lookup = parse_existing_ids(old_manifest_path)

    # collect new directory state
    fw_entries = gather_firmware()
    file_entries = gather_rsync_files()

    # Build manifest text, assigning IDs (re-use known IDs, else allocate new)
    lines: List[str] = []
    lines.append("[firmware]")
    
    for prod, ver, size, md5, p in fw_entries:
        rel = f"firmware/{prod}/{ver}.bin"
        old = id_lookup.get(rel)          # (fid, old_md5) or None

        if old and old[1] == md5:         # same content ➜ reuse
            file_id = old[0]
        else:                             # changed or brand-new ➜ new ID
            file_id = next_file_id
            next_file_id += 1

        lines.append(f"{file_id}\t{prod}\t{ver}\t{size}\t{md5}")

    lines.append("")  # blank line
    lines.append("[files]")
    for fname, size, md5, p in file_entries:
        rel = f"rsync/{fname}"
        old = id_lookup.get(rel)

        if old and old[1] == md5:
            file_id = old[0]
        else:
            file_id = next_file_id
            next_file_id += 1

        lines.append(f"{file_id}\t{fname}\t{size}\t{md5}")

    manifest_text = "\n".join(lines) + "\n"

    # decide whether we need a new manifest
    if old_manifest_path and old_manifest_path.read_text() == manifest_text:
        print("No changes detected – manifest not updated.")
        return

    # write new manifest
    serial = next_manifest_serial
    man_path = DIST_DIR / f"_manifest_{serial}.txt"
    man_path.write_text(manifest_text)
    timestamp = datetime.now().isoformat(sep=" ", timespec="seconds")
    print(f"[{timestamp}] Wrote {man_path.relative_to(ROOT)}")

    # update serial counters
    next_manifest_serial += 1
    save_serials(next_file_id, next_manifest_serial)


###############################################################################
# entry
###############################################################################

if __name__ == "__main__":
    try:
        build_manifest()
    except Exception as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        sys.exit(1)

