#ifndef CONEZ_LORA_PROTO_H
#define CONEZ_LORA_PROTO_H

// ConeZ LoRa Protocol v1 -- wire format (firmware side).
//
// SHARED DEFINITION: keep byte-for-byte in sync with lora-master/lora_proto.py.
// See documentation/lora-protocol.txt. All multi-byte fields are BIG-ENDIAN.
//
// Common header (4 bytes): type, network, src, dst.
// Addresses: 0 = master, 1..254 = cone, 255 = broadcast.

#include <stdint.h>
#include <stddef.h>

#define LORA_PROTO_VERSION   1

// Packet types
#define LP_PKT_BEACON        0x01
#define LP_PKT_REG_REQ       0x10
#define LP_PKT_REG_RESP      0x11
#define LP_PKT_POLL_REQ      0x20
#define LP_PKT_POLL_RESP     0x21
#define LP_PKT_CUE           0x30
#define LP_PKT_DIST_DATA     0x40
#define LP_PKT_DIST_PARITY   0x41

// Addresses
#define LP_ADDR_MASTER       0
#define LP_ADDR_BROADCAST    255

// Channel modes
#define LP_MODE_LORA         0
#define LP_MODE_FSK          1

// Common header (4 bytes)
#define LP_HDR_LEN           4
#define LP_HDR_TYPE          0
#define LP_HDR_NET           1
#define LP_HDR_SRC           2
#define LP_HDR_DST           3

// BEACON body offsets (absolute, from start of packet; header is 4 bytes).
// Body layout (30 bytes): ver(1) epoch_s(4) epoch_ms(2) mode(1) freq(4) bw(4)
//                         sf(1) cr(1) sync(2) manifest_serial(2) callsign(8)
#define LP_BCN_VERSION       (LP_HDR_LEN + 0)   // u8
#define LP_BCN_EPOCH_S       (LP_HDR_LEN + 1)   // u32
#define LP_BCN_EPOCH_MS      (LP_HDR_LEN + 5)   // u16
#define LP_BCN_MODE          (LP_HDR_LEN + 7)   // u8
#define LP_BCN_FREQ          (LP_HDR_LEN + 8)   // u32
#define LP_BCN_BW            (LP_HDR_LEN + 12)  // u32
#define LP_BCN_SF            (LP_HDR_LEN + 16)  // u8
#define LP_BCN_CR            (LP_HDR_LEN + 17)  // u8
#define LP_BCN_SYNC          (LP_HDR_LEN + 18)  // u16
#define LP_BCN_MANIFEST      (LP_HDR_LEN + 20)  // u16
#define LP_BCN_CALLSIGN      (LP_HDR_LEN + 22)  // 8 bytes ASCII, space-padded
#define LP_BCN_CALLSIGN_LEN  8
#define LP_BCN_LEN           (LP_HDR_LEN + 30)  // total beacon packet length

// DIST body offsets (absolute, from start of packet; header is 4 bytes). Shared
// by DIST_DATA (0x40) and DIST_PARITY (0x41) -- the packet TYPE tells you whether
// chunk_idx indexes a DATA chunk (0..N-1) or a PARITY chunk (0..R-1).
//
// Phase 4: per-BLOCK transfer. A file = total_blocks blocks of (uncompressed)
// block_size bytes; each block is independently (de)compressed.
// Phase 5: each block is also systematic-RS-coded ACROSS chunks -- N data chunks
// (each LP_DIST_CHUNK_SIZE bytes, last data chunk zero-padded for the code) plus
// R Cauchy parity chunks (rs.c / reed_solomon.py). A cone needs any N of the N+R
// to rebuild the block. block_comp_len is the true compressed length (so padding
// is unambiguous even when the last data chunk is recovered from parity).
// Block/file geometry + algo for data files is in the manifest (section 13.2);
// the wire repeats N/R/comp_len so reception is self-describing.
//
// Body (22 bytes): manifest_serial(2) file_id(2) file_len(4) block_idx(2)
//   total_blocks(2) chunk_idx(2) data_chunks(2) parity_chunks(2) block_comp_len(4)
#define LP_DIST_HDR_LEN         22
#define LP_DIST_SERIAL          (LP_HDR_LEN + 0)    // u16 manifest serial
#define LP_DIST_FILE_ID         (LP_HDR_LEN + 2)    // u16 file id (0 = manifest)
#define LP_DIST_FILE_LEN        (LP_HDR_LEN + 4)    // u32 total uncompressed file length
#define LP_DIST_BLOCK_IDX       (LP_HDR_LEN + 8)    // u16 block index
#define LP_DIST_TOTAL_BLOCKS    (LP_HDR_LEN + 10)   // u16 total blocks in this file
#define LP_DIST_CHUNK_IDX       (LP_HDR_LEN + 12)   // u16 data idx (0x40) / parity idx (0x41)
#define LP_DIST_DATA_CHUNKS     (LP_HDR_LEN + 14)   // u16 N data chunks in this block
#define LP_DIST_PARITY_CHUNKS   (LP_HDR_LEN + 16)   // u16 R parity chunks in this block
#define LP_DIST_BLOCK_COMP_LEN  (LP_HDR_LEN + 18)   // u32 compressed length of this block
#define LP_DIST_PAYLOAD         (LP_HDR_LEN + 22)   // payload starts here
#define LP_DIST_CHUNK_SIZE      200                 // payload bytes per chunk (RS symbol size)
#define LP_DIST_MANIFEST_ID     0                   // reserved file id for the manifest
#define LP_DIST_BLOCK_SIZE      32768               // block size assumed for the manifest
                                                    // file (data files carry their own)

// Per-file compression algorithm (manifest field; LP_DIST_ALGO_DEFLATE = zlib).
#define LP_DIST_ALGO_NONE     0
#define LP_DIST_ALGO_DEFLATE  1

// Big-endian readers (wire is big-endian).
static inline uint16_t lp_rd_u16(const uint8_t *p)
{
    return (uint16_t)((p[0] << 8) | p[1]);
}

static inline uint32_t lp_rd_u32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8)  |  (uint32_t)p[3];
}

#endif // CONEZ_LORA_PROTO_H
