#ifdef INCLUDE_WASM

#include "wasm_internal.h"
#include "m3_env.h"
#include "m3_config.h"

#if d_m3UsePsramMemory
#include "psram.h"

// ---- m3_psram_* glue (called from wasm3 fork internals) ----

extern "C" {

void m3_psram_read(uint32_t addr, uint8_t *buf, size_t len)
{
    psram_read(addr, buf, len);
}

void m3_psram_write(uint32_t addr, const uint8_t *buf, size_t len)
{
    psram_write(addr, buf, len);
}

void m3_psram_memset(uint32_t addr, uint8_t val, size_t len)
{
    psram_memset(addr, val, len);
}

void m3_psram_memcpy(uint32_t dst_addr, uint32_t src_addr, size_t len)
{
    psram_memcpy(dst_addr, src_addr, len);
}

uint32_t m3_psram_alloc(size_t size)
{
    return psram_malloc(size);
}

void m3_psram_free(uint32_t addr)
{
    psram_free(addr);
}

uint32_t m3_psram_yield_ctr = 0;

// ---- Split-aware helpers for bulk ops that may straddle the DRAM/PSRAM boundary ----

void m3_split_read(uint8_t *dram_buf, uint32_t psram_addr, uint32_t offset, uint8_t *dst, uint32_t len)
{
    uint32_t end = offset + len;
    if (end <= d_m3PsramDramWindow) {
        memcpy(dst, dram_buf + offset, len);
    } else if (offset >= d_m3PsramDramWindow) {
        psram_read(psram_addr + offset - d_m3PsramDramWindow, dst, len);
    } else {
        uint32_t dram_part = d_m3PsramDramWindow - offset;
        memcpy(dst, dram_buf + offset, dram_part);
        psram_read(psram_addr, dst + dram_part, len - dram_part);
    }
}

void m3_split_write(uint8_t *dram_buf, uint32_t psram_addr, uint32_t offset, const uint8_t *src, uint32_t len)
{
    uint32_t end = offset + len;
    if (end <= d_m3PsramDramWindow) {
        memcpy(dram_buf + offset, src, len);
    } else if (offset >= d_m3PsramDramWindow) {
        psram_write(psram_addr + offset - d_m3PsramDramWindow, src, len);
    } else {
        uint32_t dram_part = d_m3PsramDramWindow - offset;
        memcpy(dram_buf + offset, src, dram_part);
        psram_write(psram_addr, src + dram_part, len - dram_part);
    }
}

void m3_split_set(uint8_t *dram_buf, uint32_t psram_addr, uint32_t offset, uint8_t val, uint32_t len)
{
    uint32_t end = offset + len;
    if (end <= d_m3PsramDramWindow) {
        memset(dram_buf + offset, val, len);
    } else if (offset >= d_m3PsramDramWindow) {
        psram_memset(psram_addr + offset - d_m3PsramDramWindow, val, len);
    } else {
        uint32_t dram_part = d_m3PsramDramWindow - offset;
        memset(dram_buf + offset, val, dram_part);
        psram_memset(psram_addr, val, len - dram_part);
    }
}

void m3_split_move(uint8_t *dram_buf, uint32_t psram_addr, uint32_t dst_off, uint32_t src_off, uint32_t len)
{
    uint8_t tmp[256];
    while (len > 0) {
        uint32_t chunk = (len > sizeof(tmp)) ? (uint32_t)sizeof(tmp) : len;
        m3_split_read(dram_buf, psram_addr, src_off, tmp, chunk);
        m3_split_write(dram_buf, psram_addr, dst_off, tmp, chunk);
        src_off += chunk;
        dst_off += chunk;
        len -= chunk;
    }
}

} // extern "C"
#endif // d_m3UsePsramMemory


// ---- wasm_mem_* helpers (used by host imports) ----

uint32_t wasm_mem_size(IM3Runtime rt)
{
    return (uint32_t)rt->memory.mallocated->length;
}

bool wasm_mem_check(IM3Runtime rt, uint32_t offset, size_t len)
{
    return (uint64_t)offset + len <= rt->memory.mallocated->length;
}

void wasm_mem_read(IM3Runtime rt, uint32_t offset, void *dst, size_t len)
{
#if d_m3UsePsramMemory
    M3MemoryHeader *hdr = rt->memory.mallocated;
    uint32_t end = offset + (uint32_t)len;
    if (end <= d_m3PsramDramWindow) {
        memcpy(dst, hdr->dram_buf + offset, len);
    } else if (offset >= d_m3PsramDramWindow) {
        psram_read(hdr->psram_addr + offset - d_m3PsramDramWindow, (uint8_t *)dst, len);
    } else {
        uint32_t dram_part = d_m3PsramDramWindow - offset;
        memcpy(dst, hdr->dram_buf + offset, dram_part);
        psram_read(hdr->psram_addr, (uint8_t *)dst + dram_part, len - dram_part);
    }
#else
    uint8_t *base = m3MemData(rt->memory.mallocated);
    memcpy(dst, base + offset, len);
#endif
}

