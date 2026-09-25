/* Picasso96 presentation backend for the ZZ9000 Core1 renderer. */
 



















#include <TFE_RenderBackend/renderBackend.h>
#include <cstring>
#include <cstdio>

extern "C" {
#include "zzdf_config.h"
#include "zzdf_present_geom.h"
    extern void dcache_clean_range(unsigned int start, unsigned int len);
    extern void zzdf_log_puts(const char *s);
}

static volatile u32* s_sh = (volatile u32*)ZZDF_SHARED_ARM;

 










static u32 s_engineFrames = 0;
static u32 s_presentSeq   = 0;
static u32 s_presents     = 0;
static u32 s_dropped      = 0;
static int s_dirty        = 0;

 









extern "C" u32 zzdf_us_now(void);
static u32 s_convUsMax     = 0;
static u32 s_readyLastUs   = 0;
static u32 s_readyGapSum   = 0;
static u32 s_readyGapN     = 0;
static u32 s_readyGapMax   = 0;
static u32 s_dropFirstUs   = 0;   /* first drop since the last present */
static int s_dropPending   = 0;
static u32 s_lastRenderSeq = 0;   /* SH_RENDER_SEQ we last drew into   */
static u32 s_lastFb        = 0;   /* address we last converted into    */
static u32 s_lastFbLen     = 0;

/* The park path must clean the buffer we actually drew into last. In
   triple-buffer mode SH_FB_ADDR is only the initial MMU anchor and is
   NOT the current target, so cleaning it would flush the wrong (often
   the displayed) buffer. */
extern "C" void zzdf_rb_clean_last(void)
{
	if (s_lastFb && s_lastFbLen) dcache_clean_range(s_lastFb, s_lastFbLen);
}

/* Has the launcher published a render target we have not drawn into
   yet? This is what the pacer waits on: one engine frame per target
   means one engine frame per displayed frame. Returns 1 in double
   buffer fallback (SH_RENDER_SEQ == 0) so the pacer never blocks a
   configuration it does not drive. */
extern "C" int zzdf_rb_target_ready(void)
{
	u32 rseq = s_sh[SH_RENDER_SEQ];
	if (rseq == 0) return 1;
	return (rseq != s_lastRenderSeq) ? 1 : 0;
}
static u32 s_flipWaitSum   = 0;
static u32 s_flipWaitN     = 0;

extern "C" u32 zzdf_rb_engine_frames(void) { return s_engineFrames; }
extern "C" u32 zzdf_rb_presents(void)      { return s_presents; }
extern "C" u32 zzdf_rb_dropped(void)       { return s_dropped; }

namespace TFE_RenderBackend
{
	static u32 s_vdispWidth = 0, s_vdispHeight = 0;
	static u32 s_vdispFlags = 0;
	static const u8* s_indexed = nullptr;   /* captured CPU buffer */
	static u32 s_palette[256];               /* LUT, ZZ 32-bit order */

	bool createVirtualDisplay(const VirtualDisplayInfo& vdispInfo)
	{
		s_vdispWidth = vdispInfo.width;
		s_vdispHeight = vdispInfo.height;
		s_vdispFlags = vdispInfo.flags;
		 








		s_indexed = nullptr;
		s_dirty = 0;
		return true;
	}

	void updateVirtualDisplay(const void* buffer, size_t size)
	{
		(void)size;
		s_indexed = (const u8*)buffer;
		s_dirty = 1;
		s_engineFrames++;
		s_sh[SH_FRAME]          = s_engineFrames;  /* historic slot   */
		s_sh[SH_RENDER_FRAMES]  = s_engineFrames;  /* explicit RENDER */
	}

	void bindVirtualDisplay() {}

	void clearVirtualDisplay(f32* color, bool clearColor)
	{
		(void)color; (void)clearColor;
	}

	/* TFE palette entries are R | G<<8 | B<<16 | A<<24. The ZZ9000
	   32-bit mode, as validated by ZZQuake (QG_SetPalette), wants the
	   little-endian u32 value R<<16 | G<<8 | B (XRGB8888). Build the
	   LUT in that order once per palette change, not per pixel. */
	void setPalette(const u32* palette)
	{
		if (!palette) return;
		for (u32 i = 0; i < 256; i++)
		{
			u32 p = palette[i];
			u32 r = p & 0xFFu, g = (p >> 8) & 0xFFu, b = (p >> 16) & 0xFFu;
			s_palette[i] = (r << 16) | (g << 8) | b;
		}
	}
	const u32* getPalette() { return s_palette; }

	bool getWidescreen()
	{
		return (s_vdispFlags & VDISP_WIDESCREEN) != 0;
	}

