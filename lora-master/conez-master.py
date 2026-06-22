#!/usr/bin/env python3
import os
import re
import sys
import glob
import fcntl
import time, binascii
import struct
import math
import socket
import threading
import configparser
from datetime import datetime
from LoRaRF import SX126x
import lora_proto as proto
import reed_solomon as rs

# Bench FEC test: drop the last K DATA chunks of every block (parity still sent),
# forcing the cone to Reed-Solomon-recover. 0 = off. `DIST_TEST_DROP_DATA=1 ...`
DIST_TEST_DROP_DATA = int(os.environ.get("DIST_TEST_DROP_DATA", "0"))


# --- Single-instance guard ---------------------------------------------------
# Only one master may drive the single LoRa radio; concurrent instances corrupt
# the SPI/radio state. Hold an exclusive flock for the process lifetime. The OS
# releases it automatically on exit/crash/kill, so there is no stale lock to
# clean up.
_LOCK_PATH = "/tmp/conez-master.lock"
_lock_fh = open(_LOCK_PATH, "w")
try:
    fcntl.flock(_lock_fh, fcntl.LOCK_EX | fcntl.LOCK_NB)
except OSError:
    sys.stderr.write("conez-master: another instance is already running; exiting.\n")
    sys.exit(1)
_lock_fh.write(f"{os.getpid()}\n")
_lock_fh.flush()


# Misc
BEACON_PERIOD = 10              # seconds

DOWNSTREAM_FREQUENCY = 431250000		# 431.250MHz
DOWNSTREAM_BANDWIDTH = 500000			# 500kHz
DOWNSTREAM_SF = 7				# SF7...SF12
DOWNSTREAM_CR = 5				# 4/5 coding rate
DOWNSTREAM_PREAMBLE = 8				# 8 preamble symbols
# Low Data Rate Optimize: required ONLY when symbol time > 16 ms (high SF / low
# BW). Must be computed, NOT hard-coded: the on-cone RadioLib auto-computes LDRO
# from symbol time, so if we disagree the payload demaps wrong (CRC mismatch).
# Was hard-coded True (correct for SF12/BW125, wrong for SF7/BW500). SF7/BW500
# symbol time = 128/500000 = 0.26 ms -> LDRO off.
DOWNSTREAM_LDRO = ((1 << DOWNSTREAM_SF) / DOWNSTREAM_BANDWIDTH) > 0.016
DOWNSTREAM_TXPOWER = 0				# dBm (signed, -9..+22). Low for close-range
						# bench use. Set via setPaConfig+setTxParams
						# below, NOT LoRaRF setTxPower() (which dead-
						# ends for SX1262 powers <+14 -> no RF out)
DOWNSTREAM_DEADTIME = float(os.environ.get("DEADTIME", "0.1"))	# Gap between transmissions (env-tunable for bench)
LORA_SYNC_WORD = 0xDEAD
CALLSIGN = "N1AF"				# FCC station ID (in the v1 beacon)

# Firmware (legacy Phase-0 globals, used only by the dormant transmit_firmware()
# helper; the v1 carousel serves firmware via build_dist_chunks()). Tolerant of a
# missing file so removing an old image can't crash startup.
FIRMWARE_FILE = "firmware/ConeZ-v0/0.02.0376.bin"
firmware_len = os.path.getsize(FIRMWARE_FILE) if os.path.exists(FIRMWARE_FILE) else 0
firmware_offset = 0

# File distribution. The distributable payload lives in dist/ (only what gets
# sent to the herd); the generated manifest + serial counter live in dist-state/.
FILE_CHUNK_SIZE = 128
DIST_DIR = "dist"
MANIFEST_DIR = "dist-state"
MANIFEST_RE = re.compile(r"manifest_(\d+)\.txt$")


# --- Config file (overrides the hard-coded defaults above) -------------------
# Optional INI at CONEZ_MASTER_CONFIG (default ./conez-master.ini). Precedence:
# built-in defaults < config file < env vars (DEADTIME / DIST_TEST_DROP_DATA),
# so existing bench invocations keep working. Re-readable at runtime via the
# control socket's `reload` command.
CONFIG_PATH    = os.environ.get("CONEZ_MASTER_CONFIG", "conez-master.ini")
CONTROL_SOCKET = os.environ.get("CONEZ_MASTER_SOCKET", "/tmp/conez-master.sock")

# Snapshot the hard-coded defaults ONCE (before load_config can mutate them) so a
# reload is deterministic: every setting is recomputed as (default <- file <- env),
# never carried over from a previous reload.
_CFG_DEFAULTS = dict(
    BEACON_PERIOD=BEACON_PERIOD, DOWNSTREAM_DEADTIME=DOWNSTREAM_DEADTIME, CALLSIGN=CALLSIGN,
    DOWNSTREAM_FREQUENCY=DOWNSTREAM_FREQUENCY, DOWNSTREAM_BANDWIDTH=DOWNSTREAM_BANDWIDTH,
    DOWNSTREAM_SF=DOWNSTREAM_SF, DOWNSTREAM_CR=DOWNSTREAM_CR, DOWNSTREAM_PREAMBLE=DOWNSTREAM_PREAMBLE,
    DOWNSTREAM_TXPOWER=DOWNSTREAM_TXPOWER, LORA_SYNC_WORD=LORA_SYNC_WORD,
    DIST_DIR=DIST_DIR, MANIFEST_DIR=MANIFEST_DIR, DIST_TEST_DROP_DATA=DIST_TEST_DROP_DATA,
    CONTROL_SOCKET=CONTROL_SOCKET,
)

