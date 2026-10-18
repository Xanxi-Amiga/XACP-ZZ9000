/*
 * zz_image.c - XACP JPEG/PNG image service for XX19d-RIX / XACP v1.8
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Core0-only.  Heavy work is deferred out of the Zorro command write path.
 * JPEG and PNG share one scalar stb_image backend compiled at -Os.
 *
 * Public v1.3 session protocol:
 *   QUERY -> BEGIN -> FEED* -> DECODE[/FIT] -> GET_TILE* or DIRECT_P96 -> CLOSE
 *                                    \-> CANCEL is valid between commands
 *
 * The shared transport is the v1.8-owned 1 MiB window:
 *   fb+0x06300000 .. fb+0x06400000
 *   ARM 0x06500000 .. 0x06600000
 *
 * Compressed input is spooled into the existing Core0/newlib heap with a hard
 * 32 MiB input cap.  Decoder allocations have an independent hard 72 MiB
 * quota, leaving at least 24 MiB nominal headroom in the 128 MiB Core0 heap.
 * JPEG output is row-streamed from stb_image's decoded component planes and
 * therefore never allocates a full RGBA frame.  PNG keeps the conservative
 * full-image stb path for now and has a separate 8 MiPixel ceiling.
 */

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <limits.h>
#include <stdlib.h>
#include "zz_image.h"

#ifndef ZZIMAGE_HOST
#include "xil_cache.h"
#endif

#define ZZIMAGE_XACP_CMD_OFFSET 0x04000000u
#define ZZIMAGE_FB_SIZE         (128u * 1024u * 1024u)
#define ZZIMAGE_CACHE_LINE      32u
#define ZZIMAGE_VERSION         ((uint32_t)XACP_IMAGE_SERVICE_VERSION)
#define ZZIMAGE_SHARED_OFFSET   ((uint32_t)XACP_IMAGE_SHARED_OFFSET)
#define ZZIMAGE_SHARED_SIZE     ((uint32_t)XACP_IMAGE_SHARED_SIZE)
#define ZZIMAGE_P96_END         ((uint32_t)XACP_IMAGE_DIRECT_P96_FB_END)

/* Public ABI structure, local alias for readability. */
typedef XACPImageCommand ZZImageXACPCommand;

static inline uint32_t zzimage_bswap32(uint32_t v)
{
    return ((v & 0x000000FFu) << 24) |
           ((v & 0x0000FF00u) <<  8) |
           ((v & 0x00FF0000u) >>  8) |
           ((v & 0xFF000000u) >> 24);
}
#define ZZ_FROM_BE32(v) zzimage_bswap32((uint32_t)(v))
#define ZZ_TO_BE32(v)   zzimage_bswap32((uint32_t)(v))

#ifdef ZZIMAGE_HOST
static void zzimage_cache_invalidate(const void *p, uint32_t n) {(void)p;(void)n;}
static void zzimage_cache_flush(const void *p, uint32_t n) {(void)p;(void)n;}
#else
static void zzimage_cache_invalidate(const void *p, uint32_t n)
{
    uintptr_t s = ((uintptr_t)p) & ~(uintptr_t)(ZZIMAGE_CACHE_LINE - 1u);
    uintptr_t e = (((uintptr_t)p + (uintptr_t)n + ZZIMAGE_CACHE_LINE - 1u) &
                   ~(uintptr_t)(ZZIMAGE_CACHE_LINE - 1u));
    if (e > s) Xil_DCacheInvalidateRange(s, (uint32_t)(e - s));
}
static void zzimage_cache_flush(const void *p, uint32_t n)
{
    uintptr_t s = ((uintptr_t)p) & ~(uintptr_t)(ZZIMAGE_CACHE_LINE - 1u);
    uintptr_t e = (((uintptr_t)p + (uintptr_t)n + ZZIMAGE_CACHE_LINE - 1u) &
                   ~(uintptr_t)(ZZIMAGE_CACHE_LINE - 1u));
    if (e > s) Xil_DCacheFlushRange(s, (uint32_t)(e - s));
}
#endif

/* ---------------------------------------------------------------------
 * Bounded decoder allocator for stb_image.
 * --------------------------------------------------------------------- */
#define ZZIMAGE_ALLOC_MAGIC 0x5A494D47u /* 'ZIMG' */

typedef struct ZZImageAllocHeader {
    struct ZZImageAllocHeader *prev;
    struct ZZImageAllocHeader *next;
    uint32_t total_size;
    uint32_t magic;
} ZZImageAllocHeader;

static ZZImageAllocHeader *zzimage_alloc_head;
static uint32_t zzimage_heap_used;
static uint32_t zzimage_heap_hw;
static uint32_t zzimage_last_heap_hw;
static int zzimage_heap_oom;
static int zzimage_heap_bad_free;

static int zzimage_heap_begin(void)
{
    ZZImageAllocHeader *h = zzimage_alloc_head;
    int had_leak = (h != NULL || zzimage_heap_used != 0u);
    while (h) {
        ZZImageAllocHeader *next = h->next;
        h->magic = 0u;
        free(h);
        h = next;
    }
    zzimage_alloc_head = NULL;
    zzimage_heap_used = 0u;
    zzimage_heap_hw = 0u;
    zzimage_heap_oom = 0;
    zzimage_heap_bad_free = 0;
    return had_leak ? 0 : 1;
}

static int zzimage_heap_finish(void)
{
    ZZImageAllocHeader *h = zzimage_alloc_head;
    int clean = (h == NULL && zzimage_heap_used == 0u && !zzimage_heap_bad_free);
    zzimage_last_heap_hw = zzimage_heap_hw;
    while (h) {
        ZZImageAllocHeader *next = h->next;
        h->magic = 0u;
        free(h);
        h = next;
    }
    zzimage_alloc_head = NULL;
    zzimage_heap_used = 0u;
    return clean;
}

