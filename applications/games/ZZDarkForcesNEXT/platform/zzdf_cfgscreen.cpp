/* In-game configuration screen for ZZDarkForcesNEXT. */
#include <TFE_Input/input.h>
#include <TFE_Input/inputMapping.h>
#include <TFE_Audio/audioSystem.h>
#include <TFE_Audio/midiPlayer.h>
#include <TFE_Settings/settings.h>
#include <TFE_System/system.h>
#include <TFE_DarkForces/time.h>
#include <TFE_DarkForces/GameUI/escapeMenu.h>
#include <cstring>
#include <cstdio>

extern "C" {
#include "zzdf_config.h"
	extern void zzdf_log_puts(const char *s);
	extern unsigned char* zzdf_rb_vfb(unsigned int* w, unsigned int* h);
	extern void zzdf_rb_redirty(void);
	extern void zzdf_rb_lut_save(unsigned int* dst256);
	extern void zzdf_rb_lut_restore(const unsigned int* src256);
	extern void zzdf_rb_lut_set(unsigned int idx, unsigned int r,
	                            unsigned int g, unsigned int b);
}

namespace
{
	volatile u32* s_sh = (volatile u32*)ZZDF_SHARED_ARM;

	/* Palette entries this screen owns while it is up. High indices:
	   the game image underneath is blanked when CONFIG is entered, and
	   the whole LUT is restored on the way out anyway. */
	enum {
		C_BG = 248, C_PANEL, C_EDGE, C_BAR, C_TEXT, C_FILL   /* 248..253 */
	};

	enum { IT_MASTER = 0, IT_SFX, IT_MUSIC, IT_MOUSE,
	       IT_AUTORUN, IT_CROUCH, IT_SECRETMSG,
	       IT_KEYCOL, IT_MAPSEC, IT_SMOOTHVUE,
	       IT_BACK, IT_COUNT };

	bool s_open    = false;
	s32  s_sel     = 0;
	u32  s_savedLut[256];

	 


















	bool s_touched = false;    
	bool s_armed   = false;
	bool s_closing = false;
	 











	u32  s_pending = 0;       /* choices made, not yet honoured */
	u32  s_opens   = 0;
	u32  s_refract = 0;
	const u32 c_refractFrames = 8;

	 



	void clearAllKeyEdges()
	{
		for (u32 k = 0; k < (u32)KEY_LAST; k++)
			TFE_Input::clearKeyPressed((KeyboardCode)k);
	}