def load_config(path=CONFIG_PATH):
    """Recompute every setting from (hard-coded default <- INI file <- env var).
    Deterministic: a key absent from the file falls back to the DEFAULT, never to a
    value left over from an earlier reload. Returns a one-line summary."""
    global BEACON_PERIOD, DOWNSTREAM_FREQUENCY, DOWNSTREAM_BANDWIDTH, DOWNSTREAM_SF
    global DOWNSTREAM_CR, DOWNSTREAM_PREAMBLE, DOWNSTREAM_LDRO, DOWNSTREAM_TXPOWER
    global DOWNSTREAM_DEADTIME, LORA_SYNC_WORD, CALLSIGN
    global DIST_DIR, MANIFEST_DIR, DIST_TEST_DROP_DATA, CONTROL_SOCKET
    D = _CFG_DEFAULTS
    loaded = os.path.exists(path)
    cp = configparser.ConfigParser()
    if loaded:
        cp.read(path)
    def g(sec, key, default, cast):
        if loaded and cp.has_option(sec, key):
            try: return cast(cp.get(sec, key).strip())
            except Exception as e: print(f"config: bad [{sec}] {key}: {e}")
        return default
    BEACON_PERIOD        = g("master", "beacon_period", D["BEACON_PERIOD"], int)
    DOWNSTREAM_DEADTIME  = g("master", "deadtime",      D["DOWNSTREAM_DEADTIME"], float)
    CALLSIGN             = g("master", "callsign",      D["CALLSIGN"], str)
    DOWNSTREAM_FREQUENCY = g("lora", "frequency", D["DOWNSTREAM_FREQUENCY"], int)
    DOWNSTREAM_BANDWIDTH = g("lora", "bandwidth", D["DOWNSTREAM_BANDWIDTH"], int)
    DOWNSTREAM_SF        = g("lora", "sf",        D["DOWNSTREAM_SF"], int)
    DOWNSTREAM_CR        = g("lora", "cr",        D["DOWNSTREAM_CR"], int)
    DOWNSTREAM_PREAMBLE  = g("lora", "preamble",  D["DOWNSTREAM_PREAMBLE"], int)
    DOWNSTREAM_TXPOWER   = g("lora", "tx_power",  D["DOWNSTREAM_TXPOWER"], int)
    LORA_SYNC_WORD       = g("lora", "sync_word", D["LORA_SYNC_WORD"], lambda s: int(s, 0))
    DIST_DIR             = g("dist", "dir",            D["DIST_DIR"], str)
    MANIFEST_DIR         = g("dist", "manifest_dir",   D["MANIFEST_DIR"], str)
    DIST_TEST_DROP_DATA  = g("dist", "test_drop_data", D["DIST_TEST_DROP_DATA"], int)
    CONTROL_SOCKET       = g("control", "socket",      D["CONTROL_SOCKET"], str)
    # Env vars win over the file (legacy bench knobs).
    if "DEADTIME" in os.environ:
        DOWNSTREAM_DEADTIME = float(os.environ["DEADTIME"])
    if "DIST_TEST_DROP_DATA" in os.environ:
        DIST_TEST_DROP_DATA = int(os.environ["DIST_TEST_DROP_DATA"])
    # LDRO is DERIVED from SF/BW (must match the cone's auto-LDRO), never set directly.
    DOWNSTREAM_LDRO = ((1 << DOWNSTREAM_SF) / DOWNSTREAM_BANDWIDTH) > 0.016
    src = path if loaded else f"defaults (no {path})"
    return (f"config: {src} -> {DOWNSTREAM_FREQUENCY/1e6:.3f} MHz SF{DOWNSTREAM_SF} "
            f"BW{DOWNSTREAM_BANDWIDTH//1000}k CR4/{DOWNSTREAM_CR} {DOWNSTREAM_TXPOWER:+d} dBm "
            f"sync 0x{LORA_SYNC_WORD:04X} beacon {BEACON_PERIOD}s deadtime {DOWNSTREAM_DEADTIME}s "
            f"callsign {CALLSIGN}")

print(load_config())


def latest_manifest(dir_path=MANIFEST_DIR):
    """Return (path, serial) of the highest-numbered manifest_<N>.txt, or (None, 0)."""
    best, best_n = None, 0
    for p in glob.glob(os.path.join(dir_path, "manifest_*.txt")):
        m = MANIFEST_RE.search(os.path.basename(p))
        if m:
            n = int(m.group(1))
            if n > best_n:
                best, best_n = p, n
    return best, best_n


MANIFEST_FILE, manifest_serial = latest_manifest()
if MANIFEST_FILE is None:
    raise RuntimeError(f"No manifest_*.txt found in {MANIFEST_DIR}/ (run build_manifest.py)")
print(f"Using manifest: {MANIFEST_FILE} (serial {manifest_serial})")
manifest_len = os.path.getsize( MANIFEST_FILE )
manifest_offset = 0

DIST_FILE = "dist/test.bas"   # legacy Phase-0 global (dormant transmit_file() helper)
dist_file_len = os.path.getsize(DIST_FILE) if os.path.exists(DIST_FILE) else 0
dist_file_offset = 0



# ConeZ header
CONEZ_HEADER = b'\xDE\xAD\x00N1AF  \x00ConeZ\x00'