static void *zzimage_malloc(size_t n)
{
    ZZImageAllocHeader *h;
    uint32_t total;

    if (n == 0u) n = 1u;
    if (n > (size_t)(UINT32_MAX - sizeof(ZZImageAllocHeader))) {
        zzimage_heap_oom = 1;
        return NULL;
    }
    total = (uint32_t)n + (uint32_t)sizeof(ZZImageAllocHeader);
    if (total > ZZIMAGE_HEAP_BUDGET ||
        zzimage_heap_used > ZZIMAGE_HEAP_BUDGET - total) {
        zzimage_heap_oom = 1;
        return NULL;
    }

    h = (ZZImageAllocHeader *)malloc((size_t)total);
    if (!h) {
        zzimage_heap_oom = 1;
        return NULL;
    }
    h->prev = NULL;
    h->next = zzimage_alloc_head;
    h->total_size = total;
    h->magic = ZZIMAGE_ALLOC_MAGIC;
    if (zzimage_alloc_head) zzimage_alloc_head->prev = h;
    zzimage_alloc_head = h;

    zzimage_heap_used += total;
    if (zzimage_heap_used > zzimage_heap_hw)
        zzimage_heap_hw = zzimage_heap_used;
    return (void *)(h + 1);
}

static void zzimage_free(void *p)
{
    ZZImageAllocHeader *h;
    if (!p) return;
    h = ((ZZImageAllocHeader *)p) - 1;
    if (h->magic != ZZIMAGE_ALLOC_MAGIC || h->total_size > zzimage_heap_used) {
        zzimage_heap_bad_free = 1;
        return;
    }
    if (h->prev) h->prev->next = h->next;
    else zzimage_alloc_head = h->next;
    if (h->next) h->next->prev = h->prev;
    zzimage_heap_used -= h->total_size;
    h->magic = 0u;
    free(h);
}

static void *zzimage_realloc_sized(void *oldp, size_t oldsz, size_t newsz)
{
    ZZImageAllocHeader *oldh;
    void *newp;
    size_t copy_n;
    size_t old_capacity;
    (void)oldsz;

    if (!oldp) return zzimage_malloc(newsz);
    if (newsz == 0u) {
        zzimage_free(oldp);
        return NULL;
    }
    oldh = ((ZZImageAllocHeader *)oldp) - 1;
    if (oldh->magic != ZZIMAGE_ALLOC_MAGIC ||
        oldh->total_size < sizeof(ZZImageAllocHeader)) {
        zzimage_heap_bad_free = 1;
        return NULL;
    }
    old_capacity = (size_t)oldh->total_size - sizeof(ZZImageAllocHeader);
    if (newsz <= old_capacity) return oldp;

    newp = zzimage_malloc(newsz);
    if (!newp) return NULL;
    copy_n = old_capacity < newsz ? old_capacity : newsz;
    if (copy_n) memcpy(newp, oldp, copy_n);
    zzimage_free(oldp);
    return newp;
}

#define STBI_MALLOC(sz)                    zzimage_malloc(sz)
#define STBI_REALLOC_SIZED(p,oldsz,newsz) zzimage_realloc_sized((p),(oldsz),(newsz))
#define STBI_FREE(p)                       zzimage_free(p)
#define STBI_ONLY_JPEG
#define STBI_ONLY_PNG
#define STBI_NO_STDIO
#define STBI_NO_HDR
#define STBI_NO_LINEAR
#define STBI_NO_SIMD
#define STBI_MAX_DIMENSIONS ZZIMAGE_MAX_WIDTH
#define STBI_ASSERT(x) ((void)0)
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#include "zz_image_jpeg_stream.inc"

uint32_t zzimage_heap_highwater(void) { return zzimage_last_heap_hw; }
uint32_t zzimage_heap_current(void) { return zzimage_heap_used; }

/* ---------------------------------------------------------------------
 * Session and deferred job state.
 * --------------------------------------------------------------------- */
#define ZZIMAGE_SESS_CLOSED  0u
#define ZZIMAGE_SESS_FEEDING 1u
#define ZZIMAGE_SESS_DECODED 2u

#define ZZIMAGE_DECODE_NONE        0u
#define ZZIMAGE_DECODE_JPEG_STREAM 1u
#define ZZIMAGE_DECODE_PNG_FULL    2u

typedef struct {
    uint8_t *input;
    uint32_t input_alloc;
    uint32_t expected;
    uint32_t received;
    unsigned char *decoded;          /* PNG full RGBA path only */
    ZZImageJPEGStream jpeg;          /* JPEG row-stream path */
    uint32_t source_width;
    uint32_t source_height;
    uint32_t width;                  /* output width after optional fit */
    uint32_t height;                 /* output height after optional fit */
    uint32_t comp;
    uint32_t format;
    uint32_t rgba_bytes;             /* output width*height*4 */
    uint32_t serial;
    uint32_t next_output_y;          /* PNG tiled output cursor */
    uint8_t decode_kind;
    uint8_t state;
    uint8_t active;
} ZZImageSession;

static ZZImageSession zzimage_session;
static uint32_t zzimage_session_serial;

uint32_t zzimage_input_bytes(void) { return zzimage_session.input_alloc; }
uint32_t zzimage_session_active(void) { return zzimage_session.active ? 1u : 0u; }

typedef struct {
    uint32_t subcmd;
    uint32_t in_off;
    uint32_t in_size;
    uint32_t out_off;
    uint32_t out_max;
    uint32_t p2;
    uint32_t p3;
    uint32_t p4;
    uint8_t pending;
} ZZImageJob;

static ZZImageJob zzimage_job;

static int zzimage_range_ok(uint32_t off, uint32_t size)
{
    return (off < ZZIMAGE_FB_SIZE && size <= ZZIMAGE_FB_SIZE - off);
}