	/* ---- 5x7 font in a 6x8 cell, one byte per row, bits 4..0.
	   Built in, so this screen needs no asset, no font_load() and no
	   game_alloc allocation that a level load would free. */
	const u8 c_font[95][7] =
	{
	{ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },   /* ' ' */
	{ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },   /* '!' */
	{ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },   /* '?' */
	{ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },   /* '#' */
	{ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },   /* '$' */
	{ 0x19, 0x1A, 0x02, 0x04, 0x08, 0x16, 0x13 },   /* '%' */
	{ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },   /* '&' */
	{ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },   /* ''' */
	{ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },   /* '(' */
	{ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },   /* ')' */
	{ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },   /* '*' */
	{ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },   /* '+' */
	{ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },   /* ',' */
	{ 0x00, 0x00, 0x00, 0x1F, 0x00, 0x00, 0x00 },   /* '-' */
	{ 0x00, 0x00, 0x00, 0x00, 0x00, 0x0C, 0x0C },   /* '.' */
	{ 0x01, 0x02, 0x02, 0x04, 0x08, 0x08, 0x10 },   /* '/' */
	{ 0x0E, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0E },   /* '0' */
	{ 0x04, 0x0C, 0x04, 0x04, 0x04, 0x04, 0x0E },   /* '1' */
	{ 0x0E, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1F },   /* '2' */
	{ 0x1F, 0x02, 0x04, 0x02, 0x01, 0x11, 0x0E },   /* '3' */
	{ 0x02, 0x06, 0x0A, 0x12, 0x1F, 0x02, 0x02 },   /* '4' */
	{ 0x1F, 0x10, 0x1E, 0x01, 0x01, 0x11, 0x0E },   /* '5' */
	{ 0x06, 0x08, 0x10, 0x1E, 0x11, 0x11, 0x0E },   /* '6' */
	{ 0x1F, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08 },   /* '7' */
	{ 0x0E, 0x11, 0x11, 0x0E, 0x11, 0x11, 0x0E },   /* '8' */
	{ 0x0E, 0x11, 0x11, 0x0F, 0x01, 0x02, 0x0C },   /* '9' */
	{ 0x00, 0x0C, 0x0C, 0x00, 0x0C, 0x0C, 0x00 },   /* ':' */
	{ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },   /* ';' */
	{ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },   /* '<' */
	{ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },   /* '=' */
	{ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },   /* '>' */
	{ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },   /* '?' */
	{ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },   /* '@' */
	{ 0x0E, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11 },   /* 'A' */
	{ 0x1E, 0x11, 0x11, 0x1E, 0x11, 0x11, 0x1E },   /* 'B' */
	{ 0x0E, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0E },   /* 'C' */
	{ 0x1E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x1E },   /* 'D' */
	{ 0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x1F },   /* 'E' */
	{ 0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x10 },   /* 'F' */
	{ 0x0E, 0x11, 0x10, 0x17, 0x11, 0x11, 0x0E },   /* 'G' */
	{ 0x11, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11 },   /* 'H' */
	{ 0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x1F },   /* 'I' */
	{ 0x07, 0x02, 0x02, 0x02, 0x02, 0x12, 0x0C },   /* 'J' */
	{ 0x11, 0x12, 0x14, 0x18, 0x14, 0x12, 0x11 },   /* 'K' */
	{ 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1F },   /* 'L' */
	{ 0x11, 0x1B, 0x15, 0x11, 0x11, 0x11, 0x11 },   /* 'M' */
	{ 0x11, 0x19, 0x15, 0x13, 0x11, 0x11, 0x11 },   /* 'N' */
	{ 0x0E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E },   /* 'O' */
	{ 0x1E, 0x11, 0x11, 0x1E, 0x10, 0x10, 0x10 },   /* 'P' */
	{ 0x0E, 0x11, 0x11, 0x11, 0x15, 0x12, 0x0D },   /* 'Q' */
	{ 0x1E, 0x11, 0x11, 0x1E, 0x14, 0x12, 0x11 },   /* 'R' */
	{ 0x0F, 0x10, 0x10, 0x0E, 0x01, 0x01, 0x1E },   /* 'S' */
	{ 0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04 },   /* 'T' */
	{ 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E },   /* 'U' */
	{ 0x11, 0x11, 0x11, 0x11, 0x11, 0x0A, 0x04 },   /* 'V' */
	{ 0x11, 0x11, 0x11, 0x15, 0x15, 0x1B, 0x11 },   /* 'W' */
	{ 0x11, 0x11, 0x0A, 0x04, 0x0A, 0x11, 0x11 },   /* 'X' */
	{ 0x11, 0x11, 0x0A, 0x04, 0x04, 0x04, 0x04 },   /* 'Y' */
	{ 0x1F, 0x01, 0x02, 0x04, 0x08, 0x10, 0x1F },   /* 'Z' */
	{ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },   /* '[' */
	{ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },   /* '?' */
	{ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },   /* ']' */
	{ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },   /* '^' */
	{ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },   /* '_' */
	{ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },   /* '`' */
	{ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },   /* 'a' */
	{ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },   /* 'b' */
	{ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },   /* 'c' */
	{ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },   /* 'd' */
	{ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },   /* 'e' */
	{ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },   /* 'f' */
	{ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },   /* 'g' */
	{ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },   /* 'h' */
	{ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },   /* 'i' */
	{ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },   /* 'j' */
	{ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },   /* 'k' */
	{ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },   /* 'l' */
	{ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },   /* 'm' */
	{ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },   /* 'n' */
	{ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },   /* 'o' */
	{ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },   /* 'p' */
	{ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },   /* 'q' */
	{ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },   /* 'r' */
	{ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },   /* 's' */
	{ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },   /* 't' */
	{ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },   /* 'u' */
	{ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },   /* 'v' */
	{ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },   /* 'w' */
	{ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },   /* 'x' */
	{ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },   /* 'y' */
	{ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },   /* 'z' */
	{ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },   /* '{' */
	{ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },   /* '|' */
	{ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },   /* '}' */
	{ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },   /* '~' */
	};