# Beacon payload
# Offset   Length   Description
# 0        1        Packet type - 0x01 = ConeZ Beacon
# 1        1        ConeZ network number
# 2        4        UNIX timestamp, unsigned int32
# 6        2        UNIX timestamp milliseconds, unsigned int16
# 8        4        Downstream frequency, unsigned int32
# c        2        Downstream RF specs - bandwidth/SF/CR/flags
#                     Bits 0-2  Bandwidth
#                                 0 = 500kHz
#                                 1 = 250kHz
#                                 2 = 125kHz
#                                 3 = 62.5kHz
#                                 4 =
#                                 5 =
#                                 6 =
#                                 7 =
#                     Bits 3-5  Spreading Factor
#                                 0 = SF7
#                                 1 = SF8
#                                 2 = SF9
#                                 3 = SF10
#                                 4 = SF11
#                                 5 = SF12
#                                 6 =
#                                 7 = 
#                     Bits 6-7  Coding Rate
#                                 0 = 4/5
#                                 1 = 4/6
#                                 2 = 4/7
#                                 3 = 4/8
#                     Bits ...
# e        2        Reserved

# ------------------------------------------

# File distribution metadata
# Offset   Length   Description
# 0        1        Packet type - 0x10 = File metadata

# ------------------------------------------

# File distribution data
# Offset   Length   Description
# 0        1        Packet type - 0x11 = File distribution contents
# 1        1        File type
#                     Bits 0-3 = File type
#                       0 = Manifest file
#                       1 = Firmware
#                       2 = LittleFS file
#                     Bits 4-5 = Compression type
#                       0 = Uncompressed
#                       1 =
#                       2 =
#                       3 =
#                     Bits 6-7 = Compression page size (original file is chunked into this size and then compressed)
#                       0 = 1024 bytes
#                       1 = 8192 bytes
#                       2 =
#                       3 = 
# 2        2        Manifest serial #
# 4        2        Manifest file number that this block of data goes with
# 6        4        File length, unsigned int32
# a        2        Page number of original file
# c        1        Total blocks in this page
# d        1        This block number
# e        2        Reserved
# 10       ...      File block data (up to 128 bytes)

# ------------------------------------------


# ------------------------------------------

def _bw_code(bw_hz: int) -> int:
    """Map bandwidth in Hz → 3-bit code (table in spec)."""
    return {500_000: 0, 250_000: 1, 125_000: 2, 62_500: 3}.get(bw_hz, 0)


def _sf_code(sf: int) -> int:
    """Map spreading factor → 3-bit code."""
    return {7: 0, 8: 1, 9: 2, 10: 3, 11: 4, 12: 5}.get(sf, 0)


def _cr_code(cr: int) -> int:
    """
    Map LoRa ‘CR’ parameter (5 → 4/5, 6 → 4/6, …) or explicit denominator
    to the 2-bit code in the spec.
    """
    lookup = {5: 0, 6: 1, 7: 2, 8: 3,               # LoRa style
              1: 0, 2: 1, 3: 2, 4: 3}               # explicit 4/5 → 1, etc.
    return lookup.get(cr, 0)


def make_beacon(network: int = 0) -> bytes:
    """v1 BEACON: channel/params + master time + manifest serial + callsign."""
    return proto.make_beacon(
        network=network,
        freq=DOWNSTREAM_FREQUENCY, bw=DOWNSTREAM_BANDWIDTH,
        sf=DOWNSTREAM_SF, cr=DOWNSTREAM_CR,
        sync_word=LORA_SYNC_WORD, manifest_serial=manifest_serial,
        callsign=CALLSIGN,
    )


# ------------------------------------------


FILE_TYPE_MANIFEST = 0
FILE_TYPE_FIRMWARE = 1
FILE_TYPE_LITTLEFS = 2

COMP_TYPE_NONE = 0


def make_file_block(
    file_type: int,
    manifest_serial: int,
    manifest_file_no: int,
    file_len: int,
    page_no: int,
    total_blocks: int,
    block_no: int,
    comp_type: int,
    page_size: int,
    data: bytes
) -> bytes:
    """
    Return the complete packet (header + ≤128-byte payload).

    Parameters follow the field list from the spec.  Raises ValueError on
    length/range errors.
    """
    if len(data) > 128:
        raise ValueError("data block must be ≤128 bytes")
    if not (0 <= total_blocks <= 255 and 0 <= block_no <= 255):
        raise ValueError("total_blocks and block_no must fit in one byte")
        
    file_type_byte = ( ( file_type & 0x0F) |
        (( comp_type & 0x03) << 4) |
        (( 2 & 0x03) << 6 ) )		# FIXME - Hard-coded to 8kB page size

    hdr = struct.pack(
        '>BBHHIHBBH',              # 16-byte header
        0x11,                      # packet type
        int( file_type_byte ),
        int( manifest_serial & 0xFFFF ),
        int( manifest_file_no & 0xFFFF ),
        int( file_len & 0xFFFFFFFF ),
        int( page_no & 0xFFFF ),
        int( total_blocks ),
        int( block_no ),
        0                          # reserved
    )
    return CONEZ_HEADER + hdr + data

# ------------------------------------------

def hex_dump(data):
    if isinstance(data, (bytes, bytearray)):
        hex_data   = ' '.join(f"{b:02x}" for b in data)
        ascii_data = ''.join(chr(b) if 32 <= b <= 126 else '.' for b in data)
    else:                       # fallback for an already-decoded str
        hex_data   = ' '.join(f"{ord(c):02x}" for c in data)
        ascii_data = ''.join(c if 32 <= ord(c) <= 126 else '.' for c in data)

    return f"{hex_data}\nASCII: {ascii_data}"