static int zzimage_ranges_overlap(uint32_t a, uint32_t asz,
                                  uint32_t b, uint32_t bsz)
{
    if (asz == 0u || bsz == 0u) return 0;
    if (a > UINT32_MAX - asz || b > UINT32_MAX - bsz) return 1;
    return (a < b + bsz) && (b < a + asz);
}

static uint32_t zzimage_detect_format(const uint8_t *p, uint32_t n)
{
    if (n >= 8u && p[0] == 0x89u && p[1] == 0x50u && p[2] == 0x4Eu &&
        p[3] == 0x47u && p[4] == 0x0Du && p[5] == 0x0Au &&
        p[6] == 0x1Au && p[7] == 0x0Au)
        return ZZIMAGE_FMT_PNG;
    if (n >= 2u && p[0] == 0xFFu && p[1] == 0xD8u)
        return ZZIMAGE_FMT_JPEG;
    return ZZIMAGE_FMT_UNKNOWN;
}

static void zzimage_publish(ZZImageXACPCommand *c, uint32_t status,
                            uint32_t error)
{
    c->status = ZZ_TO_BE32(status);
    c->error  = ZZ_TO_BE32(error);
    zzimage_cache_flush(c, 128u);
}

static void zzimage_zero_result_fields(ZZImageXACPCommand *c)
{
    c->result1 = ZZ_TO_BE32(0u);
    c->result2 = ZZ_TO_BE32(0u);
}

static int zzimage_session_release(void)
{
    int clean;
    if (zzimage_session.jpeg.ready || zzimage_session.jpeg.jpeg ||
        zzimage_session.jpeg.row_rgba) {
        zzimage_jpeg_stream_release(&zzimage_session.jpeg);
    }
    if (zzimage_session.decoded) {
        stbi_image_free(zzimage_session.decoded);
        zzimage_session.decoded = NULL;
    }
    if (zzimage_session.input) {
        free(zzimage_session.input);
        zzimage_session.input = NULL;
    }
    zzimage_session.input_alloc = 0u;
    zzimage_session.expected = 0u;
    zzimage_session.received = 0u;
    zzimage_session.source_width = 0u;
    zzimage_session.source_height = 0u;
    zzimage_session.width = 0u;
    zzimage_session.height = 0u;
    zzimage_session.comp = 0u;
    zzimage_session.format = ZZIMAGE_FMT_UNKNOWN;
    zzimage_session.rgba_bytes = 0u;
    zzimage_session.next_output_y = 0u;
    zzimage_session.decode_kind = ZZIMAGE_DECODE_NONE;
    zzimage_session.state = ZZIMAGE_SESS_CLOSED;
    zzimage_session.active = 0u;
    clean = zzimage_heap_finish();
    return clean;
}

static void zzimage_fit_dimensions(uint32_t sw, uint32_t sh,
                                   uint32_t maxw, uint32_t maxh,
                                   uint32_t *ow, uint32_t *oh)
{
    uint32_t w = sw, h = sh;
    if (maxw && w > maxw) {
        h = (uint32_t)(((uint64_t)h * maxw) / w);
        if (!h) h = 1u;
        w = maxw;
    }
    if (maxh && h > maxh) {
        w = (uint32_t)(((uint64_t)w * maxh) / h);
        if (!w) w = 1u;
        h = maxh;
    }
    if (w > sw) w = sw;
    if (h > sh) h = sh;
    *ow = w;
    *oh = h;
}

static uint32_t zzimage_validate_dimensions_for_format(uint32_t fmt, int w, int h)
{
    uint32_t pixels;
    if (w <= 0 || h <= 0) return ZZIMAGE_ERR_DECODE;
    if ((uint32_t)w > ZZIMAGE_MAX_WIDTH || (uint32_t)h > ZZIMAGE_MAX_HEIGHT ||
        (uint32_t)w > ZZIMAGE_MAX_PIXELS / (uint32_t)h)
        return ZZIMAGE_ERR_TOO_LARGE;
    pixels = (uint32_t)w * (uint32_t)h;
    if (fmt == ZZIMAGE_FMT_PNG && pixels > ZZIMAGE_PNG_MAX_PIXELS)
        return ZZIMAGE_ERR_TOO_LARGE;
    return ZZIMAGE_ERR_NONE;
}

static uint32_t zzimage_output_footprint(uint32_t w, uint32_t h,
                                         uint32_t pitch, uint32_t *out)
{
    uint32_t row_bytes;
    if (w == 0u || h == 0u || w > UINT32_MAX / 4u)
        return ZZIMAGE_ERR_OVERFLOW;
    row_bytes = w * 4u;
    if (pitch < row_bytes) return ZZIMAGE_ERR_OUTPUT_TOO_SMALL;
    if (h > 1u && pitch > (UINT32_MAX - row_bytes) / (h - 1u))
        return ZZIMAGE_ERR_OVERFLOW;
    *out = (h - 1u) * pitch + row_bytes;
    return ZZIMAGE_ERR_NONE;
}

static void zzimage_copy_pixels(uint8_t *dst, uint32_t dst_pitch,
                                const uint8_t *src_rgba,
                                uint32_t width, uint32_t rows,
                                uint32_t out_fmt)
{
    uint32_t row_bytes = width * 4u;
    uint32_t y;
    if (out_fmt == ZZIMAGE_OUT_RGBA8 && dst_pitch == row_bytes) {
        memcpy(dst, src_rgba, row_bytes * rows);
        return;
    }
    for (y = 0u; y < rows; ++y) {
        const uint8_t *s = src_rgba + y * row_bytes;
        uint8_t *d = dst + y * dst_pitch;
        uint32_t x;
        if (out_fmt == ZZIMAGE_OUT_RGBA8) {
            memcpy(d, s, row_bytes);
            continue;
        }
        for (x = 0u; x < width; ++x) {
            d[0] = s[2];
            d[1] = s[1];
            d[2] = s[0];
            d[3] = s[3];
            s += 4;
            d += 4;
        }
    }
}