	void putGlyph(u8* px, u32 stride, u32 w, u32 h, s32 x, s32 y, char c, u8 col, s32 sc)
	{
		u32 idx = (u32)(u8)c;
		if (idx < 32u || idx > 126u) return;
		const u8* g = c_font[idx - 32u];
		for (s32 ry = 0; ry < 7; ry++)
		{
			u8 bits = g[ry];
			if (!bits) continue;
			for (s32 rx = 0; rx < 5; rx++)
			{
				if (!(bits & (1 << (4 - rx)))) continue;
				for (s32 dy = 0; dy < sc; dy++)
				{
					s32 py = y + ry * sc + dy;
					if (py < 0 || py >= (s32)h) continue;
					u8* row = px + (u32)py * stride;
					for (s32 dx = 0; dx < sc; dx++)
					{
						s32 pxx = x + rx * sc + dx;
						if (pxx < 0 || pxx >= (s32)w) continue;
						row[pxx] = col;
					}
				}
			}
		}
	}

	void putText(u8* px, u32 stride, u32 w, u32 h, s32 x, s32 y,
	             const char* s, u8 col, s32 sc)
	{
		for (; *s; s++)
		{
			putGlyph(px, stride, w, h, x, y, *s, col, sc);
			x += 6 * sc;
		}
	}

	void fillRect(u8* px, u32 stride, u32 w, u32 h,
	              s32 x0, s32 y0, s32 x1, s32 y1, u8 col)
	{
		if (x0 < 0) { x0 = 0; }
		if (y0 < 0) { y0 = 0; }
		if (x1 > (s32)w) { x1 = (s32)w; }
		if (y1 > (s32)h) { y1 = (s32)h; }
		for (s32 y = y0; y < y1; y++)
			memset(px + (u32)y * stride + (u32)x0, col, (size_t)(x1 - x0));
	}

	 





















	bool* boolOf(s32 item)
	{
		TFE_Settings_Game* g = TFE_Settings::getGameSettings();
		if (!g) return nullptr;
		switch (item)
		{
			case IT_AUTORUN:   return &g->df_autorun;
			case IT_CROUCH:    return &g->df_crouchToggle;
			case IT_SECRETMSG: return &g->df_showSecretFoundMsg;
			case IT_KEYCOL:    return &g->df_showKeyColors;
			case IT_MAPSEC:    return &g->df_showMapSecrets;
			case IT_SMOOTHVUE: return &g->df_smoothVUEs;
			default:           return nullptr;
		}
	}

	f32* volumeOf(s32 item)
	{
		TFE_Settings_Sound* s = TFE_Settings::getSoundSettings();
		if (!s) return nullptr;
		switch (item)
		{
			case IT_MASTER: return &s->masterVolume;
			case IT_SFX:    return &s->soundFxVolume;
			case IT_MUSIC:  return &s->musicVolume;
			default:        return nullptr;
		}
	}

	/* exactly what upstream's configSound() does after its sliders */
	void applySound()
	{
		TFE_Settings_Sound* s = TFE_Settings::getSoundSettings();
		if (!s) return;
		TFE_Audio::setVolume(s->soundFxVolume * s->masterVolume);
		TFE_MidiPlayer::setVolume(s->musicVolume * s->masterVolume);
	}

	s32 mouseSens()
	{
		s32 p = (s32)s_sh[SH_MOUSE_SENS];
		if (p < ZZDF_MOUSE_SENS_MIN || p > ZZDF_MOUSE_SENS_MAX) p = ZZDF_MOUSE_SENS_DEF;
		return p;
	}

