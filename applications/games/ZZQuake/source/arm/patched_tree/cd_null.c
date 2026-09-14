/*
Copyright (C) 1996-1997 Id Software, Inc.

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  

See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.

*/
#include "quakedef.h"

/* ---- ZZQuake diagnostic hooks (injected, see zzq_patch_engine.py) */
#include "zzquake_config.h"   /* SH_LG_* diagnostics */
extern void zzq_hunk_fail(const char *name, int raw, int adj,
                          int low, int high, int total);
extern int  zzq_alias_stage;
extern int  zzq_cvarfix_enabled(void);
extern void zzq_cvar_snapshot(int which);
extern void zzq_ftoa(char *dst, int cap, float v);
extern void zzq_ftoa_prec(char *dst, int cap, float v, int prec);
extern void zzq_timedemo_result(int frames, float seconds);
extern void zzq_note_command(const char *text);
extern double zzq_min_frametime(void);
extern void zzq_ph_begin(int p);
extern void zzq_ph_end(int p);
#define ZZQ_PH(p)     zzq_ph_begin(p)
#define ZZQ_PH_END(p) zzq_ph_end(p)
extern void zzq_s2_begin(int p);
extern void zzq_s2_end(int p);
extern void zzq_s2_count(int kind, void *spans);
#ifdef ZZQ_PRODUCTION
/* Production blob: level-2 probes are disabled.
   Level-1 profiling remains available at negligible cost. */
#define ZZQ_S2(p)     ((void)0)
#define ZZQ_S2_END(p) ((void)0)
/* zzq_s2_count remains declared normally and becomes a no-op
   in the production platform implementation. */
#else
#define ZZQ_S2(p)     zzq_s2_begin(p)
#define ZZQ_S2_END(p) zzq_s2_end(p)
#endif
extern void zzq_surfcache_got(int bytes);
/* ---- end ZZQuake hooks ---- */

extern volatile unsigned int *shared;

/* Queue a music command without blocking the engine. */
static void zzq_mus_push(unsigned int cmd, unsigned int arg)
{
    unsigned int w;
    /* Do not queue events unless a 68k music backend is active. */
    if (!shared[SH_MUSIC_ENABLE]) return;
    w = shared[SH_MUSQ_WR];
    unsigned int r = shared[SH_MUSQ_RD];
    unsigned int nxt = (w + 1u) & (ZZQ_MUSQ_SIZE - 1u);
    if (nxt == r) return;
    shared[SH_MUSQ_BASE + w * 2u + 0u] = cmd;
    shared[SH_MUSQ_BASE + w * 2u + 1u] = arg;
    __asm__ volatile("dsb sy" ::: "memory");
    shared[SH_MUSQ_WR] = nxt;
}

void CDAudio_Play(byte track, qboolean looping)
{
    shared[SH_CD_PLAY]++;
    shared[SH_CD_TRACK] = (unsigned int)track;
    shared[SH_CD_LOOP]  = looping ? 1u : 0u;
    if (track >= 1 && track <= 31)
        shared[SH_CD_TRACKMASK] |= (1u << track);
    zzq_mus_push(ZZQ_MUS_PLAY,
                 ((unsigned int)track & 0xFFu) | (looping ? 0x100u : 0u));
}

void CDAudio_Stop(void)
{
    shared[SH_CD_STOP]++;
    zzq_mus_push(ZZQ_MUS_STOP, 0u);
}

void CDAudio_Pause(void)
{
    shared[SH_CD_PAUSE]++;
    zzq_mus_push(ZZQ_MUS_PAUSE, 0u);
}

void CDAudio_Resume(void)
{
    shared[SH_CD_RESUME]++;
    zzq_mus_push(ZZQ_MUS_RESUME, 0u);
}

void CDAudio_Update(void)
{
    /* Called once per frame. Playback state and looping are handled
       by the 68k backend; only bgmvolume transitions are exported. */
    static int was_muted = -1;
    int muted;
    shared[SH_CD_UPDATE]++;
    muted = (bgmvolume.value <= 0.0f) ? 1 : 0;
    if (muted != was_muted) {
        was_muted = muted;
        zzq_mus_push(muted ? ZZQ_MUS_PAUSE : ZZQ_MUS_RESUME, 0u);
        shared[SH_CD_VOLMUTE] = (unsigned int)muted;
    }
}

/* Commande console "cd", absente de quakegeneric : cd_null.c ne
   l'enregistrait pas, d'ou "unknown command cd". Syntaxe d'origine
   de Quake. */
static void CD_f(void)
{
    const char *c;
    if (Cmd_Argc() < 2) {
        Con_Printf("cd play <track> | loop <track> | stop | pause | resume\n");
        return;
    }
    c = Cmd_Argv(1);
    if (!Q_strcasecmp(c, "play")) {
        if (Cmd_Argc() > 2) CDAudio_Play((byte)Q_atoi(Cmd_Argv(2)), false);
        return;
    }
    if (!Q_strcasecmp(c, "loop")) {
        if (Cmd_Argc() > 2) CDAudio_Play((byte)Q_atoi(Cmd_Argv(2)), true);
        return;
    }
    if (!Q_strcasecmp(c, "stop"))   { CDAudio_Stop();   return; }
    if (!Q_strcasecmp(c, "pause"))  { CDAudio_Pause();  return; }
    if (!Q_strcasecmp(c, "resume")) { CDAudio_Resume(); return; }
    if (!Q_strcasecmp(c, "info")) {
        Con_Printf("%u lectures demandees, derniere piste %u\n",
                   shared[SH_CD_PLAY], shared[SH_CD_TRACK]);
        return;
    }
    Con_Printf("cd : sous-commande inconnue\n");
}

int CDAudio_Init(void)
{
    shared[SH_CD_INIT]++;
    Cmd_AddCommand("cd", CD_f);
    return 0;          /* engine behavior unchanged */
}

void CDAudio_Shutdown(void)
{
    shared[SH_CD_SHUTDOWN]++;
    zzq_mus_push(ZZQ_MUS_SHUTDOWN, 0u);
}
