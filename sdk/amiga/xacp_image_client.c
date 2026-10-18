/* Minimal 68k transport implementation for XACP Image Service 1.3.
 * SPDX-License-Identifier: 0BSD
 */
#include <exec/types.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <string.h>
#include "xacp_image_client.h"
#include "xacp_memory_map_v1_8.h"

#define MNT_FB_BASE     0x00010000UL
#define REG_CMD         0x0064UL
#define XACP_CMD_OFFSET 0x04000000UL
#ifndef CACRF_ClearD
#define CACRF_ClearD    (1L << 2)
#endif
#define ZZ_WR(b,o,v) (*((volatile UWORD *)((UBYTE *)(b) + (o))) = (UWORD)(v))

static void ximg_flush(APTR p, ULONG n)
{
    if (n) CacheClearE(p, n, CACRF_ClearD);
}

static void ximg_clear(struct XACPImageClient *cl)
{
    memset((void *)cl->cmd, 0, sizeof(*cl->cmd));
    ximg_flush((APTR)cl->cmd, 128UL);
}

static int ximg_wait(struct XACPImageClient *cl, ULONG ticks)
{
    ULONG n;
    ULONG st;
    for (n = 0; n < ticks; ++n) {
        ximg_flush((APTR)cl->cmd, 128UL);
        st = cl->cmd->status;
        if (st == XACP_IMAGE_STATUS_DONE) return XACP_IMAGE_CLIENT_OK;
        if (st == XACP_IMAGE_STATUS_ERROR) {
            cl->last_error = cl->cmd->error;
            return XACP_IMAGE_CLIENT_ERROR;
        }
        if (SetSignal(0L, 0L) & SIGBREAKF_CTRL_C) return XACP_IMAGE_CLIENT_BREAK;
        Delay(1);
    }
    return XACP_IMAGE_CLIENT_TIMEOUT;
}

static int ximg_issue(struct XACPImageClient *cl, ULONG subcmd, ULONG ticks)
{
    int rc;
    cl->cmd->opcode = XACP_IMAGE_OPCODE;
    cl->cmd->param1 = subcmd;
    cl->cmd->status = XACP_IMAGE_STATUS_IDLE;
    cl->cmd->error = 0;
    cl->last_error = 0;
    ximg_flush((APTR)cl->cmd, 128UL);
    ZZ_WR(cl->board, REG_CMD, XACP_IMAGE_OPCODE);
    rc = ximg_wait(cl, ticks);
    return rc;
}

int XACPImage_Init(struct XACPImageClient *cl, APTR zz9000_board_base)
{
    if (!cl || !zz9000_board_base) return XACP_IMAGE_CLIENT_ERROR;
    memset(cl, 0, sizeof(*cl));
    cl->board = (volatile UBYTE *)zz9000_board_base;
    cl->fb = cl->board + MNT_FB_BASE;
    cl->cmd = (volatile XACPImageCommand *)(cl->fb + XACP_CMD_OFFSET);
    return XACPImage_Query(cl);
}

int XACPImage_Query(struct XACPImageClient *cl)
{
    int rc;
    if (!cl || !cl->cmd) return XACP_IMAGE_CLIENT_ERROR;
    ximg_clear(cl);
    rc = ximg_issue(cl, XACP_IMAGE_CMD_QUERY, 250UL);
    if (rc != XACP_IMAGE_CLIENT_OK) return rc;
    cl->version = cl->cmd->result1;
    cl->capabilities = cl->cmd->result2;
    cl->shared_offset = cl->cmd->input_offset;
    cl->shared_size = cl->cmd->input_size;
    if (cl->shared_offset != XACP_IMAGE_SHARED_OFFSET ||
        cl->shared_size != XACP_IMAGE_SHARED_SIZE)
        return XACP_IMAGE_CLIENT_ERROR;
    return XACP_IMAGE_CLIENT_OK;
}

int XACPImage_Begin(struct XACPImageClient *cl, ULONG compressed_size)
{
    if (!cl || !cl->cmd) return XACP_IMAGE_CLIENT_ERROR;
    ximg_clear(cl);
    cl->cmd->input_size = compressed_size;
    return ximg_issue(cl, XACP_IMAGE_CMD_BEGIN, 250UL);
}