# SX1262 optimized PA settings indexed by output power (-9..+22 dBm); index =
# power+9. Ported from RadioLib 7.6.0 (firmware's copy) SX1262.cpp paOptTable
# (empirically measured: https://github.com/radiolib-org/power-tests). Each entry
# is (paDutyCycle, hpMax, paVal); paVal is the signed value for SetTxParams. Using
# the matched config per power gives a CLEAN signal at low power, instead of the
# spectrally poor result of throttling the +22 dBm PA config to near-zero output.
PA_OPT_TABLE = [
    (2, 2, -5), (2, 1, 0), (1, 1, 3), (1, 2, 0), (1, 1, 6), (1, 2, 3),     # -9..-4
    (2, 2, 2), (4, 1, 6), (1, 1, 11), (2, 1, 11), (1, 1, 14), (2, 1, 14),  # -3..2
    (1, 1, 20), (1, 1, 22), (2, 2, 11), (3, 1, 21), (1, 2, 17), (4, 2, 13),# 3..8
    (1, 2, 20), (1, 2, 22), (2, 2, 21), (3, 2, 21), (1, 4, 19), (1, 4, 20),# 9..14
    (3, 3, 20), (2, 5, 19), (1, 6, 22), (2, 5, 22), (3, 5, 22), (3, 6, 22),# 15..20
    (4, 6, 22), (4, 7, 22),                                                # 21..22
]


def set_tx_power(power_dbm):
    """Set a clean TX power (-9..+22 dBm) using the per-power optimized PA config,
    mirroring RadioLib's SX1262 setOutputPower. Avoids LoRaRF setTxPower()'s dead
    branch for <+14 dBm and the dirty signal from throttling the +22 PA config."""
    power_dbm = max(-9, min(22, int(power_dbm)))
    duty, hpmax, paval = PA_OPT_TABLE[power_dbm + 9]
    LoRa.setPaConfig(duty, hpmax, 0x00, 0x01)   # paDutyCycle, hpMax, deviceSel=SX1262, paLut
    LoRa.setTxParams(paval & 0xFF, LoRa.PA_RAMP_200U)
    return power_dbm


# --- Radio bring-up ----------------------------------------------------------
busId, csId  = 0, 0
# RF switch is handled by DIO2 (setDio2RfSwitch below); TXEN/RXEN GPIOs unused
# (-1), matching the known-working lora-aprs code. (txen=6 was a red herring; the
# real TX failure was the unconfigured PA at low power via LoRaRF setTxPower(),
# now fixed by set_tx_power()'s per-power PA table.)
resetPin, busyPin, irqPin, txenPin, rxenPin = 18, 20, 16, -1, -1
LoRa = SX126x()
print("Start LoRa radio")
if not LoRa.begin(busId, csId, resetPin, busyPin, irqPin, txenPin, rxenPin):
    raise RuntimeError("Can't start LoRa radio")

LoRa.setDio2RfSwitch()
LoRa.setRxGain( LoRa.RX_GAIN_BOOSTED )


def apply_radio_config(reconfigure=False):
    """Apply the (possibly reloaded) DOWNSTREAM_* radio params, then return to RX
    continuous. reconfigure=True first drops to standby (the radio sits in
    RX_CONTINUOUS during normal operation). TX power goes through set_tx_power()'s
    per-power PA table, NOT LoRaRF setTxPower() (which dead-ends <+14 dBm on SX1262).
    Returns the actual TX power set."""
    if reconfigure:
        LoRa.standby()
    LoRa.setFrequency( DOWNSTREAM_FREQUENCY )
    LoRa.setLoRaModulation( DOWNSTREAM_SF, DOWNSTREAM_BANDWIDTH, DOWNSTREAM_CR, DOWNSTREAM_LDRO )
    LoRa.setLoRaPacket( LoRa.HEADER_EXPLICIT, DOWNSTREAM_PREAMBLE, 200, True, False )
    LoRa.setSyncWord( LORA_SYNC_WORD )
    p = set_tx_power( DOWNSTREAM_TXPOWER )
    LoRa.request( LoRa.RX_CONTINUOUS )
    return p

_actual_power = apply_radio_config()
print( f"  Radio: {DOWNSTREAM_FREQUENCY/1e6:.3f} MHz  SF{DOWNSTREAM_SF}  BW{DOWNSTREAM_BANDWIDTH//1000}k  "
       f"CR4/{DOWNSTREAM_CR}  LDRO {'on' if DOWNSTREAM_LDRO else 'off'}  {_actual_power:+d} dBm  "
       f"sync 0x{LORA_SYNC_WORD:04X}" )
print("\n--- LoRa RX Continuous ---\n")

def flush_rx_fifo():
    while LoRa.available():
        LoRa.read()

flush_rx_fifo()


def transmit(payload):
    """Beacon once, then return to continuous RX."""
    # Show the text we’re about to send
    #printable = payload.decode(errors='ignore') if isinstance(payload, (bytes, bytearray)) else payload
    #print(f"TX: {printable}")
    length = len( payload )
    
    print( f"TX Len: {length}  Hex: ", end="" )
    print( hex_dump( payload ) )

    t_txstart = time.time()
    t0 = time.perf_counter()

    LoRa.beginPacket()

    # ---- v1.3-compatible write ----
    if isinstance(payload, (bytes, bytearray)):
        LoRa.write(list(payload), len(payload))          # convert to [int, int, …]
    else:                                               # payload is a str
        LoRa.write([ord(c) for c in payload], len(payload))

    LoRa.endPacket()
    LoRa.wait()                     # block until the radio finishes TX
    #time.sleep( 0.10 )		# RN - Avoid ghost RX?
    flush_rx_fifo()
    LoRa.request(LoRa.RX_CONTINUOUS)
    
    duration = time.perf_counter() - t0
    
    print( f"TX start: {t_txstart:.3f}  Duration: {duration:.3f} sec" )    

    print()