static void zzimage_copy_pixels_scaled(uint8_t *dst, uint32_t dst_pitch,
                                       const uint8_t *src_rgba,
                                       uint32_t source_width,
                                       uint32_t source_height,
                                       uint32_t output_width,
                                       uint32_t output_height,
                                       uint32_t start_y,
                                       uint32_t rows,
                                       uint32_t out_fmt)
{
    uint32_t oy, x;
    for (oy = 0u; oy < rows; ++oy) {
        uint32_t out_y = start_y + oy;
        uint32_t sy = (uint32_t)(((uint64_t)out_y * source_height) / output_height);
        const uint8_t *srcrow = src_rgba + sy * source_width * 4u;
        uint8_t *d = dst + oy * dst_pitch;
        for (x = 0u; x < output_width; ++x) {
            uint32_t sx = (uint32_t)(((uint64_t)x * source_width) / output_width);
            const uint8_t *sp = srcrow + sx * 4u;
            if (out_fmt == ZZIMAGE_OUT_BGRA8) {
                d[0] = sp[2]; d[1] = sp[1]; d[2] = sp[0]; d[3] = sp[3];
            } else {
                d[0] = sp[0]; d[1] = sp[1]; d[2] = sp[2]; d[3] = sp[3];
            }
            d += 4;
        }
    }
}

/* ---------------------------------------------------------------------
 * Deferred operations.
 * --------------------------------------------------------------------- */
static uint32_t zzimage_do_begin(ZZImageXACPCommand *c)
{
    uint32_t expected = zzimage_job.in_size;

    if (zzimage_session.active) return ZZIMAGE_ERR_BUSY;
    if (expected == 0u || expected > ZZIMAGE_MAX_INPUT_BYTES)
        return expected > ZZIMAGE_MAX_INPUT_BYTES ? ZZIMAGE_ERR_TOO_LARGE : ZZIMAGE_ERR_BOUNDS;
    if (!zzimage_heap_begin()) return ZZIMAGE_ERR_ALLOCATOR_STATE;

    memset(&zzimage_session, 0, sizeof(zzimage_session));
    zzimage_session.input = (uint8_t *)malloc((size_t)expected);
    if (!zzimage_session.input) {
        (void)zzimage_heap_finish();
        return ZZIMAGE_ERR_OOM;
    }
    zzimage_session.input_alloc = expected;
    zzimage_session.expected = expected;
    zzimage_session.serial = ++zzimage_session_serial;
    if (zzimage_session.serial == 0u)
        zzimage_session.serial = ++zzimage_session_serial;
    zzimage_session.state = ZZIMAGE_SESS_FEEDING;
    zzimage_session.active = 1u;

    c->input_offset    = ZZ_TO_BE32(ZZIMAGE_SHARED_OFFSET);
    c->input_size      = ZZ_TO_BE32(ZZIMAGE_SHARED_SIZE);
    c->output_offset   = ZZ_TO_BE32(ZZIMAGE_SHARED_OFFSET);
    c->output_max_size = ZZ_TO_BE32(ZZIMAGE_SHARED_SIZE);
    c->result1         = ZZ_TO_BE32(zzimage_session.serial);
    c->result2         = ZZ_TO_BE32(expected);
    return ZZIMAGE_ERR_NONE;
}

static uint32_t zzimage_do_feed(uint8_t *fb, ZZImageXACPCommand *c)
{
    uint32_t stream_off = zzimage_job.p2;
    uint32_t flags = zzimage_job.p3;
    uint32_t n = zzimage_job.in_size;
    uint8_t *shared;

    if (!zzimage_session.active || zzimage_session.state != ZZIMAGE_SESS_FEEDING)
        return ZZIMAGE_ERR_BAD_STATE;
    if (zzimage_job.in_off != ZZIMAGE_SHARED_OFFSET || n == 0u || n > ZZIMAGE_SHARED_SIZE)
        return ZZIMAGE_ERR_BOUNDS;
    if (stream_off != zzimage_session.received)
        return ZZIMAGE_ERR_SEQUENCE;
    if (n > zzimage_session.expected - zzimage_session.received)
        return ZZIMAGE_ERR_BOUNDS;

    shared = fb + ZZIMAGE_SHARED_OFFSET;
    zzimage_cache_invalidate(shared, n);
    memcpy(zzimage_session.input + zzimage_session.received, shared, n);
    zzimage_session.received += n;

    c->result1 = ZZ_TO_BE32(zzimage_session.received);
    c->result2 = ZZ_TO_BE32(zzimage_session.expected);
    if ((flags & XACP_IMAGE_FEED_FINAL) &&
        zzimage_session.received != zzimage_session.expected)
        return ZZIMAGE_ERR_INCOMPLETE_INPUT;
    return ZZIMAGE_ERR_NONE;
}

