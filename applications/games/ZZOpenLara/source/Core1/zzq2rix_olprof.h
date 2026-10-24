/* ZZOpenLara A2.7: one frame of the RasterIX backend (Core1/zzq2rix_gpu.c), for the report of
 * Core1/ol_game.cpp (olstat.txt). Filled at the end of zz_rix_end_frame; seq counts the frames.
 * All times in microseconds. ASCII only.
 */
#ifndef ZZQ2RIX_OLPROF_H
#define ZZQ2RIX_OLPROF_H
typedef struct {
    unsigned seq;
    unsigned wall_us;        /* frame period, end of one frame to the end of the next */
    unsigned ref_us;         /* zz_rix_begin_frame -> zz_rix_end_frame */
    unsigned swap_us;        /* closing the frame (zzgl_swap) */
    unsigned gpu_us;         /* frame fence wait (RIXOVERLAP=0 or RIXSYNC=1 only) */
    unsigned present_us;     /* presentation (RIXOVERLAP=1: of frame N-1, its fence wait included) */
    unsigned pwait_us;       /* RIXOVERLAP=1: of present_us, the fence wait of frame N-1 */
    unsigned pcopy_us;       /* RIXOVERLAP=1: of present_us, the RGB565 -> P96 copy */
    unsigned flush_us;       /* zzglBegin: state + binds */
    unsigned bind_us;        /* of flush_us: binds */
    unsigned end_us;         /* zzglEnd: triangles -> descriptors */
    unsigned upload_us;      /* texture uploads */
    unsigned tris_in, tris_out, verts, begins, binds, culled, clipped;
    unsigned ringwait_us, bwait_us, sync_gpu_us;
    unsigned batch_us;       /* all batches (zzglBegin .. zzglEnd, the DIP code between them included) */
} zz_ol_frame;
const zz_ol_frame *zz_rix_last_frame(void);
#endif