# -------------------------------------------------------

def next_firmware_chunk(size: int = FILE_CHUNK_SIZE) -> bytes:
    global firmware_offset

    """
    Return the next *size* bytes of the firmware file.

    On the first call it opens FIRMWARE_FILE for reading and keeps the handle
    attached to the function object.  When EOF is reached it closes the file
    and removes the handle so a subsequent call starts over from the top.

    Returns
    -------
    bytes  – the next chunk (0-length at end of file)
    """
    # Lazy-open on first use
    fh = getattr(next_firmware_chunk, "_fh", None)
    if fh is None:
        fh = open(FIRMWARE_FILE, "rb")
        next_firmware_chunk._fh = fh

    chunk = fh.read(size)

    firmware_offset = firmware_offset + size

    # Reached EOF → close & reset for future calls
    if not chunk:
        fh.close()
        delattr(next_firmware_chunk, "_fh")
        firmware_offset = 0

    return chunk

# -------------------------------------------------------

def transmit_firmware():
    # Grab the next firmware chunk
    page_num = int( firmware_offset / 8192 )
    page_blocks = 8192 / FILE_CHUNK_SIZE
    block_num = int( ( firmware_offset / FILE_CHUNK_SIZE ) % page_blocks ) 

    data = next_firmware_chunk()
    
    zzz_total_blocks = math.ceil( firmware_len / FILE_CHUNK_SIZE )
    zzz_this_block = int( firmware_offset / FILE_CHUNK_SIZE )

    packet = make_file_block(
        FILE_TYPE_FIRMWARE,		# File type
        1,				# Manifest serial #
        1,				# Manifest file #
        firmware_len,			# Total file size
        page_num,			# Page #
        page_blocks,			# Total blocks in this page
        block_num,			# This block #
        COMP_TYPE_NONE,			# Compression type
        8192,				# Page size
        data
        )
        
    print( f"  Firmware: {FIRMWARE_FILE}  Size: {firmware_len}  Chunk: {zzz_this_block} / {zzz_total_blocks}  Page: {page_num}  Block: {block_num}" )
        
    transmit( packet )

# -------------------------------------------------------

def next_manifest_chunk(size: int = FILE_CHUNK_SIZE) -> bytes:
    global manifest_offset

    """
    Return the next *size* bytes of the firmware file.

    On the first call it opens FIRMWARE_FILE for reading and keeps the handle
    attached to the function object.  When EOF is reached it closes the file
    and removes the handle so a subsequent call starts over from the top.

    Returns
    -------
    bytes  – the next chunk (0-length at end of file)
    """
    # Lazy-open on first use
    fh = getattr(next_manifest_chunk, "_fh", None)
    if fh is None:
        fh = open(MANIFEST_FILE, "rb")
        next_manifest_chunk._fh = fh

    chunk = fh.read(size)

    manifest_offset = manifest_offset + size

    # Reached EOF → close & reset for future calls
    if not chunk:
        fh.close()
        delattr(next_manifest_chunk, "_fh")
        manifest_offset = 0

    return chunk

# -------------------------------------------------------

def transmit_manifest():
    # Grab the next manifest chunk
    page_num = int( manifest_offset / 8192 )
    page_blocks = 8192 / FILE_CHUNK_SIZE
    block_num = int( ( manifest_offset / FILE_CHUNK_SIZE ) % page_blocks ) 

    data = next_manifest_chunk()
    
    zzz_total_blocks = math.ceil( manifest_len / FILE_CHUNK_SIZE )
    zzz_this_block = int( manifest_offset / FILE_CHUNK_SIZE )

    packet = make_file_block(
        FILE_TYPE_FIRMWARE,		# File type
        manifest_serial,		# Manifest serial #
        1,				# Manifest file #
        manifest_len,			# Total file size
        page_num,			# Page #
        page_blocks,			# Total blocks in this page
        block_num,			# This block #
        COMP_TYPE_NONE,			# Compression type
        8192,				# Page size
        data
        )
        
    print( f"  Manifest: {MANIFEST_FILE}  Size: {manifest_len}  Chunk: {zzz_this_block} / {zzz_total_blocks}  Page: {page_num}  Block: {block_num}" )
        
    transmit( packet )

# -------------------------------------------------------

def next_dist_chunk(size: int = FILE_CHUNK_SIZE) -> bytes:
    global dist_file_offset

    """
    Return the next *size* bytes of the firmware file.

    On the first call it opens FIRMWARE_FILE for reading and keeps the handle
    attached to the function object.  When EOF is reached it closes the file
    and removes the handle so a subsequent call starts over from the top.

    Returns
    -------
    bytes  – the next chunk (0-length at end of file)
    """
    # Lazy-open on first use
    fh = getattr(next_dist_chunk, "_fh", None)
    if fh is None:
        fh = open(DIST_FILE, "rb")
        next_dist_chunk._fh = fh

    chunk = fh.read(size)

    dist_file_offset = dist_file_offset + size

    # Reached EOF → close & reset for future calls
    if not chunk:
        fh.close()
        delattr(next_dist_chunk, "_fh")
        dist_file_offset = 0

    return chunk

# -------------------------------------------------------