	void getDisplayInfo(DisplayInfo* displayInfo)
	{
		/* The DISPLAY is the physical screen, not the game buffer.
		   Upstream derives its widescreen aspect from this, so
		   reporting the game size here would feed that computation
		   the wrong ratio. With widescreen disabled it is only a
		   report, but it should still be true. */
		u32 sw = s_sh[SH_SCREEN_W], sh_ = s_sh[SH_SCREEN_H];
		displayInfo->width  = sw ? sw : (s_sh[SH_FB_WIDTH]  ? s_sh[SH_FB_WIDTH]  : s_vdispWidth);
		displayInfo->height = sh_ ? sh_ : (s_sh[SH_FB_HEIGHT] ? s_sh[SH_FB_HEIGHT] : s_vdispHeight);
		displayInfo->refreshRate = 60.0f;
	}
	void getCurrentMonitorInfo(MonitorInfo* monitorInfo)
	{
		monitorInfo->x = 0; monitorInfo->y = 0;
		monitorInfo->w = (s32)(s_sh[SH_SCREEN_W] ? s_sh[SH_SCREEN_W] :
		                       (s_sh[SH_FB_WIDTH]  ? s_sh[SH_FB_WIDTH]  : s_vdispWidth));
		monitorInfo->h = (s32)(s_sh[SH_SCREEN_H] ? s_sh[SH_SCREEN_H] :
		                       (s_sh[SH_FB_HEIGHT] ? s_sh[SH_FB_HEIGHT] : s_vdispHeight));
	}

	/* --------- the present: 8 -> 32 with palette LUT ---------- */
	void swap(bool blitVirtualDisplay)
	{
		if (!blitVirtualDisplay || !s_dirty || !s_indexed) return;
		s_dirty = 0;                       /* this engine frame is consumed */

		u32 pitch = s_sh[SH_FB_PITCH];
		u32 outW  = s_sh[SH_FB_WIDTH];
		u32 outH  = s_sh[SH_FB_HEIGHT];
		u32 rseq  = s_sh[SH_RENDER_SEQ];
		u32 fb;

		if (rseq)
		{
			 








			if (rseq == s_lastRenderSeq)
			{
				if (!s_dropPending) { s_dropFirstUs = zzdf_us_now(); s_dropPending = 1; }
				s_dropped++;
				s_sh[SH_PRESENT_DROPPED] = s_dropped;
				return;
			}
			fb = s_sh[SH_RENDER_FB];
		}
		else
		{
			/* Double-buffer fallback: wait for PAN acknowledgement before
			   reading the newly published back-buffer address. */
			if (s_presentSeq != 0 && s_sh[SH_FLIP_SEQ] != s_presentSeq)
			{
				if (!s_dropPending) { s_dropFirstUs = zzdf_us_now(); s_dropPending = 1; }
				s_dropped++;
				s_sh[SH_PRESENT_DROPPED] = s_dropped;
				return;
			}
			fb = s_sh[SH_FB_ADDR];
		}
		if (!fb || !pitch || !outW || !outH) return;   /* headless */

		u32 convT0 = zzdf_us_now();

		 











		zzdf_present_geom pg;
		zzdf_present_geom_compute(s_vdispWidth, s_vdispHeight, outW, outH, &pg);
		if (!pg.sw || !pg.sh) return;

		if (s_vdispWidth != outW || s_vdispHeight != outH)
		{
			static u32 s_toldGeom = 0;
			u32 geom = (s_vdispWidth << 16) | (s_vdispHeight & 0xFFFFu);
			s_sh[SH_RES_MISMATCH] = geom;
			/* Reported on every CHANGE, not once per session: the
			   engine legitimately switches size several times, and a
			   single line could not say which switch it described.
			   An engine image LARGER than the area is the only case
			   that is still a fault, and it is named as such. */
			if (s_toldGeom != geom)
			{
				s_toldGeom = geom;
				char m[160];
				if (s_vdispWidth > outW || s_vdispHeight > outH)
					sprintf(m, "[ZZDF] RESOLUTION FAULT engine %ux%u exceeds area %ux%u, clamped\n",
					        (unsigned)s_vdispWidth, (unsigned)s_vdispHeight,
					        (unsigned)outW, (unsigned)outH);
				else
					sprintf(m, "[ZZDF] present engine %ux%u in %ux%u: %ux, +%u,+%u%s\n",
					        (unsigned)s_vdispWidth, (unsigned)s_vdispHeight,
					        (unsigned)outW, (unsigned)outH,
					        (unsigned)pg.scale, (unsigned)pg.ox, (unsigned)pg.oy,
					        pg.border ? " (bordered)" : "");
				zzdf_log_puts(m);
			}
		}

		/* One call, shared verbatim with the host test: border
		   clear, 1:1 fast path and integer magnification all live
		   in zzdf_present_geom.h so the bytes proved on the host
		   are the bytes the board runs. */
		zzdf_present_blit(s_indexed, s_vdispWidth, s_palette,
		                  (u8*)fb, pitch, outW, outH, &pg);

		{
			 


			u32 span = pg.border ? (outH * pitch) : (pg.dh * pitch);
			dcache_clean_range(fb, span);
			s_lastFb = fb; s_lastFbLen = span;
		}
		__asm__ volatile("dsb":::"memory");

		{
			u32 now = zzdf_us_now();
			u32 conv = now - convT0;
			s_sh[SH_CONV_US_LAST] = conv;
			if (conv > s_convUsMax) { s_convUsMax = conv;
				s_sh[SH_CONV_US_MAX] = conv; }

			/* how long did an already-rendered frame sit waiting for
			   the 68k to acknowledge the previous PAN? */
			if (s_dropPending)
			{
				s_flipWaitSum += now - s_dropFirstUs;
				s_flipWaitN++;
				s_sh[SH_FLIPWAIT_US_AVG] = s_flipWaitSum / s_flipWaitN;
				if (s_flipWaitN >= 4096u)
				{ s_flipWaitSum /= 2u; s_flipWaitN /= 2u; }
				s_dropPending = 0;
			}

			if (s_readyLastUs)
			{
				u32 gap = now - s_readyLastUs;
				s_readyGapSum += gap; s_readyGapN++;
				if (gap > s_readyGapMax) { s_readyGapMax = gap;
					s_sh[SH_READY_GAP_US_MAX] = gap; }
				if (s_readyGapN)
					s_sh[SH_READY_GAP_US_AVG] = s_readyGapSum / s_readyGapN;
				/* keep the running means from overflowing on long runs */
				if (s_readyGapN >= 4096u)
				{ s_readyGapSum /= 2u; s_readyGapN /= 2u; }
			}
			s_readyLastUs = now;
		}

		s_presents++;
		s_sh[SH_PRESENT_COUNT] = s_presents;
		if (rseq)
		{
			/* answer with the sequence we just finished: the launcher
			   flips only when FRAME_READY matches the target it gave */
			s_lastRenderSeq = rseq;
			s_presentSeq    = rseq;
			s_sh[SH_FRAME_READY] = rseq;
		}
		else
		{
			s_sh[SH_FRAME_READY] = ++s_presentSeq;  /* 68k PANs + acks */
		}
		__asm__ volatile("dsb":::"memory");
	}