static uint32_t zzimage_do_session_decode(ZZImageXACPCommand *c)
{
    int w = 0, h = 0, comp = 0;
    uint32_t fmt, err, pixels, out_w, out_h;
    uint32_t max_w = zzimage_job.p2;
    uint32_t max_h = zzimage_job.p3;
    unsigned char *decoded = NULL;

    if (!zzimage_session.active || zzimage_session.state != ZZIMAGE_SESS_FEEDING)
        return ZZIMAGE_ERR_BAD_STATE;
    if (zzimage_session.received != zzimage_session.expected)
        return ZZIMAGE_ERR_INCOMPLETE_INPUT;

    fmt = zzimage_detect_format(zzimage_session.input, zzimage_session.received);
    if (fmt == ZZIMAGE_FMT_UNKNOWN) return ZZIMAGE_ERR_BAD_FORMAT;

    if (!stbi_info_from_memory(zzimage_session.input, (int)zzimage_session.received,
                               &w, &h, &comp))
        return zzimage_heap_oom ? ZZIMAGE_ERR_OOM : ZZIMAGE_ERR_DECODE;
    err = zzimage_validate_dimensions_for_format(fmt, w, h);
    if (err != ZZIMAGE_ERR_NONE) return err;

    zzimage_fit_dimensions((uint32_t)w, (uint32_t)h, max_w, max_h, &out_w, &out_h);
    if (!out_w || !out_h || out_w > (uint32_t)w || out_h > (uint32_t)h)
        return ZZIMAGE_ERR_SCALE;
    if (out_w > UINT32_MAX / out_h || out_w * out_h > UINT32_MAX / 4u)
        return ZZIMAGE_ERR_OVERFLOW;

    if (fmt == ZZIMAGE_FMT_JPEG) {
        uint32_t jpeg_comp = 0u;
        if (!zzimage_jpeg_stream_begin(&zzimage_session.jpeg,
                                       zzimage_session.input,
                                       zzimage_session.received,
                                       out_w, out_h, &jpeg_comp))
            return zzimage_heap_oom ? ZZIMAGE_ERR_OOM : ZZIMAGE_ERR_DECODE;
        comp = (int)jpeg_comp;
        zzimage_session.decode_kind = ZZIMAGE_DECODE_JPEG_STREAM;
    } else {
        decoded = stbi_load_from_memory(zzimage_session.input,
                                        (int)zzimage_session.received,
                                        &w, &h, &comp, 4);
        if (!decoded)
            return zzimage_heap_oom ? ZZIMAGE_ERR_OOM : ZZIMAGE_ERR_DECODE;
        err = zzimage_validate_dimensions_for_format(fmt, w, h);
        if (err != ZZIMAGE_ERR_NONE) {
            stbi_image_free(decoded);
            return err;
        }
        zzimage_session.decoded = decoded;
        zzimage_session.decode_kind = ZZIMAGE_DECODE_PNG_FULL;
    }

    /* Compressed bytes are no longer needed after JPEG entropy decode or
     * after stb_image has produced the PNG RGBA image. */
    free(zzimage_session.input);
    zzimage_session.input = NULL;
    zzimage_session.input_alloc = 0u;

    zzimage_session.source_width = (uint32_t)w;
    zzimage_session.source_height = (uint32_t)h;
    zzimage_session.width = out_w;
    zzimage_session.height = out_h;
    zzimage_session.comp = (uint32_t)comp;
    zzimage_session.format = fmt;
    pixels = out_w * out_h;
    zzimage_session.rgba_bytes = pixels * 4u;
    zzimage_session.next_output_y = 0u;
    zzimage_session.state = ZZIMAGE_SESS_DECODED;

    c->result1         = ZZ_TO_BE32(zzimage_session.width);
    c->result2         = ZZ_TO_BE32(zzimage_session.height);
    c->output_offset   = ZZ_TO_BE32(zzimage_session.source_width);
    c->output_max_size = ZZ_TO_BE32(zzimage_session.source_height);
    c->param2          = ZZ_TO_BE32(zzimage_session.comp);
    c->param3          = ZZ_TO_BE32(zzimage_session.rgba_bytes);
    c->param4          = ZZ_TO_BE32(zzimage_session.format);
    return ZZIMAGE_ERR_NONE;
}

static uint32_t zzimage_do_get_tile(uint8_t *fb, ZZImageXACPCommand *c)
{
    uint32_t y = zzimage_job.p2;
    uint32_t req_rows = zzimage_job.p3;
    uint32_t out_fmt = zzimage_job.p4;
    uint32_t row_bytes, max_rows, rows, bytes;
    uint8_t *out;

    if (!zzimage_session.active || zzimage_session.state != ZZIMAGE_SESS_DECODED)
        return ZZIMAGE_ERR_BAD_STATE;
    if (out_fmt != ZZIMAGE_OUT_RGBA8 && out_fmt != ZZIMAGE_OUT_BGRA8)
        return ZZIMAGE_ERR_BAD_SUBCOMMAND;
    if (y >= zzimage_session.height) return ZZIMAGE_ERR_BOUNDS;
    if (y != zzimage_session.next_output_y) return ZZIMAGE_ERR_SEQUENCE;

    row_bytes = zzimage_session.width * 4u;
    if (row_bytes == 0u || row_bytes > ZZIMAGE_SHARED_SIZE)
        return ZZIMAGE_ERR_OUTPUT_TOO_SMALL;
    max_rows = ZZIMAGE_SHARED_SIZE / row_bytes;
    if (max_rows == 0u) return ZZIMAGE_ERR_OUTPUT_TOO_SMALL;
    rows = req_rows ? req_rows : max_rows;
    if (rows > max_rows) rows = max_rows;
    if (rows > zzimage_session.height - y) rows = zzimage_session.height - y;

    out = fb + ZZIMAGE_SHARED_OFFSET;
    if (zzimage_session.decode_kind == ZZIMAGE_DECODE_JPEG_STREAM) {
        uint32_t produced = zzimage_jpeg_emit_rows(&zzimage_session.jpeg,
                                                   out, row_bytes, rows, out_fmt);
        if (produced != rows)
            return zzimage_heap_oom ? ZZIMAGE_ERR_OOM : ZZIMAGE_ERR_DECODE;
    } else if (zzimage_session.decode_kind == ZZIMAGE_DECODE_PNG_FULL &&
               zzimage_session.decoded) {
        zzimage_copy_pixels_scaled(out, row_bytes, zzimage_session.decoded,
                                   zzimage_session.source_width,
                                   zzimage_session.source_height,
                                   zzimage_session.width,
                                   zzimage_session.height, y, rows, out_fmt);
    } else {
        return ZZIMAGE_ERR_BAD_STATE;
    }

    bytes = rows * row_bytes;
    zzimage_session.next_output_y += rows;
    zzimage_cache_flush(out, bytes);

    /* One-pass tile semantics intentionally match the eventual datatype path.
     * Release expensive decode storage as soon as the last row is exported. */
    if (zzimage_session.next_output_y == zzimage_session.height) {
        if (zzimage_session.decode_kind == ZZIMAGE_DECODE_JPEG_STREAM)
            zzimage_jpeg_stream_release(&zzimage_session.jpeg);
        else if (zzimage_session.decoded) {
            stbi_image_free(zzimage_session.decoded);
            zzimage_session.decoded = NULL;
        }
    }

    c->output_offset   = ZZ_TO_BE32(ZZIMAGE_SHARED_OFFSET);
    c->output_max_size = ZZ_TO_BE32(bytes);
    c->result1         = ZZ_TO_BE32(y);
    c->result2         = ZZ_TO_BE32(rows);
    c->param2          = ZZ_TO_BE32(zzimage_session.width);
    c->param3          = ZZ_TO_BE32(row_bytes);
    c->param4          = ZZ_TO_BE32(out_fmt);
    return ZZIMAGE_ERR_NONE;
}