def transmit_file():
    # Grab the next manifest chunk
    page_num = int( dist_file_offset / 8192 )
    page_blocks = 8192 / FILE_CHUNK_SIZE
    block_num = int( ( dist_file_offset / FILE_CHUNK_SIZE ) % page_blocks ) 

    data = next_dist_chunk()
    
    zzz_total_blocks = math.ceil( dist_file_len / FILE_CHUNK_SIZE )
    zzz_this_block = int( dist_file_offset / FILE_CHUNK_SIZE )

    packet = make_file_block(
        FILE_TYPE_LITTLEFS,		# File type
        manifest_serial,		# Manifest serial #
        2,				# Manifest file #
        dist_file_len,			# Total file size
        page_num,			# Page #
        page_blocks,			# Total blocks in this page
        block_num,			# This block #
        COMP_TYPE_NONE,			# Compression type
        8192,				# Page size
        data
        )
        
    print( f"  File: {DIST_FILE}  Size: {dist_file_len}  Chunk: {zzz_this_block} / {zzz_total_blocks}  Page: {page_num}  Block: {block_num}" )
        
    transmit( packet )


# --- Phase 3 dist carousel ---------------------------------------------------
def parse_manifest_files(path):
    """Return [(file_id, fname, size, md5)] from the manifest [files] section."""
    out = []
    section = None
    with open(path) as fp:
        for line in fp:
            line = line.rstrip("\n")
            if line == "[firmware]": section = "fw"; continue
            if line == "[files]":    section = "files"; continue
            if not line or line.startswith("#"): continue
            parts = line.split("\t")
            if section == "files" and len(parts) >= 4:
                out.append((int(parts[0]), parts[1], int(parts[2]), parts[3]))
    return out


def parse_manifest_firmware(path):
    """Return [(file_id, product, version, size, block_size)] from [firmware]
    (Phase 6 -- firmware is dist-able). Skips legacy lines lacking block geometry."""
    out = []
    section = None
    with open(path) as fp:
        for line in fp:
            line = line.rstrip("\n")
            if line == "[firmware]": section = "fw"; continue
            if line == "[files]":    section = "files"; continue
            if not line or line.startswith("#"): continue
            parts = line.split("\t")
            # id product version size md5 algo block_size total_blocks
            if section == "fw" and len(parts) >= 8:
                out.append((int(parts[0]), parts[1], parts[2], int(parts[3]), int(parts[6])))
    return out