	void change(s32 dir)
	{
		{   /* LEFT sets OFF, RIGHT sets ON - never a blind flip. Pressing
		       the same direction twice must not undo itself, and with key
		       repeat a held arrow would otherwise oscillate. */
			bool* b = boolOf(s_sel);
			if (b)
			{
				const bool nv = (dir > 0);
				if (nv != *b) { *b = nv; s_touched = true; }
				return;
			}
		}
		f32* v = volumeOf(s_sel);
		if (v)
		{
			s_touched = true;
			f32 nv = *v + 0.05f * (f32)dir;
			if (nv < 0.0f) nv = 0.0f;
			if (nv > 1.0f) nv = 1.0f;
			*v = nv;
			applySound();
			return;
		}
		if (s_sel == IT_MOUSE)
		{
			s_touched = true;
			/* slot 93, which the launcher already publishes and already
			   applies every frame (keypad -/+ moves it the same way).
			   No new slot, no new protocol. */
			s32 p = mouseSens() + 5 * dir;
			if (p < ZZDF_MOUSE_SENS_MIN) p = ZZDF_MOUSE_SENS_MIN;
			if (p > ZZDF_MOUSE_SENS_MAX) p = ZZDF_MOUSE_SENS_MAX;
			s_sh[SH_MOUSE_SENS] = (u32)p;
		}
	}

	void openScreen()
	{
		s_open    = true;
		s_armed   = false;      /* no exit key acts before a release */
		s_closing = false;

		 





		if (TFE_DarkForces::escapeMenu_isOpen())
		{
			TFE_DarkForces::escapeMenu_close();
			zzdf_log_puts("[ZZDF] config: escape menu was still open, closed it\n");
		}
		clearAllKeyEdges();
		s_opens++;

		zzdf_rb_lut_save(s_savedLut);
		zzdf_rb_lut_set(C_BG,    0,   0,   0);
		zzdf_rb_lut_set(C_PANEL, 24,  24,  40);
		zzdf_rb_lut_set(C_EDGE,  120, 120, 140);
		zzdf_rb_lut_set(C_BAR,   48,  64,  128);
		zzdf_rb_lut_set(C_TEXT,  220, 220, 220);
		zzdf_rb_lut_set(C_FILL,  230, 180, 40);

		TFE_DarkForces::time_pause(JTRUE);
		zzdf_log_puts("[ZZDF] config screen opened\n");
	}

	 


































	const char* const c_optName    = "ZZDFOPT.CFG";
	const int         c_optVersion = 1;

	s32 pctOf(f32 v)
	{
		s32 p = (s32)(v * 100.0f + 0.5f);
		if (p < 0)   p = 0;
		if (p > 100) p = 100;
		return p;
	}

	void saveOptions()
	{
		TFE_Settings_Sound* s = TFE_Settings::getSoundSettings();
		if (!s) return;
		FILE* f = fopen(c_optName, "w");
		if (!f)
		{
			zzdf_log_puts("[ZZDF] options: could not write ZZDFOPT.CFG\n");
			return;
		}
		/* integers only, on purpose: no float formatting anywhere in
		   the path, and a file a human can read at a glance */
		fprintf(f, "ZZDFOPT %d\n", c_optVersion);
		fprintf(f, "MASTER %d\n", (int)pctOf(s->masterVolume));
		fprintf(f, "SFX %d\n",    (int)pctOf(s->soundFxVolume));
		fprintf(f, "MUSIC %d\n",  (int)pctOf(s->musicVolume));
		fprintf(f, "MOUSE %d\n",  (int)mouseSens());
		{    






			TFE_Settings_Game* g = TFE_Settings::getGameSettings();
			if (g)
			{
				fprintf(f, "AUTORUN %d\n",    g->df_autorun            ? 1 : 0);
				fprintf(f, "CROUCH %d\n",     g->df_crouchToggle       ? 1 : 0);
				fprintf(f, "SECRETMSG %d\n",  g->df_showSecretFoundMsg ? 1 : 0);
				fprintf(f, "KEYCOLORS %d\n",  g->df_showKeyColors      ? 1 : 0);
				fprintf(f, "MAPSECRETS %d\n", g->df_showMapSecrets     ? 1 : 0);
				fprintf(f, "SMOOTHVUE %d\n",  g->df_smoothVUEs         ? 1 : 0);
			}
		}
		fclose(f);
		zzdf_log_puts("[ZZDF] options saved to ZZDFOPT.CFG\n");
	}