	/* ---------------- GPU-only surface: inert ------------------ */
	RenderTargetHandle createRenderTarget(u32 width, u32 height, bool hasDepthBuffer)
	{ (void)width;(void)height;(void)hasDepthBuffer; return nullptr; }
	void freeRenderTarget(RenderTargetHandle handle) { (void)handle; }
	void unbindRenderTarget() {}
	const TextureGpu* getRenderTargetTexture(RenderTargetHandle rtHandle)
	{ (void)rtHandle; return nullptr; }
	void getRenderTargetDim(RenderTargetHandle rtHandle, u32* width, u32* height)
	{ (void)rtHandle; if (width) *width = 0; if (height) *height = 0; }
	void copyBackbufferToRenderTarget(RenderTargetHandle dst) { (void)dst; }
	void captureScreenToMemory(u32* mem) { (void)mem; }
	void bloomPostEnable(bool enable) { (void)enable; }

	 



	const u8* zzdf_vfbPixels() { return s_indexed; }
	u32  zzdf_vfbW()  { return s_vdispWidth; }
	u32  zzdf_vfbH()  { return s_vdispHeight; }
	void zzdf_lutRead(u32* dst)        { for (u32 i = 0; i < 256; i++) dst[i] = s_palette[i]; }
	void zzdf_lutWrite(const u32* src) { for (u32 i = 0; i < 256; i++) s_palette[i] = src[i]; }
	void zzdf_lutSet(u32 idx, u32 xrgb) { if (idx < 256) s_palette[idx] = xrgb; }
}

 











extern "C" unsigned char* zzdf_rb_vfb(unsigned int* w, unsigned int* h)
{
	const u8* px = TFE_RenderBackend::zzdf_vfbPixels();
	if (!px) return 0;
	if (w) *w = TFE_RenderBackend::zzdf_vfbW();
	if (h) *h = TFE_RenderBackend::zzdf_vfbH();
	return (unsigned char*)px;          /* the engine's buffer, writable */
}

extern "C" void zzdf_rb_redirty(void)
{
	if (TFE_RenderBackend::zzdf_vfbPixels()) s_dirty = 1;
}

extern "C" void zzdf_rb_lut_save(unsigned int* dst256)
{
	if (dst256) TFE_RenderBackend::zzdf_lutRead((u32*)dst256);
}

extern "C" void zzdf_rb_lut_restore(const unsigned int* src256)
{
	if (src256) TFE_RenderBackend::zzdf_lutWrite((const u32*)src256);
}

extern "C" void zzdf_rb_lut_set(unsigned int idx, unsigned int r,
                                unsigned int g, unsigned int b)
{
	/* same XRGB order swap() reads (see setPalette above) */
	TFE_RenderBackend::zzdf_lutSet(idx, ((r & 0xFFu) << 16) |
	                                    ((g & 0xFFu) << 8)  | (b & 0xFFu));
}
