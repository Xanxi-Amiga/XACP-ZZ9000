/* Shared-memory keyboard and mouse input bridge for TFE. */
 




















#include <TFE_Input/input.h>
#include <TFE_Input/inputMapping.h>
#include "zzdf_platform_hooks.h"

extern "C" {
#include "zzdf_config.h"
}

static volatile u32* s_sh = (volatile u32*)ZZDF_SHARED_ARM;
static s32 s_relX = 0, s_relY = 0;
/* last mouse totals we consumed; the 68k owns the slots. */
static u32 s_lastTotX = 0, s_lastTotY = 0;
static s32 s_absX = 160, s_absY = 100;
 

static int s_absInit = 0;
 
static char s_text[32];
static u32  s_textLen = 0;

extern "C" void zzdf_getRelativeMouse(int* dx, int* dy)
{
	*dx = (int)s_relX;
	*dy = (int)s_relY;
}
extern "C" void zzdf_getAbsoluteMouse(int* x, int* y)
{
	*x = (int)s_absX;
	*y = (int)s_absY;
}

extern "C" void zzdf_input_pump(void)
{
	/* Mouse sensitivity. inputMapping_startup() leaves it at 1.0f
	   because upstream sets it from the configuration UI, which this
	   port does not have. The launcher owns the value (keypad -/+),
	   so re-apply it every pump rather than once at startup. */
	{
		u32 pct = s_sh[SH_MOUSE_SENS];
		if (pct < ZZDF_MOUSE_SENS_MIN) pct = ZZDF_MOUSE_SENS_DEF;
		if (pct > ZZDF_MOUSE_SENS_MAX) pct = ZZDF_MOUSE_SENS_MAX;
		TFE_Input::InputConfig* cfg = TFE_Input::inputMapping_get();
		if (cfg)
		{
			f32 s = (f32)pct / 100.0f;
			cfg->mouseSensitivity[0] = s;
			cfg->mouseSensitivity[1] = s;
		}
	}

	 





















	{
		/* Mouse totals are 16-bit so each update is published atomically
		   through the Zorro window. Unsigned wrap preserves delta math. */
		u32 totX = s_sh[SH_MOUSE_DX] & 0xFFFFu;
		u32 totY = s_sh[SH_MOUSE_DY] & 0xFFFFu;
		s_relX = (s32)(s16)(u16)(totX - s_lastTotX);
		s_relY = (s32)(s16)(u16)(totY - s_lastTotY);
		s_lastTotX = totX;
		s_lastTotY = totY;
	}
	 




















	{
		s32 dw = (s32)(s_sh[SH_SCREEN_W] ? s_sh[SH_SCREEN_W] : s_sh[SH_FB_WIDTH]);
		s32 dh = (s32)(s_sh[SH_SCREEN_H] ? s_sh[SH_SCREEN_H] : s_sh[SH_FB_HEIGHT]);
		s32 gw = (s32)s_sh[SH_FB_WIDTH];
		s32 gh = (s32)s_sh[SH_FB_HEIGHT];
		s32 xmax, ymax;
		if (dw < 16 || dw > 4096) dw = 320;
		if (dh < 16 || dh > 4096) dh = 200;
		if (gw < 16 || gw > 4096) gw = 320;
		if (gh < 16 || gh > 4096) gh = 200;

		 

























		if (dw >= dh)
		{
			xmax = (gw - 1) * dh / (gh ? gh : 1);
			ymax = (gh - 1) * dh / (gh ? gh : 1);
		}
		else
		{
			xmax = (gw - 1) * dw / (gw ? gw : 1);
			ymax = (gh - 1) * dw / (gw ? gw : 1);
		}
		if (xmax < 16) xmax = dw - 1;
		if (ymax < 16) ymax = dh - 1;

		if (!s_absInit) { s_absX = xmax / 2; s_absY = ymax / 2; s_absInit = 1; }
		s_absX += s_relX;
		s_absY += s_relY;
		if (s_absX < 0)    s_absX = 0;
		if (s_absY < 0)    s_absY = 0;
		if (s_absX > xmax) s_absX = xmax;
		if (s_absY > ymax) s_absY = ymax;
	}
	TFE_Input::setRelativeMousePos(s_relX, s_relY);
	TFE_Input::setMousePos(s_absX, s_absY);

	/* mouse buttons */
	u32 btn = s_sh[SH_MOUSE_BTN];
	static u32 s_prevBtn = 0;
	u32 changed = btn ^ s_prevBtn;
	for (u32 b = 0; b < 3; b++)
	{
		if (!(changed & (1u << b))) continue;
		MouseButton mb = (b == 0) ? MBUTTON_LEFT :
		                 (b == 1) ? MBUTTON_RIGHT : MBUTTON_MIDDLE;
		if (btn & (1u << b)) TFE_Input::setMouseButtonDown(mb);
		else                 TFE_Input::setMouseButtonUp(mb);
	}
	s_prevBtn = btn;

	 

	volatile u32* ring = (volatile u32*)(ZZDF_SHARED_ARM + ZZDF_KEYRING_OFF);
	u32 head = s_sh[SH_KEYRING_HEAD];
	u32 tail = s_sh[SH_KEYRING_TAIL];
	while (tail != head)
	{
		u32 ev = ring[tail % ZZDF_KEYRING_LEN];
		u32 code = ev & 0xFF;
		u32 up = (ev >> 8) & 1;
		u32 ascii = (ev >> 16) & 0xFF;
		if (up) TFE_Input::setKeyUp((KeyboardCode)code);
		else
		{
			TFE_Input::setKeyDown((KeyboardCode)code, false);
			TFE_Input::setKeyPress((KeyboardCode)code);
			 






			TFE_Input::setBufferedKey((KeyboardCode)code);
			if (ascii >= 32 && ascii < 127 &&
			    s_textLen < sizeof(s_text) - 1)
				s_text[s_textLen++] = (char)ascii;
		}
		tail++;
	}
	s_sh[SH_KEYRING_TAIL] = tail;

	/* setBufferedInput() is a strcpy, not an append, and this pump runs
	   several times per frame (the pacer waits in it). So the text is
	   accumulated here and handed over whole, and the accumulator is
	   cleared where the engine clears its own buffered state - see
	   zzdf_input_text_consumed(). */
	if (s_textLen)
	{
		s_text[s_textLen] = 0;
		TFE_Input::setBufferedInput(s_text);
	}
}

/* Called from the game loop at the same point TFE_Input::endFrame() is,
   which is where upstream's buffered text is cleared. */
extern "C" void zzdf_input_text_consumed(void)
{
	s_textLen = 0;
	s_text[0] = 0;
}

extern "C" void zzdf_input_endframe(void)
{
	zzdf_input_text_consumed();
	TFE_Input::endFrame();
}