static uint32_t zzimage_do_direct_p96(uint8_t *fb, ZZImageXACPCommand *c)
{
    uint32_t out_off = zzimage_job.out_off;
    uint32_t out_max = zzimage_job.out_max;
    uint32_t out_fmt = zzimage_job.p2;
    uint32_t pitch = zzimage_job.p3;
    uint32_t footprint, err;
    uint8_t *out;

    if (!zzimage_session.active || zzimage_session.state != ZZIMAGE_SESS_DECODED)
        return ZZIMAGE_ERR_BAD_STATE;
    if (zzimage_session.next_output_y != 0u)
        return ZZIMAGE_ERR_SEQUENCE;
    if (out_fmt != ZZIMAGE_OUT_RGBA8 && out_fmt != ZZIMAGE_OUT_BGRA8)
        return ZZIMAGE_ERR_BAD_SUBCOMMAND;
    if ((out_off & 3u) != 0u || (pitch & 3u) != 0u)
        return ZZIMAGE_ERR_BAD_P96_DEST;

    err = zzimage_output_footprint(zzimage_session.width, zzimage_session.height,
                                   pitch, &footprint);
    if (err != ZZIMAGE_ERR_NONE) return err;
    if (out_max < footprint) return ZZIMAGE_ERR_OUTPUT_TOO_SMALL;
    if (out_off >= ZZIMAGE_P96_END || footprint > ZZIMAGE_P96_END - out_off)
        return ZZIMAGE_ERR_BAD_P96_DEST;

    out = fb + out_off;
    if (zzimage_session.decode_kind == ZZIMAGE_DECODE_JPEG_STREAM) {
        uint32_t produced = zzimage_jpeg_emit_rows(&zzimage_session.jpeg,
                                                   out, pitch,
                                                   zzimage_session.height,
                                                   out_fmt);
        if (produced != zzimage_session.height)
            return zzimage_heap_oom ? ZZIMAGE_ERR_OOM : ZZIMAGE_ERR_DECODE;
        zzimage_jpeg_stream_release(&zzimage_session.jpeg);
    } else if (zzimage_session.decode_kind == ZZIMAGE_DECODE_PNG_FULL &&
               zzimage_session.decoded) {
        zzimage_copy_pixels_scaled(out, pitch, zzimage_session.decoded,
                                   zzimage_session.source_width,
                                   zzimage_session.source_height,
                                   zzimage_session.width,
                                   zzimage_session.height, 0u,
                                   zzimage_session.height, out_fmt);
        stbi_image_free(zzimage_session.decoded);
        zzimage_session.decoded = NULL;
    } else {
        return ZZIMAGE_ERR_BAD_STATE;
    }
    zzimage_session.next_output_y = zzimage_session.height;
    zzimage_cache_flush(out, footprint);

    c->result1 = ZZ_TO_BE32(zzimage_session.width);
    c->result2 = ZZ_TO_BE32(zzimage_session.height);
    c->param2  = ZZ_TO_BE32(zzimage_session.comp);
    c->param3  = ZZ_TO_BE32(zzimage_session.rgba_bytes);
    c->param4  = ZZ_TO_BE32(zzimage_session.format);
    return ZZIMAGE_ERR_NONE;
}

static uint32_t zzimage_do_close(ZZImageXACPCommand *c, int cancel)
{
    uint32_t serial = zzimage_session.serial;
    uint32_t received = zzimage_session.received;
    int clean;
    (void)cancel;

    if (!zzimage_session.active) {
        /* CANCEL is deliberately idempotent; CLOSE without a session is not. */
        if (cancel) {
            c->result1 = ZZ_TO_BE32(zzimage_heap_highwater());
            c->result2 = ZZ_TO_BE32(0u);
            return ZZIMAGE_ERR_NONE;
        }
        return ZZIMAGE_ERR_BAD_STATE;
    }
    clean = zzimage_session_release();
    c->result1 = ZZ_TO_BE32(zzimage_heap_highwater());
    c->result2 = ZZ_TO_BE32(received);
    c->param2  = ZZ_TO_BE32(serial);
    return clean ? ZZIMAGE_ERR_NONE : ZZIMAGE_ERR_ALLOCATOR_STATE;
}

