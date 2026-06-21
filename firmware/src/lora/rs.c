// Systematic Reed-Solomon erasure code over GF(2^8). See rs.h.
// Shared math with lora-master/reed_solomon.py -- keep byte-for-byte identical.

#include "rs.h"
#include <stdlib.h>
#include <string.h>

// GF(2^8), primitive polynomial 0x11D (x^8+x^4+x^3+x^2+1), generator 2.
static uint8_t gf_exp[512];
static uint8_t gf_log[256];
static int     gf_ready = 0;

void rs_init(void)
{
    if (gf_ready) return;
    int x = 1;
    for (int i = 0; i < 255; i++) {
        gf_exp[i] = (uint8_t)x;
        gf_log[x] = (uint8_t)i;
        x <<= 1;
        if (x & 0x100) x ^= 0x11D;
    }
    for (int i = 255; i < 512; i++) gf_exp[i] = gf_exp[i - 255];
    gf_log[0] = 0;  // unused (gf_mul guards 0); defined for safety
    gf_ready = 1;
}

static inline uint8_t gf_mul(uint8_t a, uint8_t b)
{
    if (a == 0 || b == 0) return 0;
    return gf_exp[(int)gf_log[a] + (int)gf_log[b]];
}

static inline uint8_t gf_inv(uint8_t a)
{
    // a != 0 required
    return gf_exp[255 - gf_log[a]];
}

// Cauchy matrix element for parity row p (p in 0..R-1) and data col j (0..N-1):
//   C(p,j) = 1 / (x_p XOR y_j),  x_p = N+p,  y_j = j   (disjoint -> XOR != 0)
// Every square submatrix of a Cauchy matrix is invertible -> the code is MDS.
static inline uint8_t cauchy(int N, int p, int j)
{
    return gf_inv((uint8_t)((N + p) ^ j));
}

void rs_encode(int N, int R, size_t len, const uint8_t *data, uint8_t *parity)
{
    rs_init();
    for (int p = 0; p < R; p++) {
        uint8_t *par = parity + (size_t)p * len;
        memset(par, 0, len);
        for (int j = 0; j < N; j++) {
            uint8_t coef = cauchy(N, p, j);
            if (coef == 0) continue;               // never happens (Cauchy)
            int lc = gf_log[coef];
            const uint8_t *d = data + (size_t)j * len;
            for (size_t c = 0; c < len; c++) {
                uint8_t dv = d[c];
                if (dv) par[c] ^= gf_exp[lc + (int)gf_log[dv]];
            }
        }
    }
}

// Invert e x e matrix M (row-major) into Minv via Gauss-Jordan over GF(2^8).
// Returns 0 on success, -1 if singular (cannot happen for a Cauchy submatrix).
static int gf_mat_inv(uint8_t *M, uint8_t *Minv, int e)
{
    memset(Minv, 0, (size_t)e * e);
    for (int i = 0; i < e; i++) Minv[i * e + i] = 1;

    for (int col = 0; col < e; col++) {
        int piv = -1;
        for (int r = col; r < e; r++) if (M[r * e + col]) { piv = r; break; }
        if (piv < 0) return -1;
        if (piv != col) {
            for (int j = 0; j < e; j++) {
                uint8_t t;
                t = M[piv * e + j];    M[piv * e + j]    = M[col * e + j];    M[col * e + j]    = t;
                t = Minv[piv * e + j]; Minv[piv * e + j] = Minv[col * e + j]; Minv[col * e + j] = t;
            }
        }
        uint8_t inv = gf_inv(M[col * e + col]);
        for (int j = 0; j < e; j++) {
            M[col * e + j]    = gf_mul(M[col * e + j], inv);
            Minv[col * e + j] = gf_mul(Minv[col * e + j], inv);
        }
        for (int r = 0; r < e; r++) {
            if (r == col) continue;
            uint8_t f = M[r * e + col];
            if (!f) continue;
            for (int j = 0; j < e; j++) {
                M[r * e + j]    ^= gf_mul(f, M[col * e + j]);
                Minv[r * e + j] ^= gf_mul(f, Minv[col * e + j]);
            }
        }
    }
    return 0;
}

int rs_decode(int N, int R, size_t len,
              uint8_t *data, const uint8_t *have_data,
              const uint8_t *parity, const uint8_t *have_par)
{
    rs_init();

    // Missing data columns (erasures).
    int *miss = (int *)malloc(sizeof(int) * (size_t)(N > 0 ? N : 1));
    if (!miss) return -1;
    int e = 0;
    for (int j = 0; j < N; j++) if (!have_data[j]) miss[e++] = j;
    if (e == 0) { free(miss); return 0; }          // nothing to recover

    // Surviving parity rows (need e of them).
    int *sp = (int *)malloc(sizeof(int) * (size_t)(R > 0 ? R : 1));
    if (!sp) { free(miss); return -1; }
    int s = 0;
    for (int p = 0; p < R && s < e; p++) if (have_par[p]) sp[s++] = p;
    if (s < e) { free(miss); free(sp); return -1; } // not enough parity yet

    // e x e matrix M[r][k] = C(sp[r], miss[k]); invert it.
    uint8_t *M    = (uint8_t *)malloc((size_t)e * e);
    uint8_t *Minv = (uint8_t *)malloc((size_t)e * e);
    // pcoef[r][j] = C(sp[r], j) for present data j (else 0): syndrome coefficients.
    uint8_t *pcoef = (uint8_t *)malloc((size_t)e * (size_t)N);
    uint8_t *syn   = (uint8_t *)malloc((size_t)e);
    if (!M || !Minv || !pcoef || !syn) { free(miss); free(sp); free(M); free(Minv); free(pcoef); free(syn); return -1; }

    for (int r = 0; r < e; r++) {
        for (int k = 0; k < e; k++) M[r * e + k] = cauchy(N, sp[r], miss[k]);
        for (int j = 0; j < N; j++) pcoef[r * N + j] = have_data[j] ? cauchy(N, sp[r], j) : 0;
    }
    if (gf_mat_inv(M, Minv, e) != 0) { free(miss); free(sp); free(M); free(Minv); free(pcoef); free(syn); return -1; }

    for (size_t c = 0; c < len; c++) {
        for (int r = 0; r < e; r++) {
            uint8_t acc = parity[(size_t)sp[r] * len + c];
            const uint8_t *pc = pcoef + (size_t)r * N;
            for (int j = 0; j < N; j++) {
                if (pc[j]) acc ^= gf_mul(pc[j], data[(size_t)j * len + c]);
            }
            syn[r] = acc;
        }
        for (int k = 0; k < e; k++) {
            uint8_t acc = 0;
            const uint8_t *mr = Minv + (size_t)k * e;
            for (int r = 0; r < e; r++) acc ^= gf_mul(mr[r], syn[r]);
            data[(size_t)miss[k] * len + c] = acc;
        }
    }

    free(miss); free(sp); free(M); free(Minv); free(pcoef); free(syn);
    return 0;
}