int XACPImage_Feed(struct XACPImageClient *cl, const void *data, ULONG bytes,
                   ULONG stream_offset, int final_chunk)
{
    if (!cl || !data || !bytes || bytes > cl->shared_size)
        return XACP_IMAGE_CLIENT_ERROR;
    CopyMem((APTR)data, (APTR)(cl->fb + cl->shared_offset), bytes);
    ximg_flush((APTR)(cl->fb + cl->shared_offset), bytes);
    ximg_clear(cl);
    cl->cmd->input_offset = cl->shared_offset;
    cl->cmd->input_size = bytes;
    cl->cmd->param2 = stream_offset;
    cl->cmd->param3 = final_chunk ? XACP_IMAGE_FEED_FINAL : 0UL;
    return ximg_issue(cl, XACP_IMAGE_CMD_FEED, 250UL);
}

int XACPImage_DecodeFit(struct XACPImageClient *cl, struct XACPImageInfo *info,
                        ULONG max_width, ULONG max_height)
{
    int rc;
    if (!cl || !info) return XACP_IMAGE_CLIENT_ERROR;
    ximg_clear(cl);
    cl->cmd->param2 = max_width;
    cl->cmd->param3 = max_height;
    rc = ximg_issue(cl, XACP_IMAGE_CMD_DECODE, 6000UL);
    if (rc != XACP_IMAGE_CLIENT_OK) return rc;
    info->width = cl->cmd->result1;
    info->height = cl->cmd->result2;
    info->source_width = cl->cmd->output_offset;
    info->source_height = cl->cmd->output_max_size;
    info->source_components = cl->cmd->param2;
    info->rgba_bytes = cl->cmd->param3;
    info->format = cl->cmd->param4;
    return XACP_IMAGE_CLIENT_OK;
}

int XACPImage_Decode(struct XACPImageClient *cl, struct XACPImageInfo *info)
{
    return XACPImage_DecodeFit(cl, info, 0UL, 0UL);
}

int XACPImage_GetTile(struct XACPImageClient *cl, ULONG y, ULONG requested_rows,
                      ULONG out_format, APTR *pixels, ULONG *bytes,
                      ULONG *rows, ULONG *row_bytes)
{
    int rc;
    if (!cl || !pixels || !bytes || !rows || !row_bytes)
        return XACP_IMAGE_CLIENT_ERROR;
    ximg_clear(cl);
    cl->cmd->param2 = y;
    cl->cmd->param3 = requested_rows;
    cl->cmd->param4 = out_format;
    rc = ximg_issue(cl, XACP_IMAGE_CMD_GET_TILE, 1000UL);
    if (rc != XACP_IMAGE_CLIENT_OK) return rc;
    *bytes = cl->cmd->output_max_size;
    *rows = cl->cmd->result2;
    *row_bytes = cl->cmd->param3;
    *pixels = (APTR)(cl->fb + cl->cmd->output_offset);
    ximg_flush(*pixels, *bytes);
    return XACP_IMAGE_CLIENT_OK;
}

int XACPImage_DirectP96(struct XACPImageClient *cl, ULONG fb_offset,
                        ULONG capacity, ULONG pitch, ULONG out_format)
{
    if (!cl) return XACP_IMAGE_CLIENT_ERROR;
    ximg_clear(cl);
    cl->cmd->output_offset = fb_offset;
    cl->cmd->output_max_size = capacity;
    cl->cmd->param2 = out_format;
    cl->cmd->param3 = pitch;
    return ximg_issue(cl, XACP_IMAGE_CMD_DIRECT_P96, 3000UL);
}

int XACPImage_Close(struct XACPImageClient *cl, ULONG *decoder_heap_highwater)
{
    int rc;
    if (!cl) return XACP_IMAGE_CLIENT_ERROR;
    ximg_clear(cl);
    rc = ximg_issue(cl, XACP_IMAGE_CMD_CLOSE, 250UL);
    if (rc == XACP_IMAGE_CLIENT_OK && decoder_heap_highwater)
        *decoder_heap_highwater = cl->cmd->result1;
    return rc;
}

int XACPImage_Cancel(struct XACPImageClient *cl)
{
    if (!cl) return XACP_IMAGE_CLIENT_ERROR;
    ximg_clear(cl);
    return ximg_issue(cl, XACP_IMAGE_CMD_CANCEL, 250UL);
}
