#!/usr/bin/env python3
"""Systematic Reed-Solomon erasure code over GF(2^8) for the dist FEC (Phase 5).

Mirror of the firmware's firmware/src/lora/rs.c -- keep byte-for-byte identical
(same primitive poly 0x11D, generator 2, Cauchy matrix x_p=N+p / y_j=j). Operates
ACROSS chunks: a block is N data + R parity chunks each `len` bytes; RS runs
per byte-column, so all chunks are the same length (pad the last data chunk with
zeros). The master only needs encode(); decode() is here for tests / symmetry.
"""

# GF(2^8) tables (poly 0x11D, generator 2)
_EXP = [0] * 512
_LOG = [0] * 256
_x = 1
for _i in range(255):
    _EXP[_i] = _x
    _LOG[_x] = _i
    _x <<= 1
    if _x & 0x100:
        _x ^= 0x11D
for _i in range(255, 512):
    _EXP[_i] = _EXP[_i - 255]


def gf_mul(a, b):
    if a == 0 or b == 0:
        return 0
    return _EXP[_LOG[a] + _LOG[b]]


def gf_inv(a):
    return _EXP[255 - _LOG[a]]   # a != 0


def _cauchy(N, p, j):
    # C(p,j) = 1 / (x_p XOR y_j), x_p=N+p, y_j=j (disjoint -> nonzero)
    return gf_inv(((N + p) ^ j) & 0xFF)


def encode(N, R, length, data):
    """data: bytes of length N*length (chunk j at [j*length:(j+1)*length]).
    Returns R*length parity bytes (chunk p at [p*length:(p+1)*length])."""
    parity = bytearray(R * length)
    for p in range(R):
        base = p * length
        for j in range(N):
            coef = _cauchy(N, p, j)
            if coef == 0:
                continue
            lc = _LOG[coef]
            doff = j * length
            for c in range(length):
                dv = data[doff + c]
                if dv:
                    parity[base + c] ^= _EXP[lc + _LOG[dv]]
    return bytes(parity)


def _mat_inv(M, e):
    """Invert e x e GF(2^8) matrix (list of bytearrays) -> Minv. None if singular."""
    Minv = [bytearray(e) for _ in range(e)]
    for i in range(e):
        Minv[i][i] = 1
    M = [bytearray(row) for row in M]
    for col in range(e):
        piv = next((r for r in range(col, e) if M[r][col]), -1)
        if piv < 0:
            return None
        if piv != col:
            M[piv], M[col] = M[col], M[piv]
            Minv[piv], Minv[col] = Minv[col], Minv[piv]
        inv = gf_inv(M[col][col])
        for j in range(e):
            M[col][j] = gf_mul(M[col][j], inv)
            Minv[col][j] = gf_mul(Minv[col][j], inv)
        for r in range(e):
            if r == col:
                continue
            f = M[r][col]
            if not f:
                continue
            for j in range(e):
                M[r][j] ^= gf_mul(f, M[col][j])
                Minv[r][j] ^= gf_mul(f, Minv[col][j])
    return Minv


def decode(N, R, length, data, have_data, parity, have_par):
    """In-place erasure decode. data: bytearray N*length; parity: bytes R*length.
    have_data/have_par: 0/1 lists. Returns True if all data recovered."""
    miss = [j for j in range(N) if not have_data[j]]
    e = len(miss)
    if e == 0:
        return True
    sp = [p for p in range(R) if have_par[p]][:e]
    if len(sp) < e:
        return False
    M = [bytearray(_cauchy(N, sp[r], miss[k]) for k in range(e)) for r in range(e)]
    Minv = _mat_inv(M, e)
    if Minv is None:
        return False
    pcoef = [[(_cauchy(N, sp[r], j) if have_data[j] else 0) for j in range(N)] for r in range(e)]
    for c in range(length):
        syn = [0] * e
        for r in range(e):
            acc = parity[sp[r] * length + c]
            pc = pcoef[r]
            for j in range(N):
                if pc[j]:
                    acc ^= gf_mul(pc[j], data[j * length + c])
            syn[r] = acc
        for k in range(e):
            acc = 0
            mr = Minv[k]
            for r in range(e):
                acc ^= gf_mul(mr[r], syn[r])
            data[miss[k] * length + c] = acc
    return True


def parity_count(N, frac, minimum):
    """R for a block of N data chunks: max(minimum, ceil(N*frac)), capped N+R<=255."""
    import math
    R = max(minimum, math.ceil(N * frac))
    return max(0, min(R, 255 - N))