#if ZZIMAGE_ENABLE_LEGACY_DECODE
static uint32_t zzimage_do_legacy_decode(uint8_t *fb, ZZImageXACPCommand *c)
{
    uint8_t *in, *out;
    unsigned char *decoded = NULL;
    int w = 0, h = 0, comp = 0;
    uint32_t fmt = ZZIMAGE_FMT_UNKNOWN, pixels, out_bytes, footprint, row_bytes, pitch;
    uint32_t err;

    if (zzimage_session.active) return ZZIMAGE_ERR_BUSY;
    if (zzimage_job.in_size == 0u || zzimage_job.in_size > ZZIMAGE_MAX_INPUT_BYTES ||
        !zzimage_range_ok(zzimage_job.in_off, zzimage_job.in_size) ||
        zzimage_job.out_max == 0u || !zzimage_range_ok(zzimage_job.out_off, zzimage_job.out_max))
        return ZZIMAGE_ERR_BOUNDS;
    if (zzimage_job.p2 != ZZIMAGE_OUT_RGBA8 && zzimage_job.p2 != ZZIMAGE_OUT_BGRA8)
        return ZZIMAGE_ERR_BAD_SUBCOMMAND;
    if (zzimage_ranges_overlap(zzimage_job.in_off, zzimage_job.in_size,
                               zzimage_job.out_off, zzimage_job.out_max) ||
        zzimage_ranges_overlap(ZZIMAGE_XACP_CMD_OFFSET, 128u,
                               zzimage_job.in_off, zzimage_job.in_size) ||
        zzimage_ranges_overlap(ZZIMAGE_XACP_CMD_OFFSET, 128u,
                               zzimage_job.out_off, zzimage_job.out_max))
        return ZZIMAGE_ERR_OVERLAP;

    in = fb + zzimage_job.in_off;
    out = fb + zzimage_job.out_off;
    zzimage_cache_invalidate(in, zzimage_job.in_size);
    fmt = zzimage_detect_format(in, zzimage_job.in_size);
    if (fmt == ZZIMAGE_FMT_UNKNOWN) return ZZIMAGE_ERR_BAD_FORMAT;
    if (!zzimage_heap_begin()) return ZZIMAGE_ERR_ALLOCATOR_STATE;

    if (!stbi_info_from_memory(in, (int)zzimage_job.in_size, &w, &h, &comp)) {
        err = ZZIMAGE_ERR_DECODE;
        goto fail;
    }
    err = zzimage_validate_dimensions_for_format(fmt, w, h);
    if (err != ZZIMAGE_ERR_NONE) goto fail;
    pixels = (uint32_t)w * (uint32_t)h;
    out_bytes = pixels * 4u;
    row_bytes = (uint32_t)w * 4u;
    pitch = (zzimage_job.p2 == ZZIMAGE_OUT_BGRA8) ? zzimage_job.p3 : row_bytes;
    err = zzimage_output_footprint((uint32_t)w, (uint32_t)h, pitch, &footprint);
    if (err != ZZIMAGE_ERR_NONE) goto fail;
    if (footprint > zzimage_job.out_max) { err = ZZIMAGE_ERR_OUTPUT_TOO_SMALL; goto fail; }

    decoded = stbi_load_from_memory(in, (int)zzimage_job.in_size, &w, &h, &comp, 4);
    if (!decoded) {
        err = zzimage_heap_oom ? ZZIMAGE_ERR_OOM : ZZIMAGE_ERR_DECODE;
        goto fail;
    }
    zzimage_copy_pixels(out, pitch, decoded, (uint32_t)w, (uint32_t)h, zzimage_job.p2);
    stbi_image_free(decoded);
    decoded = NULL;
    if (!zzimage_heap_finish()) return ZZIMAGE_ERR_ALLOCATOR_STATE;
    zzimage_cache_flush(out, footprint);

    c->result1 = ZZ_TO_BE32((uint32_t)w);
    c->result2 = ZZ_TO_BE32((uint32_t)h);
    c->param2  = ZZ_TO_BE32((uint32_t)comp);
    c->param3  = ZZ_TO_BE32(out_bytes);
    c->param4  = ZZ_TO_BE32(fmt);
    return ZZIMAGE_ERR_NONE;

fail:
    if (decoded) stbi_image_free(decoded);
    if (!zzimage_heap_finish() && err == ZZIMAGE_ERR_NONE)
        err = ZZIMAGE_ERR_ALLOCATOR_STATE;
    return err;
}
#endif

