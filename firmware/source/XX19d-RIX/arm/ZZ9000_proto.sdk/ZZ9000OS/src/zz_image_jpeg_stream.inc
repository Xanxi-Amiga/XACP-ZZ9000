/*
 * zz_image_jpeg_stream.inc - private JPEG row-output adapter for stb_image
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * This file is intentionally included by zz_image.c AFTER
 * STB_IMAGE_IMPLEMENTATION.  It uses stb_image's private JPEG structures to
 * keep its well-tested entropy/IDCT/resampling code while avoiding the final
 * full-frame RGBA allocation.  That is the key to handling camera-sized JPEGs
 * safely in the bounded Core0 heap.
 *
 * JPEG entropy decode still completes before rows are exported.  Baseline
 * JPEG therefore holds the component planes only.  Progressive JPEG also
 * needs coefficient planes during scan decode; the normal 72 MiB decoder
 * quota remains the hard safety ceiling and can reject unusually expensive
 * progressive layouts cleanly.
 */

#ifndef ZZ_IMAGE_JPEG_STREAM_INC
#define ZZ_IMAGE_JPEG_STREAM_INC

typedef struct ZZImageJPEGStream {
    stbi__context ctx;
    stbi__jpeg *jpeg;
    stbi__resample res_comp[4];
    stbi_uc *row_rgba;
    uint32_t source_width;
    uint32_t source_height;
    uint32_t output_width;
    uint32_t output_height;
    uint32_t source_y;
    uint32_t output_y;
    int decode_n;
    int is_rgb;
    uint8_t progressive;
    uint8_t ready;
} ZZImageJPEGStream;

static void zzimage_jpeg_stream_zero(ZZImageJPEGStream *s)
{
    if (s) memset(s, 0, sizeof(*s));
}

static void zzimage_jpeg_stream_release(ZZImageJPEGStream *s)
{
    if (!s) return;
    if (s->row_rgba) {
        zzimage_free(s->row_rgba);
        s->row_rgba = NULL;
    }
    if (s->jpeg) {
        stbi__cleanup_jpeg(s->jpeg);
        zzimage_free(s->jpeg);
        s->jpeg = NULL;
    }
    zzimage_jpeg_stream_zero(s);
}

static int zzimage_jpeg_stream_begin(ZZImageJPEGStream *s,
                                     const uint8_t *data, uint32_t bytes,
                                     uint32_t output_width,
                                     uint32_t output_height,
                                     uint32_t *source_comp)
{
    stbi__jpeg *z;
    int k;

    if (!s || !data || bytes == 0u || bytes > 0x7fffffffu ||
        output_width == 0u || output_height == 0u)
        return 0;

    zzimage_jpeg_stream_zero(s);
    z = (stbi__jpeg *)zzimage_malloc(sizeof(*z));
    if (!z) return 0;
    memset(z, 0, sizeof(*z));
    s->jpeg = z;

    stbi__start_mem(&s->ctx, data, (int)bytes);
    z->s = &s->ctx;
    stbi__setup_jpeg(z);

    if (!stbi__decode_jpeg_image(z)) {
        zzimage_jpeg_stream_release(s);
        return 0;
    }

    s->source_width = z->s->img_x;
    s->source_height = z->s->img_y;
    s->output_width = output_width;
    s->output_height = output_height;
    s->progressive = z->progressive ? 1u : 0u;
    s->is_rgb = z->s->img_n == 3 &&
                (z->rgb == 3 || (z->app14_color_transform == 0 && !z->jfif));

    /* req_comp is always RGBA8 for the XACP image service. */
    s->decode_n = z->s->img_n;
    if (s->decode_n <= 0 || s->decode_n > 4) {
        zzimage_jpeg_stream_release(s);
        return 0;
    }

    /* Progressive coefficients are no longer needed after
     * stbi__decode_jpeg_image() called stbi__jpeg_finish().  Release them
     * before output begins so only the sample planes stay resident. */
    for (k = 0; k < z->s->img_n; ++k) {
        if (z->img_comp[k].raw_coeff) {
            zzimage_free(z->img_comp[k].raw_coeff);
            z->img_comp[k].raw_coeff = NULL;
            z->img_comp[k].coeff = NULL;
        }
    }

    for (k = 0; k < s->decode_n; ++k) {
        stbi__resample *r = &s->res_comp[k];
        z->img_comp[k].linebuf =
            (stbi_uc *)zzimage_malloc((size_t)z->s->img_x + 3u);
        if (!z->img_comp[k].linebuf) {
            zzimage_jpeg_stream_release(s);
            return 0;
        }

        r->hs      = z->img_h_max / z->img_comp[k].h;
        r->vs      = z->img_v_max / z->img_comp[k].v;
        r->ystep   = r->vs >> 1;
        r->w_lores = (z->s->img_x + r->hs - 1) / r->hs;
        r->ypos    = 0;
        r->line0   = r->line1 = z->img_comp[k].data;

        if      (r->hs == 1 && r->vs == 1) r->resample = resample_row_1;
        else if (r->hs == 1 && r->vs == 2) r->resample = stbi__resample_row_v_2;
        else if (r->hs == 2 && r->vs == 1) r->resample = stbi__resample_row_h_2;
        else if (r->hs == 2 && r->vs == 2) r->resample = z->resample_row_hv_2_kernel;
        else                               r->resample = stbi__resample_row_generic;
    }

    if (z->s->img_x > (int)(UINT32_MAX / 4u)) {
        zzimage_jpeg_stream_release(s);
        return 0;
    }
    s->row_rgba = (stbi_uc *)zzimage_malloc((size_t)z->s->img_x * 4u);
    if (!s->row_rgba) {
        zzimage_jpeg_stream_release(s);
        return 0;
    }

    if (source_comp)
        *source_comp = z->s->img_n >= 3 ? 3u : 1u;
    s->ready = 1u;
    return 1;
}