	void closeScreen()
	{
		s_open    = false;
		s_closing = false;
		s_refract = c_refractFrames;
		zzdf_rb_lut_restore(s_savedLut);
		TFE_DarkForces::time_pause(JFALSE);

		/* "Eat" the keys so closing does not immediately reopen the
		   escape menu - the two calls upstream's main.cpp makes. The
		   release gate above already guarantees the key is up; this is
		   the belt to that pair of braces. */
		TFE_Input::clearKeyPressed(KEY_ESCAPE);
		TFE_Input::inputMapping_clearKeyBinding(KEY_ESCAPE);
		TFE_Input::clearKeyPressed(KEY_RETURN);
		TFE_Input::inputMapping_clearKeyBinding(KEY_RETURN);
		TFE_Input::clearKeyPressed(KEY_KP_ENTER);
		TFE_Input::clearKeyPressed(KEY_SPACE);
		clearAllKeyEdges();
		if (s_touched) { s_touched = false; saveOptions(); }
		{
			char m[64];
			sprintf(m, "[ZZDF] config screen closed (opened %u time(s))\n",
			        (unsigned)s_opens);
			zzdf_log_puts(m);
		}
	}

	bool exitKeyDown()
	{
		return TFE_Input::keyDown(KEY_ESCAPE)   ||
		       TFE_Input::keyDown(KEY_RETURN)   ||
		       TFE_Input::keyDown(KEY_KP_ENTER) ||
		       TFE_Input::keyDown(KEY_SPACE);
	}

	bool confirmPressed()
	{
		return TFE_Input::keyPressed(KEY_RETURN)   ||
		       TFE_Input::keyPressed(KEY_KP_ENTER) ||
		       TFE_Input::keyPressed(KEY_SPACE);
	}

	void draw()
	{
		u32 w = 0, h = 0;
		u8* px = zzdf_rb_vfb(&w, &h);
		if (!px || !w || !h) return;
		const u32 stride = w;

		const s32 sc = (w >= 512u) ? 2 : 1;          /* 640x400 doubles  */
		const s32 lh = 12 * sc;                       
		const s32 pw = 210 * sc, ph = 24 * sc + IT_COUNT * lh + 18 * sc;
		const s32 x0 = ((s32)w - pw) / 2, y0 = ((s32)h - ph) / 2;

		fillRect(px, stride, w, h, 0, 0, (s32)w, (s32)h, C_BG);
		fillRect(px, stride, w, h, x0 - sc, y0 - sc, x0 + pw + sc, y0 + ph + sc, C_EDGE);
		fillRect(px, stride, w, h, x0, y0, x0 + pw, y0 + ph, C_PANEL);
		putText(px, stride, w, h, x0 + 12 * sc, y0 + 7 * sc, "CONFIGURATION", C_TEXT, sc);

		/* 14 characters maximum: putText advances 6*sc per glyph and the
		   value column starts at x0 + 110*sc, so 14 glyphs end at 84 and
		   leave a gap. Longer labels would run into the bars. */
		static const char* names[IT_COUNT] =
			{ "MASTER VOLUME", "SOUND FX", "MUSIC", "MOUSE",
			  "AUTORUN", "CROUCH TOGGLE", "SECRET MESSAGE",
			  "MAP KEY COLORS", "MAP SECRETS", "SMOOTH VUE",
			  "BACK" };

		for (s32 i = 0; i < IT_COUNT; i++)
		{
			const s32 y = y0 + 24 * sc + i * lh;
			if (i == s_sel)
				fillRect(px, stride, w, h, x0 + 3 * sc, y - 2 * sc,
				         x0 + pw - 3 * sc, y + 10 * sc, C_BAR);
			putText(px, stride, w, h, x0 + 8 * sc, y, names[i], C_TEXT, sc);

			{   const bool* b = boolOf(i);
				if (b)
				{
					putText(px, stride, w, h, x0 + 110 * sc, y,
					        *b ? "ON" : "OFF", C_TEXT, sc);
					continue;
				}
			}

			s32 pct = -1, full = 100;
			const f32* v = volumeOf(i);
			if (v) { pct = (s32)(*v * 100.0f + 0.5f); }
			else if (i == IT_MOUSE) { pct = mouseSens(); full = ZZDF_MOUSE_SENS_MAX; }
			if (pct < 0) continue;

			const s32 bx = x0 + 110 * sc, bw = 60 * sc;
			fillRect(px, stride, w, h, bx, y + sc, bx + bw, y + 7 * sc, C_EDGE);
			s32 f = (pct * bw) / (full ? full : 100);
			if (f < 0) f = 0;
			if (f > bw) f = bw;
			fillRect(px, stride, w, h, bx, y + sc, bx + f, y + 7 * sc, C_FILL);

			char num[12];
			sprintf(num, "%d", (int)pct);
			putText(px, stride, w, h, bx + bw + 6 * sc, y, num, C_TEXT, sc);
		}

		putText(px, stride, w, h, x0 + 8 * sc, y0 + ph - 12 * sc,
		        "ARROWS ADJUST    ESC EXITS", C_TEXT, sc);

		zzdf_rb_redirty();
	}
}