/* --------------------------------------------------------------------- */
int zzimage_handle_opcode(void *fb_base, uint32_t *error_out)
{
    uint8_t *fb = (uint8_t *)fb_base;
    ZZImageXACPCommand *c;
    uint32_t subcmd;

    if (error_out) *error_out = ZZIMAGE_ERR_NONE;
    if (!fb) {
        if (error_out) *error_out = ZZIMAGE_ERR_BOUNDS;
        return ZZIMAGE_STATUS_ERROR;
    }

    c = (ZZImageXACPCommand *)(fb + ZZIMAGE_XACP_CMD_OFFSET);
    zzimage_cache_invalidate(c, 128u);
    subcmd = ZZ_FROM_BE32(c->param1);

    if (subcmd == ZZIMAGE_CMD_QUERY) {
        c->input_offset    = ZZ_TO_BE32(ZZIMAGE_SHARED_OFFSET);
        c->input_size      = ZZ_TO_BE32(ZZIMAGE_SHARED_SIZE);
        c->output_offset   = ZZ_TO_BE32(ZZIMAGE_SHARED_OFFSET);
        c->output_max_size = ZZ_TO_BE32(ZZIMAGE_MAX_INPUT_BYTES);
        c->result1 = ZZ_TO_BE32(ZZIMAGE_VERSION);
        c->result2 = ZZ_TO_BE32(ZZIMAGE_CAP_JPEG | ZZIMAGE_CAP_PNG |
                                ZZIMAGE_CAP_RGBA8 | ZZIMAGE_CAP_BGRA8 |
                                ZZIMAGE_CAP_SESSION | ZZIMAGE_CAP_CHUNK_FEED |
                                ZZIMAGE_CAP_TILED_OUTPUT | ZZIMAGE_CAP_DIRECT_P96 |
                                ZZIMAGE_CAP_CANCEL | ZZIMAGE_CAP_SCALE_FIT |
                                ZZIMAGE_CAP_JPEG_ROW_STREAM);
        c->param2 = ZZ_TO_BE32(ZZIMAGE_MAX_WIDTH);
        c->param3 = ZZ_TO_BE32(ZZIMAGE_MAX_HEIGHT);
        c->param4 = ZZ_TO_BE32(ZZIMAGE_MAX_PIXELS);
        zzimage_publish(c, ZZIMAGE_STATUS_DONE, ZZIMAGE_ERR_NONE);
        return ZZIMAGE_STATUS_DONE;
    }

    if (subcmd != ZZIMAGE_CMD_DECODE_LEGACY && subcmd != ZZIMAGE_CMD_BEGIN &&
        subcmd != ZZIMAGE_CMD_FEED && subcmd != ZZIMAGE_CMD_DECODE &&
        subcmd != ZZIMAGE_CMD_GET_TILE && subcmd != ZZIMAGE_CMD_DIRECT_P96 &&
        subcmd != ZZIMAGE_CMD_CLOSE && subcmd != ZZIMAGE_CMD_CANCEL) {
        if (error_out) *error_out = ZZIMAGE_ERR_BAD_SUBCOMMAND;
        zzimage_publish(c, ZZIMAGE_STATUS_ERROR, ZZIMAGE_ERR_BAD_SUBCOMMAND);
        return ZZIMAGE_STATUS_ERROR;
    }

#if !ZZIMAGE_ENABLE_DECODE
    if (error_out) *error_out = ZZIMAGE_ERR_DISABLED;
    zzimage_publish(c, ZZIMAGE_STATUS_ERROR, ZZIMAGE_ERR_DISABLED);
    return ZZIMAGE_STATUS_ERROR;
#else
    if (zzimage_job.pending) {
        if (error_out) *error_out = ZZIMAGE_ERR_BUSY;
        zzimage_publish(c, ZZIMAGE_STATUS_ERROR, ZZIMAGE_ERR_BUSY);
        return ZZIMAGE_STATUS_ERROR;
    }

    zzimage_job.subcmd  = subcmd;
    zzimage_job.in_off  = ZZ_FROM_BE32(c->input_offset);
    zzimage_job.in_size = ZZ_FROM_BE32(c->input_size);
    zzimage_job.out_off = ZZ_FROM_BE32(c->output_offset);
    zzimage_job.out_max = ZZ_FROM_BE32(c->output_max_size);
    zzimage_job.p2      = ZZ_FROM_BE32(c->param2);
    zzimage_job.p3      = ZZ_FROM_BE32(c->param3);
    zzimage_job.p4      = ZZ_FROM_BE32(c->param4);
    zzimage_job.pending = 1u;
    zzimage_publish(c, ZZIMAGE_STATUS_BUSY, ZZIMAGE_ERR_NONE);
    return ZZIMAGE_STATUS_BUSY;
#endif
}

int zzimage_idle_poll(void *fb_base, uint32_t *error_out)
{
#if !ZZIMAGE_ENABLE_DECODE
    (void)fb_base;
    if (error_out) *error_out = ZZIMAGE_ERR_NONE;
    return ZZIMAGE_STATUS_IDLE;
#else
    uint8_t *fb = (uint8_t *)fb_base;
    ZZImageXACPCommand *c;
    uint32_t err = ZZIMAGE_ERR_NONE;
    uint32_t subcmd;
    int fatal_session_error = 0;

    if (error_out) *error_out = ZZIMAGE_ERR_NONE;
    if (!zzimage_job.pending) return ZZIMAGE_STATUS_IDLE;
    zzimage_job.pending = 0u;
    if (!fb) {
        if (error_out) *error_out = ZZIMAGE_ERR_BOUNDS;
        return ZZIMAGE_STATUS_ERROR;
    }

    c = (ZZImageXACPCommand *)(fb + ZZIMAGE_XACP_CMD_OFFSET);
    subcmd = zzimage_job.subcmd;
    zzimage_zero_result_fields(c);

    switch (subcmd) {
        case ZZIMAGE_CMD_BEGIN:
            err = zzimage_do_begin(c);
            break;
        case ZZIMAGE_CMD_FEED:
            err = zzimage_do_feed(fb, c);
            break;
        case ZZIMAGE_CMD_DECODE:
            err = zzimage_do_session_decode(c);
            if (err != ZZIMAGE_ERR_NONE) fatal_session_error = 1;
            break;
        case ZZIMAGE_CMD_GET_TILE:
            err = zzimage_do_get_tile(fb, c);
            break;
        case ZZIMAGE_CMD_DIRECT_P96:
            err = zzimage_do_direct_p96(fb, c);
            break;
        case ZZIMAGE_CMD_CLOSE:
            err = zzimage_do_close(c, 0);
            break;
        case ZZIMAGE_CMD_CANCEL:
            err = zzimage_do_close(c, 1);
            break;
        case ZZIMAGE_CMD_DECODE_LEGACY:
#if ZZIMAGE_ENABLE_LEGACY_DECODE
            err = zzimage_do_legacy_decode(fb, c);
#else
            err = ZZIMAGE_ERR_DISABLED;
#endif
            break;
        default:
            err = ZZIMAGE_ERR_BAD_SUBCOMMAND;
            break;
    }

    if (fatal_session_error && zzimage_session.active) {
        int clean = zzimage_session_release();
        if (!clean && err == ZZIMAGE_ERR_NONE)
            err = ZZIMAGE_ERR_ALLOCATOR_STATE;
    }

    if (err != ZZIMAGE_ERR_NONE) {
        zzimage_publish(c, ZZIMAGE_STATUS_ERROR, err);
        if (error_out) *error_out = err;
        return ZZIMAGE_STATUS_ERROR;
    }

    zzimage_publish(c, ZZIMAGE_STATUS_DONE, ZZIMAGE_ERR_NONE);
    return ZZIMAGE_STATUS_DONE;
#endif
}
