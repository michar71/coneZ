#!/usr/bin/env python3
import os
import re
import sys
import glob
import fcntl
import time, binascii
import struct
import math
from datetime import datetime
from LoRaRF import SX126x
import lora_proto as proto


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
DOWNSTREAM_DEADTIME = 0.1			# Gap between transmissions
LORA_SYNC_WORD = 0xDEAD
CALLSIGN = "N1AF"				# FCC station ID (in the v1 beacon)

# Firmware
FIRMWARE_FILE = "firmware/ConeZ-v0/0.02.0376.bin"
firmware_len = os.path.getsize( FIRMWARE_FILE )
firmware_offset = 0

# File distribution. The distributable payload lives in dist/ (only what gets
# sent to the herd); the generated manifest + serial counter live in dist-state/.
FILE_CHUNK_SIZE = 128
DIST_DIR = "dist"
MANIFEST_DIR = "dist-state"
MANIFEST_RE = re.compile(r"manifest_(\d+)\.txt$")


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

DIST_FILE = "dist/test.bas"
dist_file_len = os.path.getsize( DIST_FILE )
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

print( f"  Set frequency: {DOWNSTREAM_FREQUENCY/1000000} MHz" )
LoRa.setFrequency( DOWNSTREAM_FREQUENCY )

LoRa.setRxGain( LoRa.RX_GAIN_BOOSTED )

print( f"  Set modulation parameters:\n    Spreading factor = {DOWNSTREAM_SF}\n    Bandwidth = {DOWNSTREAM_BANDWIDTH/1000} kHz\n    Coding rate = 4/{DOWNSTREAM_CR}\n    LDRO = {'on' if DOWNSTREAM_LDRO else 'off'}" )
LoRa.setLoRaModulation( DOWNSTREAM_SF, DOWNSTREAM_BANDWIDTH, DOWNSTREAM_CR, DOWNSTREAM_LDRO )

print( f"  Set packet parameters:\n    Preamble = {DOWNSTREAM_PREAMBLE}  CRC: On" )
LoRa.setLoRaPacket( LoRa.HEADER_EXPLICIT, DOWNSTREAM_PREAMBLE, 200, True, False )

print( f"  Set LoRa sync word: 0x{LORA_SYNC_WORD:04X}" )
LoRa.setSyncWord( LORA_SYNC_WORD )

# Set TX power via the per-power optimized PA table (set_tx_power above). LoRaRF's
# own setTxPower() dead-ends for SX1262 <+14 dBm; this gives a clean signal at any
# power -9..+22 dBm.
_actual_power = set_tx_power( DOWNSTREAM_TXPOWER )
print( f"  Set TX power: {_actual_power:+d} dBm (matched PA config)" )


print("\n--- LoRa RX Continuous ---\n")
LoRa.request(LoRa.RX_CONTINUOUS)

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


# --- Main loop ---------------------------------------------------------------
last_tx = time.monotonic() - BEACON_PERIOD         # force immediate beacon
try:
    while True:
        # Periodic beacon
        if time.monotonic() - last_tx >= BEACON_PERIOD:
            print( "-------" )
            print()
            
            print( "TX Beacon" )
            beacon = make_beacon()
            transmit( beacon )
            last_tx = time.monotonic()
            # Phase 1: beacon-only. Legacy file/firmware/manifest carousel
            # removed; the v1 dist carousel arrives in Phase 3.
        # Check for incoming packets
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

