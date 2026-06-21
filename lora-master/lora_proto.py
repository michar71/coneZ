#!/usr/bin/env python3
"""
ConeZ LoRa Protocol v1 -- wire format (Python side).

SHARED DEFINITION: keep byte-for-byte in sync with the firmware's
firmware/src/lora/lora_proto.h. See documentation/lora-protocol.txt for the
design. All integers are big-endian.

Common header (4 bytes):  type, network, src, dst
Addresses: 0 = master, 1..254 = cone, 255 = broadcast.
"""
import struct
import time
import zlib

PROTO_VERSION = 1

# Packet types
PKT_BEACON      = 0x01
PKT_REG_REQ     = 0x10
PKT_REG_RESP    = 0x11
PKT_POLL_REQ    = 0x20
PKT_POLL_RESP   = 0x21
PKT_CUE         = 0x30
PKT_DIST_DATA   = 0x40
PKT_DIST_PARITY = 0x41

# Addresses
ADDR_MASTER    = 0
ADDR_BROADCAST = 255

# Channel modes
MODE_LORA = 0
MODE_FSK  = 1

# Common header: type, network, src, dst
HEADER_FMT = ">BBBB"
HEADER_LEN = struct.calcsize(HEADER_FMT)   # 4

# BEACON body (LoRa channel descriptor), after the header:
#   B  proto version
#   I  epoch seconds        (master clock at end-of-TX; see spec section 8)
#   H  epoch millis
#   B  mode (0=LoRa, 1=FSK)
#   I  frequency  (Hz)
#   I  bandwidth  (Hz)       [LoRa]
#   B  spreading factor      [LoRa]
#   B  coding rate (5..8 = 4/5..4/8)  [LoRa]
#   H  sync word
#   H  manifest serial
#   8s callsign (ASCII, space-padded)
BEACON_FMT = ">BIHBIIBBHH8s"
BEACON_LEN = struct.calcsize(BEACON_FMT)   # 30
CALLSIGN_LEN = 8


def make_header(ptype: int, network: int, src: int, dst: int) -> bytes:
    return struct.pack(HEADER_FMT, ptype & 0xFF, network & 0xFF, src & 0xFF, dst & 0xFF)


def make_beacon(*, network: int, freq: int, bw: int, sf: int, cr: int,
                sync_word: int, manifest_serial: int, callsign: str,
                mode: int = MODE_LORA,
                epoch_s: int | None = None, epoch_ms: int | None = None) -> bytes:
    """Build a full BEACON packet (header + body). If epoch not given, use now."""
    if epoch_s is None:
        now = time.time()
        epoch_s = int(now)
        epoch_ms = int((now - epoch_s) * 1000)
    cs = callsign.encode("ascii", "replace")[:CALLSIGN_LEN].ljust(CALLSIGN_LEN, b" ")
    body = struct.pack(BEACON_FMT,
                       PROTO_VERSION,
                       epoch_s & 0xFFFFFFFF, epoch_ms & 0xFFFF,
                       mode & 0xFF,
                       freq & 0xFFFFFFFF, bw & 0xFFFFFFFF,
                       sf & 0xFF, cr & 0xFF,
                       sync_word & 0xFFFF, manifest_serial & 0xFFFF, cs)
    return make_header(PKT_BEACON, network, ADDR_MASTER, ADDR_BROADCAST) + body


# DIST body (after the header). Shared by DIST_DATA (0x40) and DIST_PARITY (0x41);
# the packet TYPE says whether chunk_idx is a data index (0..N-1) or parity index
# (0..R-1). Phase 4 per-BLOCK transfer + Phase 5 systematic-RS-across-chunks.
#   H  manifest serial
#   H  file id            (0 = the manifest itself)
#   I  file length        (total uncompressed bytes)
#   H  block index
#   H  total blocks       (in this file)
#   H  chunk index        (data idx for DATA, parity idx for PARITY)
#   H  data chunks N      (in this block)
#   H  parity chunks R    (in this block)
#   I  block comp len     (true compressed length of this block)
#   <payload>             (CHUNK_SIZE bytes; last DATA chunk may be short, padded
#                          with zeros for the RS code; PARITY chunks are full)
DIST_HDR_FMT    = ">HHIHHHHHI"
DIST_HDR_LEN    = struct.calcsize(DIST_HDR_FMT)   # 22
DIST_CHUNK_SIZE = 200                             # payload bytes per chunk (RS symbol size)
DIST_MANIFEST_ID = 0                              # reserved file id for the manifest

# Block geometry / compression. A file is split into BLOCK_SIZE-byte (uncompressed)
# blocks; each block is independently deflated, then sliced into <=CHUNK_SIZE
# on-air chunks. Compress THEN (later) FEC, per block (spec section 13.3).
DIST_BLOCK_SIZE          = 32768   # data-file block size (LOWER to bench multi-block)
DIST_MANIFEST_BLOCK_SIZE = 32768   # MUST equal firmware LP_DIST_BLOCK_SIZE
DIST_FW_BLOCK_SIZE       = 16384   # firmware block size (Phase 6): a flash-sector
                                   # multiple (4096) so the cone can erase/write per
                                   # block; kept modest to bound cone RAM