def _file_to_chunks(file_id, file_len, algo, blocks):
    """Slice a file's per-block (compressed) byte strings into chunk descriptors:
    N DATA chunks then R systematic-RS PARITY chunks per block (Phase 5).

    Each chunk carries the full block/file geometry so the cone can reassemble a
    block (RS-recovering any lost DATA chunks from PARITY), inflate it, and place
    it at block_idx*block_size in the output file. The last DATA chunk is sent
    short; the cone zero-pads it for the code (block_comp_len makes it unambiguous).
    """
    csz = proto.DIST_CHUNK_SIZE
    out = []
    total_blocks = len(blocks)
    for bidx, b in enumerate(blocks):
        comp_len = len(b)
        N = max(1, (comp_len + csz - 1) // csz)
        R = rs.parity_count(N, proto.DIST_PARITY_FRAC, proto.DIST_PARITY_MIN)
        padded = b + bytes(N * csz - comp_len)          # zero-pad last chunk for RS
        parity = rs.encode(N, R, csz, padded) if R else b""
        drop = min(DIST_TEST_DROP_DATA, R, N)           # bench: simulate erasures
        meta = dict(fid=file_id, flen=file_len, algo=algo, bidx=bidx,
                    tb=total_blocks, N=N, R=R, comp_len=comp_len)
        for ci in range(N):
            if drop and ci >= N - drop:                 # skip -> cone must RS-recover
                continue
            out.append(dict(meta, parity=False, idx=ci, payload=b[ci * csz:(ci + 1) * csz]))
        for pi in range(R):
            out.append(dict(meta, parity=True, idx=pi, payload=parity[pi * csz:(pi + 1) * csz]))
    return out


def build_dist_chunks(manifest_path=None):
    """All chunks to broadcast: the manifest (file id 0, uncompressed) first, then
    data files (per-block deflate or store, per the manifest). Defaults to the
    current MANIFEST_FILE; reload_manifest_if_changed() passes a fresh path."""
    if manifest_path is None:
        manifest_path = MANIFEST_FILE
    chunks = []
    with open(manifest_path, "rb") as fp:
        man = fp.read()
    chunks += _file_to_chunks(proto.DIST_MANIFEST_ID, len(man), proto.ALGO_NONE,
                              proto.split_blocks(man, proto.DIST_MANIFEST_BLOCK_SIZE))
    for (fid, fname, size, md5) in parse_manifest_files(manifest_path):
        fpath = os.path.join(DIST_DIR, fname)
        try:
            with open(fpath, "rb") as fp:
                data = fp.read()
        except FileNotFoundError:
            print(f"  dist: WARNING missing {fpath} (id {fid})")
            continue
        algo, blocks = proto.encode_file(data)
        chunks += _file_to_chunks(fid, len(data), algo, blocks)
    # Phase 6: firmware images, chunked at the firmware block size (sent last so the
    # small files keep flowing each carousel pass while the big image trickles).
    for (fid, prod, ver, size, bsz) in parse_manifest_firmware(manifest_path):
        fpath = os.path.join("firmware", prod, ver + ".bin")
        try:
            with open(fpath, "rb") as fp:
                data = fp.read()
        except FileNotFoundError:
            print(f"  dist: WARNING missing firmware {fpath} (id {fid})")
            continue
        algo, blocks = proto.encode_file(data, bsz)
        chunks += _file_to_chunks(fid, len(data), algo, blocks)
        print(f"  dist: firmware {prod} {ver} ({len(data)} B) -> {len(blocks)} blocks, "
              f"{'deflate' if algo else 'store'}")
    return chunks


dist_chunks = build_dist_chunks()
# The MANIFEST (file id 0) is the entry point: a cone can't act on any data/firmware
# chunk until it holds the matching manifest. With a large firmware a full carousel
# pass is many minutes, so sending the manifest only once per pass strands a cone
# that locks mid-pass. Broadcast it after EVERY beacon instead (spec 13.7: manifest
# = highest priority); the big payload cycles in between.
manifest_chunks = [c for c in dist_chunks if c["fid"] == proto.DIST_MANIFEST_ID]
payload_chunks  = [c for c in dist_chunks if c["fid"] != proto.DIST_MANIFEST_ID]
payload_idx = 0
_comp = sum(len(c["payload"]) for c in dist_chunks)
_par = sum(1 for c in dist_chunks if c["parity"])
print(f"dist: {len(dist_chunks)} chunks ({_par} parity) ({len(manifest_chunks)} manifest + "
      f"{len(parse_manifest_files(MANIFEST_FILE))} files + {len(parse_manifest_firmware(MANIFEST_FILE))} fw), "
      f"chunk size {proto.DIST_CHUNK_SIZE}, {_comp} on-air payload bytes"
      + (f"  [TEST drop {DIST_TEST_DROP_DATA} data/blk]" if DIST_TEST_DROP_DATA else ""))


def send_chunk(c):
    mk = proto.make_dist_parity if c["parity"] else proto.make_dist_data
    pkt = mk(manifest_serial=manifest_serial, file_id=c["fid"], file_len=c["flen"],
             block_index=c["bidx"], total_blocks=c["tb"], chunk_index=c["idx"],
             data_chunks=c["N"], parity_chunks=c["R"], block_comp_len=c["comp_len"],
             payload=c["payload"])
    transmit(pkt)
    _a = "deflate" if c["algo"] == proto.ALGO_DEFLATE else "store"
    _k = "par" if c["parity"] else "dat"
    _tot = c["R"] if c["parity"] else c["N"]
    print(f"TX dist id={c['fid']} blk {c['bidx']+1}/{c['tb']} "
          f"{_k} {c['idx']+1}/{_tot} ({len(c['payload'])} B, {_a})")


def send_manifest():
    for c in manifest_chunks:
        send_chunk(c)
        time.sleep(DOWNSTREAM_DEADTIME)


def send_next_payload_chunk():
    global payload_idx
    if not payload_chunks:
        return
    send_chunk(payload_chunks[payload_idx])
    payload_idx = (payload_idx + 1) % len(payload_chunks)


def reload_manifest_if_changed():
    """Hot-reload: if build_manifest.py has produced a newer manifest_<N>.txt,
    switch to it live -- re-read the manifest + repackage all chunks -- WITHOUT a
    restart. Build the new chunks first; only swap the live globals if it succeeds,
    so a half-written manifest or a missing payload never takes the master down."""
    global MANIFEST_FILE, manifest_serial, manifest_len
    global dist_chunks, manifest_chunks, payload_chunks, payload_idx
    path, serial = latest_manifest()
    if path is None or serial <= manifest_serial:
        return False
    try:
        new_chunks = build_dist_chunks(path)
    except Exception as e:
        print(f"dist: reload of serial {serial} failed ({e}); staying on serial {manifest_serial}")
        return False
    MANIFEST_FILE  = path
    manifest_serial = serial
    manifest_len   = os.path.getsize(path)
    dist_chunks    = new_chunks
    manifest_chunks = [c for c in dist_chunks if c["fid"] == proto.DIST_MANIFEST_ID]
    payload_chunks  = [c for c in dist_chunks if c["fid"] != proto.DIST_MANIFEST_ID]
    payload_idx = 0
    nf = len(parse_manifest_files(path)); nfw = len(parse_manifest_firmware(path))
    print(f"dist: HOT-RELOADED manifest serial {serial} ({os.path.basename(path)}): "
          f"{len(manifest_chunks)} manifest + {len(payload_chunks)} payload chunks "
          f"({nf} files + {nfw} fw)")
    return True


# --- Control socket ----------------------------------------------------------
# A Unix-domain control socket (+ the conez-masterctl CLI) drives the running
# master without a restart. Commands set flags the MAIN LOOP acts on, so config/
# manifest/radio reloads happen in the main thread and never race a transmit.
g_tx_enabled    = True               # LoRa transmission on/off (enable/disable)
g_stop          = False              # graceful shutdown (stop)
g_reload_req    = threading.Event()  # control thread -> main loop: do a full reload
g_reload_done   = threading.Event()  # main loop -> control thread: reload finished
g_reload_result = ""                 # main loop writes the reload summary

def do_full_reload():
    """Re-read the config file, re-apply the radio params, and reload the latest
    manifest. MUST run in the main-loop thread (no race with transmit)."""
    global MANIFEST_FILE, manifest_serial, manifest_len, dist_chunks
    global manifest_chunks, payload_chunks, payload_idx, _actual_power
    cfg_summary = load_config()
    _actual_power = apply_radio_config(reconfigure=True)
    path, serial = latest_manifest(MANIFEST_DIR)
    if path is not None:
        MANIFEST_FILE   = path
        manifest_serial = serial
        manifest_len    = os.path.getsize(path)
        dist_chunks     = build_dist_chunks(path)
        manifest_chunks = [c for c in dist_chunks if c["fid"] == proto.DIST_MANIFEST_ID]
        payload_chunks  = [c for c in dist_chunks if c["fid"] != proto.DIST_MANIFEST_ID]
        payload_idx     = 0
    return (f"{cfg_summary}; manifest serial {manifest_serial} "
            f"({os.path.basename(MANIFEST_FILE) if MANIFEST_FILE else 'none'}), "
            f"{len(payload_chunks)} payload chunks")

def handle_control_command(line):
    """Runs in the control thread. Returns a one-line response."""
    global g_tx_enabled, g_stop
    cmd = (line.split() or [""])[0].lower()
    if cmd in ("enable", "on"):
        g_tx_enabled = True;  return "OK: LoRa TX enabled"
    if cmd in ("disable", "off"):
        g_tx_enabled = False; return "OK: LoRa TX disabled"
    if cmd == "stop":
        g_stop = True;        return "OK: stopping"
    if cmd == "reload":
        # do_full_reload() re-runs build_dist_chunks() (deflate+RS of the whole
        # firmware, ~20 s for a 1.2 MB image), so wait generously.
        g_reload_done.clear()
        g_reload_req.set()                       # main loop performs it
        if g_reload_done.wait(timeout=90):
            return "OK: reloaded — " + g_reload_result
        return "ERROR: reload still running (taking >90 s); check the master log"
    if cmd == "status":
        return (f"OK: tx={'enabled' if g_tx_enabled else 'disabled'}  "
                f"manifest=serial {manifest_serial} "
                f"({os.path.basename(MANIFEST_FILE) if MANIFEST_FILE else 'none'})  "
                f"payload_chunks={len(payload_chunks)}  "
                f"radio={DOWNSTREAM_FREQUENCY/1e6:.3f}MHz/SF{DOWNSTREAM_SF}/"
                f"BW{DOWNSTREAM_BANDWIDTH//1000}k/{DOWNSTREAM_TXPOWER:+d}dBm  "
                f"beacon={BEACON_PERIOD}s deadtime={DOWNSTREAM_DEADTIME}s callsign={CALLSIGN}")
    if cmd in ("help", "?", ""):
        return "OK: commands: status | reload | enable|on | disable|off | stop | help"
    return f"ERROR: unknown command '{cmd}' (try: status, reload, enable, disable, stop, help)"

def control_server():
    """Background thread: serve the Unix-domain control socket."""
    try:
        if os.path.exists(CONTROL_SOCKET):
            os.unlink(CONTROL_SOCKET)
        srv = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        srv.bind(CONTROL_SOCKET)
        srv.listen(4)
        srv.settimeout(1.0)
    except Exception as e:
        print(f"control socket: FAILED on {CONTROL_SOCKET}: {e}")
        return
    print(f"control socket: {CONTROL_SOCKET}  (use conez-masterctl)")
    while not g_stop:
        try:
            conn, _ = srv.accept()
        except socket.timeout:
            continue
        except OSError:
            break
        try:
            conn.settimeout(20.0)
            line = conn.recv(512).decode(errors="replace").strip()
            conn.sendall((handle_control_command(line) + "\n").encode())
        except Exception as e:
            try: conn.sendall(f"ERROR: {e}\n".encode())
            except Exception: pass
        finally:
            conn.close()
    try:
        srv.close(); os.unlink(CONTROL_SOCKET)
    except OSError:
        pass

threading.Thread(target=control_server, name="control", daemon=True).start()


# --- Main loop ---------------------------------------------------------------
last_tx = time.monotonic() - BEACON_PERIOD         # force immediate beacon
try:
    while not g_stop:
        # Control: explicit full reload (config + radio + manifest), done HERE so
        # it never races a transmit or the chunk lists.
        if g_reload_req.is_set():
            try:
                g_reload_result = do_full_reload()
            except Exception as e:
                g_reload_result = f"reload failed: {e}"
            g_reload_req.clear()
            g_reload_done.set()
            print(f"control: reloaded — {g_reload_result}")

        # Periodic beacon (only while TX is enabled)
        if g_tx_enabled and time.monotonic() - last_tx >= BEACON_PERIOD:
            reload_manifest_if_changed()   # pick up a freshly-built manifest, no restart
            print( "-------" )
            print()

            print( "TX Beacon" )
            beacon = make_beacon()         # carries the (possibly just-reloaded) manifest_serial
            transmit( beacon )
            last_tx = time.monotonic()
            # Manifest right after each beacon so a freshly-locked cone can act
            # immediately (highest priority); the payload cycles between beacons.
            send_manifest()
        elif g_tx_enabled:
            # dist carousel: next data/firmware chunk (manifest sent post-beacon)
            send_next_payload_chunk()
            time.sleep( DOWNSTREAM_DEADTIME )
        else:
            time.sleep( 0.05 )             # TX disabled: idle, but keep serving RX below

        # Check for incoming packets (always, even when TX is disabled)
        if LoRa.available():
            t_rx = datetime.now()
            msg   = bytearray()
            while LoRa.available():
                msg.append(LoRa.read())
            #hexline = ' '.join(f"{b:02x}" for b in msg)
            #print(hexline)
            print("RX Len = {:>3}  RSSI = {:6.2f} dBm  SNR = {:6.2f} dB  {}"
                  .format(len(msg), LoRa.packetRssi(), LoRa.snr(),
                          t_rx.strftime("%Y-%m-%d %H:%M:%S")))
            print( "RX Hex: ", end="" )
            print(hex_dump(msg))
            print()

            status = LoRa.status()
            if status == LoRa.STATUS_CRC_ERR:    print("CRC error")
            if status == LoRa.STATUS_HEADER_ERR: print("Packet header error")
            print()

        time.sleep(0.01)   # tiny duty-cycle so we don't spin flat-out
except KeyboardInterrupt:
    pass
finally:
    LoRa.end()
    try: os.unlink(CONTROL_SOCKET)
    except OSError: pass
    print("conez-master: stopped")