void wasm_mem_write(IM3Runtime rt, uint32_t offset, const void *src, size_t len)
{
#if d_m3UsePsramMemory
    M3MemoryHeader *hdr = rt->memory.mallocated;
    uint32_t end = offset + (uint32_t)len;
    if (end <= d_m3PsramDramWindow) {
        memcpy(hdr->dram_buf + offset, src, len);
    } else if (offset >= d_m3PsramDramWindow) {
        psram_write(hdr->psram_addr + offset - d_m3PsramDramWindow, (const uint8_t *)src, len);
    } else {
        uint32_t dram_part = d_m3PsramDramWindow - offset;
        memcpy(hdr->dram_buf + offset, src, dram_part);
        psram_write(hdr->psram_addr, (const uint8_t *)src + dram_part, len - dram_part);
    }
#else
    uint8_t *base = m3MemData(rt->memory.mallocated);
    memcpy(base + offset, src, len);
#endif
}

uint8_t wasm_mem_read8(IM3Runtime rt, uint32_t offset)
{
    uint8_t v;
    wasm_mem_read(rt, offset, &v, 1);
    return v;
}

void wasm_mem_write8(IM3Runtime rt, uint32_t offset, uint8_t val)
{
    wasm_mem_write(rt, offset, &val, 1);
}

int wasm_mem_read_str(IM3Runtime rt, uint32_t offset, char *dst, size_t max)
{
    uint32_t mem_len = wasm_mem_size(rt);
    size_t i = 0;
    while (offset + i < mem_len && i < max - 1) {
        uint8_t c = wasm_mem_read8(rt, offset + (uint32_t)i);
        if (c == 0) break;
        dst[i] = (char)c;
        i++;
    }
    dst[i] = '\0';
    return (int)i;
}

// Bounded strlen in WASM memory — returns length or -1 on out-of-bounds
int wasm_mem_strlen(IM3Runtime rt, uint32_t ptr)
{
    uint32_t mem_len = wasm_mem_size(rt);
    if (ptr >= mem_len) return -1;
#if d_m3UsePsramMemory
    M3MemoryHeader *hdr = rt->memory.mallocated;
    uint32_t pos = ptr;
    // Fast path: scan DRAM buffer directly
    if (pos < d_m3PsramDramWindow) {
        uint32_t limit = (mem_len < d_m3PsramDramWindow) ? mem_len : d_m3PsramDramWindow;
        while (pos < limit) {
            if (hdr->dram_buf[pos] == 0) return (int)(pos - ptr);
            pos++;
        }
    }
    // Continue in PSRAM
    while (pos < mem_len) {
        uint8_t c;
        psram_read(hdr->psram_addr + pos - d_m3PsramDramWindow, &c, 1);
        if (c == 0) return (int)(pos - ptr);
        pos++;
    }
    return -1;
#else
    uint8_t *base = m3MemData(rt->memory.mallocated);
    int len = 0;
    while (ptr + (uint32_t)len < mem_len) {
        if (base[ptr + len] == 0) return len;
        len++;
    }
    return -1;
#endif
}

// Bulk copy between two WASM memory offsets
void wasm_mem_copy(IM3Runtime rt, uint32_t dst, uint32_t src, size_t len)
{
#if d_m3UsePsramMemory
    M3MemoryHeader *hdr = rt->memory.mallocated;
    uint8_t tmp[256];
    while (len > 0) {
        size_t chunk = (len > sizeof(tmp)) ? sizeof(tmp) : len;
        m3_split_read(hdr->dram_buf, hdr->psram_addr, src, tmp, (uint32_t)chunk);
        m3_split_write(hdr->dram_buf, hdr->psram_addr, dst, tmp, (uint32_t)chunk);
        src += (uint32_t)chunk;
        dst += (uint32_t)chunk;
        len -= chunk;
    }
#else
    uint8_t *b = m3MemData(rt->memory.mallocated);
    memcpy(b + dst, b + src, len);
#endif
}

// Bulk memset in WASM memory
void wasm_mem_set(IM3Runtime rt, uint32_t offset, uint8_t val, size_t len)
{
#if d_m3UsePsramMemory
    M3MemoryHeader *hdr = rt->memory.mallocated;
    uint32_t end = offset + (uint32_t)len;
    if (end <= d_m3PsramDramWindow) {
        memset(hdr->dram_buf + offset, val, len);
    } else if (offset >= d_m3PsramDramWindow) {
        psram_memset(hdr->psram_addr + offset - d_m3PsramDramWindow, val, len);
    } else {
        uint32_t dram_part = d_m3PsramDramWindow - offset;
        memset(hdr->dram_buf + offset, val, dram_part);
        psram_memset(hdr->psram_addr, val, len - dram_part);
    }
#else
    uint8_t *base = m3MemData(rt->memory.mallocated);
    memset(base + offset, val, len);
#endif
}

#endif // INCLUDE_WASM
