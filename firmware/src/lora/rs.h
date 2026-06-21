#ifndef CONEZ_RS_H
#define CONEZ_RS_H

// Systematic Reed-Solomon erasure code over GF(2^8) for the dist FEC (Phase 5).
//
// Operates ACROSS chunks: a block is N data chunks + R parity chunks, each of
// `len` bytes (one chunk = one RS symbol position). RS is applied independently
// per byte-column, so all chunks must be the same `len` (pad the last data chunk
// with zeros). The generator is systematic [I_N ; C] with C an R x N Cauchy
// matrix, so it is MDS: any N of the N+R chunks reconstruct the block, and since
// PHY CRC tells us WHICH chunks are missing, this is pure ERASURE decoding.
//
// Pure C, no platform deps (host-compilable: shared math with the master's
// lora-master/reed_solomon.py -- keep the two byte-for-byte identical).
//
// Constraint: N + R <= 255.

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// Idempotent; rs_encode/rs_decode call it themselves.
void rs_init(void);

// Systematic encode. data = N*len contiguous bytes (chunk j at data + j*len);
// parity = R*len contiguous bytes (written; chunk p at parity + p*len).
void rs_encode(int N, int R, size_t len, const uint8_t *data, uint8_t *parity);

// Erasure decode, in place. have_data[j]/have_par[p] are 0/1 presence flags.
// Missing data chunks (have_data[j]==0) are reconstructed into data[]. Returns 0
// on success (all N data chunks valid), -1 if there is not enough parity to
// recover (caller should wait for the next carousel cycle). A no-erasure call
// (all data present) returns 0 immediately.
int rs_decode(int N, int R, size_t len,
              uint8_t *data, const uint8_t *have_data,
              const uint8_t *parity, const uint8_t *have_par);

#ifdef __cplusplus
}
#endif

#endif // CONEZ_RS_H