ALGO_NONE    = 0
ALGO_DEFLATE = 1                   # zlib stream (firmware inflate_buf auto-detects)
COMPRESS_LEVEL = 6

# Phase 5 FEC: per-block systematic Reed-Solomon parity (rs across chunks).
DIST_PARITY_FRAC = 0.10            # R ~ this fraction of N data chunks per block
DIST_PARITY_MIN  = 2               # ...but at least this many (capped at N+R <= 255)


def split_blocks(data: bytes, block_size: int = DIST_BLOCK_SIZE) -> list:
    """Slice raw bytes into uncompressed blocks (>=1 block, even for empty data)."""
    if not data:
        return [b""]
    return [data[i:i + block_size] for i in range(0, len(data), block_size)]


def encode_file(data: bytes, block_size: int = DIST_BLOCK_SIZE,
                level: int = COMPRESS_LEVEL):
    """Plan a file for on-air transfer.

    Returns (algo, blocks) where `blocks` is the list of per-block byte strings
    AS SENT (compressed when algo == ALGO_DEFLATE, raw when ALGO_NONE). The
    deflate-vs-store choice is per file: deflate only if it shrinks the whole
    file (so already-compressed payloads aren't bloated). Deterministic, so the
    manifest builder and the carousel agree without sharing state.
    """
    raw = split_blocks(data, block_size)
    comp = [zlib.compress(b, level) for b in raw]
    if sum(len(c) for c in comp) < len(data):
        return ALGO_DEFLATE, comp
    return ALGO_NONE, raw


def _make_dist(ptype: int, *, manifest_serial: int, file_id: int, file_len: int,
               block_index: int, total_blocks: int, chunk_index: int,
               data_chunks: int, parity_chunks: int, block_comp_len: int,
               payload: bytes, network: int = 0) -> bytes:
    body = struct.pack(DIST_HDR_FMT,
                       manifest_serial & 0xFFFF, file_id & 0xFFFF,
                       file_len & 0xFFFFFFFF,
                       block_index & 0xFFFF, total_blocks & 0xFFFF,
                       chunk_index & 0xFFFF, data_chunks & 0xFFFF,
                       parity_chunks & 0xFFFF, block_comp_len & 0xFFFFFFFF)
    return make_header(ptype, network, ADDR_MASTER, ADDR_BROADCAST) + body + payload


def make_dist_data(*, manifest_serial: int, file_id: int, file_len: int,
                   block_index: int, total_blocks: int, chunk_index: int,
                   data_chunks: int, parity_chunks: int, block_comp_len: int,
                   payload: bytes, network: int = 0) -> bytes:
    """Build a DIST_DATA packet (chunk_index is a DATA index 0..N-1)."""
    return _make_dist(PKT_DIST_DATA, manifest_serial=manifest_serial, file_id=file_id,
                      file_len=file_len, block_index=block_index, total_blocks=total_blocks,
                      chunk_index=chunk_index, data_chunks=data_chunks,
                      parity_chunks=parity_chunks, block_comp_len=block_comp_len,
                      payload=payload, network=network)


def make_dist_parity(*, manifest_serial: int, file_id: int, file_len: int,
                     block_index: int, total_blocks: int, chunk_index: int,
                     data_chunks: int, parity_chunks: int, block_comp_len: int,
                     payload: bytes, network: int = 0) -> bytes:
    """Build a DIST_PARITY packet (chunk_index is a PARITY index 0..R-1)."""
    return _make_dist(PKT_DIST_PARITY, manifest_serial=manifest_serial, file_id=file_id,
                      file_len=file_len, block_index=block_index, total_blocks=total_blocks,
                      chunk_index=chunk_index, data_chunks=data_chunks,
                      parity_chunks=parity_chunks, block_comp_len=block_comp_len,
                      payload=payload, network=network)


def parse_header(pkt: bytes):
    """Return (type, network, src, dst) or None if too short."""
    if len(pkt) < HEADER_LEN:
        return None
    return struct.unpack(HEADER_FMT, pkt[:HEADER_LEN])


def parse_beacon(pkt: bytes) -> dict | None:
    """Parse a BEACON packet body into a dict, or None if malformed."""
    if len(pkt) < HEADER_LEN + BEACON_LEN:
        return None
    (ver, es, ems, mode, freq, bw, sf, cr, sync, serial, cs) = \
        struct.unpack(BEACON_FMT, pkt[HEADER_LEN:HEADER_LEN + BEACON_LEN])
    return dict(version=ver, epoch_s=es, epoch_ms=ems, mode=mode, freq=freq,
                bw=bw, sf=sf, cr=cr, sync_word=sync, manifest_serial=serial,
                callsign=cs.decode("ascii", "replace").rstrip())
