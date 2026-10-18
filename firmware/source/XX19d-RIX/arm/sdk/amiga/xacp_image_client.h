/*
 * Minimal 68k client for XACP Image Service 1.3.
 * Intended to stay usable from SAS/C, VBCC and GCC.
 *
 * SPDX-License-Identifier: 0BSD
 *
 * This development header has not yet been validated with SAS/C itself.
 */
#ifndef XACP_IMAGE_CLIENT_H
#define XACP_IMAGE_CLIENT_H

#include <exec/types.h>
#include "xacp_image_service.h"

#define XACP_IMAGE_CLIENT_OK          0
#define XACP_IMAGE_CLIENT_ERROR      -1
#define XACP_IMAGE_CLIENT_TIMEOUT    -2
#define XACP_IMAGE_CLIENT_BREAK      -3

struct XACPImageInfo {
    ULONG source_width;
    ULONG source_height;
    ULONG width;
    ULONG height;
    ULONG source_components;
    ULONG rgba_bytes;
    ULONG format;
};

struct XACPImageClient {
    volatile UBYTE *board;
    volatile UBYTE *fb;
    volatile XACPImageCommand *cmd;
    ULONG shared_offset;
    ULONG shared_size;
    ULONG capabilities;
    ULONG version;
    ULONG last_error;
};

int XACPImage_Init(struct XACPImageClient *cl, APTR zz9000_board_base);
int XACPImage_Query(struct XACPImageClient *cl);
int XACPImage_Begin(struct XACPImageClient *cl, ULONG compressed_size);
int XACPImage_Feed(struct XACPImageClient *cl, const void *data, ULONG bytes,
                   ULONG stream_offset, int final_chunk);
int XACPImage_Decode(struct XACPImageClient *cl, struct XACPImageInfo *info);
int XACPImage_DecodeFit(struct XACPImageClient *cl, struct XACPImageInfo *info,
                        ULONG max_width, ULONG max_height);
int XACPImage_GetTile(struct XACPImageClient *cl, ULONG y, ULONG requested_rows,
                      ULONG out_format, APTR *pixels, ULONG *bytes,
                      ULONG *rows, ULONG *row_bytes);
int XACPImage_DirectP96(struct XACPImageClient *cl, ULONG fb_offset,
                        ULONG capacity, ULONG pitch, ULONG out_format);
int XACPImage_Close(struct XACPImageClient *cl, ULONG *decoder_heap_highwater);
int XACPImage_Cancel(struct XACPImageClient *cl);

#endif
