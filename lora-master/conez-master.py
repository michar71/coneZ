#!/usr/bin/env python3
import os
import time, binascii
import struct
import math
from datetime import datetime
from LoRaRF import SX126x


# Misc
BEACON_PERIOD = 10              # seconds

DOWNSTREAM_FREQUENCY = 431250000		# 431.250MHz
DOWNSTREAM_BANDWIDTH = 500000			# 500kHz
DOWNSTREAM_SF = 7				# SF7...SF12
DOWNSTREAM_CR = 5				# 4/5 coding rate
DOWNSTREAM_PREAMBLE = 8				# 8 preamble symbols
DOWNSTREAM_TXPOWER = 5				# Transmit power
DOWNSTREAM_DEADTIME = 0.1			# Gap between transmissions
LORA_SYNC_WORD = 0xDEAD

# Firmware
FIRMWARE_FILE = "firmware/ConeZ-v0/0.02.0376.bin"
firmware_len = os.path.getsize( FIRMWARE_FILE )
firmware_offset = 0

# File distribution
FILE_CHUNK_SIZE = 128
MANIFEST_FILE = "rsync/_manifest_1.txt"
manifest_serial = 1
manifest_len = os.path.getsize( MANIFEST_FILE )
manifest_offset = 0

DIST_FILE = "rsync/test.bas"
rsync_file_len = os.path.getsize( DIST_FILE )
rsync_file_offset = 0



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


def make_beacon(
    network: int = 0,
    freq: int = DOWNSTREAM_FREQUENCY,
    bw: int = DOWNSTREAM_BANDWIDTH,
    sf: int = DOWNSTREAM_SF,
    cr: int = DOWNSTREAM_CR
) -> bytes:

    # Translate RF parameters
    rf_params = ( _bw_code(bw)		# Bandwidth
        | (_sf_code(sf) << 3)		# Spreading factor
        | ( _cr_code( cr ) << 6 ) )	# Coding rate

    # Get current UNIX timestamp + millis
    now = time.time()
    seconds = int( now )
    millis = int( (now - seconds) * 1000)

    payload = struct.pack(
        '>BBIHIHH',
        0x01,                    # packet type
        network & 0xFF,          # network number
        seconds & 0xFFFFFFFF,    # timestamp seconds
        millis & 0xFFFF,         # timestamp ms
        freq   & 0xFFFFFFFF,     # downstream frequency
        rf_params,               # RF parameters word
        0                        # reserved (0x0000)
    )
    
    return CONEZ_HEADER + payload    


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


# --- Radio bring-up ----------------------------------------------------------
busId, csId  = 0, 0
resetPin, busyPin, irqPin, txenPin, rxenPin = 18, 20, 16, -1, -1
LoRa = SX126x()
print("Start LoRa radio")
if not LoRa.begin(busId, csId, resetPin, busyPin, irqPin, txenPin, rxenPin):
    raise RuntimeError("Can't start LoRa radio")

LoRa.setDio2RfSwitch()

print( f"  Set frequency: {DOWNSTREAM_FREQUENCY/1000000} MHz" )
LoRa.setFrequency( DOWNSTREAM_FREQUENCY )

LoRa.setRxGain( LoRa.RX_GAIN_BOOSTED )

print( f"  Set modulation parameters:\n    Spreading factor = {DOWNSTREAM_SF}\n    Bandwidth = {DOWNSTREAM_BANDWIDTH/1000} kHz\n    Coding rate = 4/{DOWNSTREAM_CR}" )
LoRa.setLoRaModulation( DOWNSTREAM_SF, DOWNSTREAM_BANDWIDTH, DOWNSTREAM_CR, True )

print( f"  Set packet parameters:\n    Preamble = {DOWNSTREAM_PREAMBLE}  CRC: On" )
LoRa.setLoRaPacket( LoRa.HEADER_EXPLICIT, DOWNSTREAM_PREAMBLE, 200, True, False )

print( f"  Set LoRa sync word: 0x{LORA_SYNC_WORD:04X}" )
LoRa.setSyncWord( LORA_SYNC_WORD )

print( f"  Set TX power: +{DOWNSTREAM_TXPOWER} dBm" )
LoRa.setTxParams( DOWNSTREAM_TXPOWER, LoRa.PA_RAMP_200U )    # 22 dBm, 200 µs ramp
LoRa.setTxPower( DOWNSTREAM_TXPOWER, LoRa.TX_POWER_SX1262 )


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

def next_rsync_chunk(size: int = FILE_CHUNK_SIZE) -> bytes:
    global rsync_file_offset

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
    fh = getattr(next_rsync_chunk, "_fh", None)
    if fh is None:
        fh = open(DIST_FILE, "rb")
        next_rsync_chunk._fh = fh

    chunk = fh.read(size)

    rsync_file_offset = rsync_file_offset + size

    # Reached EOF → close & reset for future calls
    if not chunk:
        fh.close()
        delattr(next_rsync_chunk, "_fh")
        rsync_file_offset = 0

    return chunk

# -------------------------------------------------------

def transmit_file():
    # Grab the next manifest chunk
    page_num = int( rsync_file_offset / 8192 )
    page_blocks = 8192 / FILE_CHUNK_SIZE
    block_num = int( ( rsync_file_offset / FILE_CHUNK_SIZE ) % page_blocks ) 

    data = next_rsync_chunk()
    
    zzz_total_blocks = math.ceil( rsync_file_len / FILE_CHUNK_SIZE )
    zzz_this_block = int( rsync_file_offset / FILE_CHUNK_SIZE )

    packet = make_file_block(
        FILE_TYPE_LITTLEFS,		# File type
        manifest_serial,		# Manifest serial #
        2,				# Manifest file #
        rsync_file_len,			# Total file size
        page_num,			# Page #
        page_blocks,			# Total blocks in this page
        block_num,			# This block #
        COMP_TYPE_NONE,			# Compression type
        8192,				# Page size
        data
        )
        
    print( f"  File: {DIST_FILE}  Size: {rsync_file_len}  Chunk: {zzz_this_block} / {zzz_total_blocks}  Page: {page_num}  Block: {block_num}" )
        
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
            
            time.sleep( DOWNSTREAM_DEADTIME )
            
            print( "TX Manifest" )
            transmit_manifest()

            time.sleep( DOWNSTREAM_DEADTIME )
            
            print( "TX File" )
            transmit_file()
            
            time.sleep( DOWNSTREAM_DEADTIME )

            print( "TX File" )
            transmit_file()
            
            time.sleep( DOWNSTREAM_DEADTIME )
            
            print( "TX Firmware" )
            transmit_firmware()

            time.sleep( DOWNSTREAM_DEADTIME )

            print( "TX Firmware" )
            transmit_firmware()
            
            time.sleep( DOWNSTREAM_DEADTIME )

#            print( "TX Firmware" )
#            transmit_firmware()
#            
#            time.sleep( 0.5 )
#
#            print( "TX Firmware" )
#            transmit_firmware()
#            
#            time.sleep( 0.5 )

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

