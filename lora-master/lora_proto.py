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


# DIST_DATA body (after the header):
#   H  manifest serial
#   H  file id            (0 = the manifest itself)
#   I  file length        (total uncompressed bytes)
#   H  chunk index
#   H  total chunks
#   <payload>             (DIST_CHUNK_SIZE bytes; last chunk may be short)
DIST_HDR_FMT    = ">HHIHH"
DIST_HDR_LEN    = struct.calcsize(DIST_HDR_FMT)   # 12
DIST_CHUNK_SIZE = 200                             # payload bytes per chunk
DIST_MANIFEST_ID = 0                              # reserved file id for the manifest


def make_dist_data(*, manifest_serial: int, file_id: int, file_len: int,
                   chunk_index: int, total_chunks: int, payload: bytes,
                   network: int = 0) -> bytes:
    """Build a full DIST_DATA packet (header + dist header + payload)."""
    body = struct.pack(DIST_HDR_FMT,
                       manifest_serial & 0xFFFF, file_id & 0xFFFF,
                       file_len & 0xFFFFFFFF, chunk_index & 0xFFFF, total_chunks & 0xFFFF)
    return make_header(PKT_DIST_DATA, network, ADDR_MASTER, ADDR_BROADCAST) + body + payload


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