extern "C" int zzdf_cfgscreen_open(void) { return s_open ? 1 : 0; }

/* Called by mission.cpp when the player chooses CONFIG in the escape
   menu - the click on the button, or its hotkey. One call, one opening.
   If the screen is already up, or the choice lands in the few passes
   just after a close, the request is simply held until the next pass
   that can honour it. */
extern "C" void zzdf_cfgscreen_request(void)
{
	if (s_open) return;
	s_pending = 1;
}

 





extern "C" void zzdf_cfgopt_load(void)
{
	FILE* f = fopen(c_optName, "r");
	if (!f)
	{
		zzdf_log_puts("[ZZDF] options: no ZZDFOPT.CFG, defaults in use\n");
		return;
	}

	char line[80];
	int ver = -1, master = -1, sfx = -1, music = -1, mouse = -1;
	int autorun = -1, crouch = -1, secmsg = -1;
	int keycol = -1, mapsec = -1, svue = -1;
	while (fgets(line, (int)sizeof(line), f))
	{
		char key[16];
		int  val;
		if (sscanf(line, "%15s %d", key, &val) != 2) continue;
		if      (!strcmp(key, "ZZDFOPT")) ver    = val;
		else if (!strcmp(key, "MASTER"))  master = val;
		else if (!strcmp(key, "SFX"))     sfx    = val;
		else if (!strcmp(key, "MUSIC"))   music  = val;
		else if (!strcmp(key, "MOUSE"))   mouse  = val;
		else if (!strcmp(key, "AUTORUN"))    autorun = val;
		else if (!strcmp(key, "CROUCH"))     crouch  = val;
		else if (!strcmp(key, "SECRETMSG"))  secmsg  = val;
		else if (!strcmp(key, "KEYCOLORS"))  keycol  = val;
		else if (!strcmp(key, "MAPSECRETS")) mapsec  = val;
		else if (!strcmp(key, "SMOOTHVUE"))  svue    = val;
		/* anything else: ignored on purpose */
	}
	fclose(f);

	if (ver != c_optVersion)
	{
		char m[80];
		sprintf(m, "[ZZDF] options: ZZDFOPT.CFG version %d not understood, ignored\n", ver);
		zzdf_log_puts(m);
		return;
	}

	TFE_Settings_Sound* s = TFE_Settings::getSoundSettings();
	if (s)
	{
		if (master >= 0 && master <= 100) s->masterVolume  = (f32)master / 100.0f;
		if (sfx    >= 0 && sfx    <= 100) s->soundFxVolume = (f32)sfx    / 100.0f;
		if (music  >= 0 && music  <= 100) s->musicVolume   = (f32)music  / 100.0f;
	}
	if (mouse >= ZZDF_MOUSE_SENS_MIN && mouse <= ZZDF_MOUSE_SENS_MAX)
		s_sh[SH_MOUSE_SENS] = (u32)mouse;

	{   /* Each key is validated on its own and an absent or out-of-range
	       one simply keeps the engine default - exactly like the volumes.
	       A key present but not 0 or 1 is ignored, not coerced. */
		TFE_Settings_Game* g = TFE_Settings::getGameSettings();
		if (g)
		{
			if (autorun == 0 || autorun == 1) g->df_autorun            = (autorun != 0);
			if (crouch  == 0 || crouch  == 1) g->df_crouchToggle       = (crouch  != 0);
			if (secmsg  == 0 || secmsg  == 1) g->df_showSecretFoundMsg = (secmsg  != 0);
			if (keycol  == 0 || keycol  == 1) g->df_showKeyColors      = (keycol  != 0);
			if (mapsec  == 0 || mapsec  == 1) g->df_showMapSecrets     = (mapsec  != 0);
			if (svue    == 0 || svue    == 1) g->df_smoothVUEs         = (svue    != 0);
		}
	}

	{
		char m[96];
		sprintf(m, "[ZZDF] options: master %d sfx %d music %d mouse %d\n",
		        master, sfx, music, mouse);
		zzdf_log_puts(m);
	}
	{
		TFE_Settings_Game* g = TFE_Settings::getGameSettings();
		char m[128];
		if (g)
		{
			sprintf(m, "[ZZDF] gameplay: autorun %d crouch %d secretmsg %d "
			           "keycolors %d mapsecrets %d smoothvue %d\n",
			        g->df_autorun ? 1 : 0, g->df_crouchToggle ? 1 : 0,
			        g->df_showSecretFoundMsg ? 1 : 0, g->df_showKeyColors ? 1 : 0,
			        g->df_showMapSecrets ? 1 : 0, g->df_smoothVUEs ? 1 : 0);
			zzdf_log_puts(m);
		}
	}
}

 