/* Render the next full-resolution RGBA source row, preserving stb_image's
 * normal color conversion and upsampling behavior exactly. */
static int zzimage_jpeg_render_source_row(ZZImageJPEGStream *s)
{
    stbi__jpeg *z;
    stbi_uc *coutput[4] = { NULL, NULL, NULL, NULL };
    stbi_uc *out;
    stbi_uc *y;
    uint32_t i;
    int k;

    if (!s || !s->ready || !s->jpeg || !s->row_rgba ||
        s->source_y >= s->source_height)
        return 0;

    z = s->jpeg;
    out = s->row_rgba;

    for (k = 0; k < s->decode_n; ++k) {
        stbi__resample *r = &s->res_comp[k];
        int y_bot = r->ystep >= (r->vs >> 1);
        coutput[k] = r->resample(z->img_comp[k].linebuf,
                                 y_bot ? r->line1 : r->line0,
                                 y_bot ? r->line0 : r->line1,
                                 r->w_lores, r->hs);
        if (++r->ystep >= r->vs) {
            r->ystep = 0;
            r->line0 = r->line1;
            if (++r->ypos < z->img_comp[k].y)
                r->line1 += z->img_comp[k].w2;
        }
    }

    y = coutput[0];
    if (z->s->img_n == 3) {
        if (s->is_rgb) {
            for (i = 0u; i < s->source_width; ++i) {
                out[0] = y[i];
                out[1] = coutput[1][i];
                out[2] = coutput[2][i];
                out[3] = 255u;
                out += 4;
            }
        } else {
            z->YCbCr_to_RGB_kernel(out, y, coutput[1], coutput[2],
                                    (int)s->source_width, 4);
        }
    } else if (z->s->img_n == 4) {
        if (z->app14_color_transform == 0) { /* CMYK */
            for (i = 0u; i < s->source_width; ++i) {
                stbi_uc m = coutput[3][i];
                out[0] = stbi__blinn_8x8(coutput[0][i], m);
                out[1] = stbi__blinn_8x8(coutput[1][i], m);
                out[2] = stbi__blinn_8x8(coutput[2][i], m);
                out[3] = 255u;
                out += 4;
            }
        } else if (z->app14_color_transform == 2) { /* YCCK */
            z->YCbCr_to_RGB_kernel(out, y, coutput[1], coutput[2],
                                    (int)s->source_width, 4);
            out = s->row_rgba;
            for (i = 0u; i < s->source_width; ++i) {
                stbi_uc m = coutput[3][i];
                out[0] = stbi__blinn_8x8((stbi_uc)(255u - out[0]), m);
                out[1] = stbi__blinn_8x8((stbi_uc)(255u - out[1]), m);
                out[2] = stbi__blinn_8x8((stbi_uc)(255u - out[2]), m);
                out += 4;
            }
        } else { /* YCbCr + fourth channel ignored, stock stb behavior */
            z->YCbCr_to_RGB_kernel(out, y, coutput[1], coutput[2],
                                    (int)s->source_width, 4);
        }
    } else {
        for (i = 0u; i < s->source_width; ++i) {
            out[0] = out[1] = out[2] = y[i];
            out[3] = 255u;
            out += 4;
        }
    }

    s->source_y++;
    return 1;
}

static void zzimage_jpeg_write_scaled_row(uint8_t *dst,
                                          const uint8_t *src_rgba,
                                          uint32_t source_width,
                                          uint32_t output_width,
                                          uint32_t out_fmt)
{
    uint32_t x;
    for (x = 0u; x < output_width; ++x) {
        uint32_t sx = (uint32_t)(((uint64_t)x * source_width) / output_width);
        const uint8_t *s = src_rgba + sx * 4u;
        if (out_fmt == ZZIMAGE_OUT_BGRA8) {
            dst[0] = s[2]; dst[1] = s[1]; dst[2] = s[0]; dst[3] = s[3];
        } else {
            dst[0] = s[0]; dst[1] = s[1]; dst[2] = s[2]; dst[3] = s[3];
        }
        dst += 4;
    }
}

/* Emit the next sequential output rows.  Scaling is nearest-neighbor and
 * downscale-only; this keeps the implementation deterministic and tiny while
 * making multi-megapixel camera images viewable on practical Amiga RTG modes.
 */
static uint32_t zzimage_jpeg_emit_rows(ZZImageJPEGStream *s,
                                       uint8_t *dst, uint32_t dst_pitch,
                                       uint32_t max_rows, uint32_t out_fmt)
{
    uint32_t done = 0u;

    if (!s || !s->ready || !dst || max_rows == 0u ||
        (out_fmt != ZZIMAGE_OUT_RGBA8 && out_fmt != ZZIMAGE_OUT_BGRA8))
        return 0u;

    while (done < max_rows && s->output_y < s->output_height) {
        uint32_t target_sy = (uint32_t)(((uint64_t)s->output_y *
                                         s->source_height) /
                                        s->output_height);
        while (s->source_y <= target_sy) {
            if (!zzimage_jpeg_render_source_row(s))
                return done;
        }
        zzimage_jpeg_write_scaled_row(dst + done * dst_pitch,
                                      s->row_rgba,
                                      s->source_width,
                                      s->output_width,
                                      out_fmt);
        s->output_y++;
        done++;
    }
    return done;
}

#endif /* ZZ_IMAGE_JPEG_STREAM_INC */