extern "C" void zzdf_cfgopt_apply(void)
{
	applySound();
}

/* One call per game-loop iteration. Returns 1 while the screen owns the
   frame: the caller then skips the game step and presents, exactly as
   it presents any other frame. */
extern "C" int zzdf_cfgscreen_frame(void)
{
	if (!s_open)
	{
		 







		/* Drain upstream's flag so it cannot pile up, and ignore it. */
		(void)TFE_System::systemUiRequestPosted();

		/* The refractory window only covers the few passes right after a
		   close, so the keys of that close cannot walk the escape menu
		   back to CONFIG before the game has settled. A choice made
		   during it is kept, not lost. */
		if (s_refract) { s_refract--; return 0; }
		if (!s_pending) return 0;
		s_pending = 0;
		openScreen();
	}

	/* Every exit key released at least once since the screen opened?
	   Until then none of them acts - this is what stops the Esc that
	   chose CONFIG from also closing the screen. */
	const bool exitDown = exitKeyDown();
	if (!exitDown) s_armed = true;

	/* A close asked for: hold the frame until the key comes back up, so
	   the game is never handed a pressed Esc. */
	if (s_closing)
	{
		if (!exitDown) { closeScreen(); return 1; }
		draw();
		return 1;
	}

	if (s_armed)
	{
		if (TFE_Input::keyPressed(KEY_ESCAPE)) s_closing = true;
		else if (s_sel == IT_BACK && confirmPressed()) s_closing = true;
		if (s_closing) { draw(); return 1; }
	}

	if (TFE_Input::keyPressedWithRepeat(KEY_UP))    s_sel = (s_sel + IT_COUNT - 1) % IT_COUNT;
	if (TFE_Input::keyPressedWithRepeat(KEY_DOWN))  s_sel = (s_sel + 1) % IT_COUNT;
	if (TFE_Input::keyPressedWithRepeat(KEY_LEFT))  change(-1);
	if (TFE_Input::keyPressedWithRepeat(KEY_RIGHT)) change(+1);

	draw();
	return 1;
}
