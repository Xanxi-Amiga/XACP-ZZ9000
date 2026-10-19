/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * zzq2_cd.c - ZZQuake2: the CDAudio_* functions of Quake II on the
 * music queue of the ZZQuake640 release.
 *
 * Replaces null/cd_null.c of quake2generic (not linked).
 *
 * Quake II asks for music with CD track numbers: every map names its
 * track (worldspawn "sounds", sent to the client in CS_CDTRACK), and
 * the client calls CDAudio_Play(track, true) when the map is loaded.
 * The original CD has the music on tracks 2 to 11.
 *
 * As in ZZQuake640 (Quake I), Core1 does not play anything: it puts
 * the command in the release music queue (SH_MUSQ_*, ZZQ_MUS_*) and
 * the 68k launcher plays it with the backend chosen by its MUSIC=
 * option:
 *   MUSIC=CD   the CD drive plays the audio track itself (SCSI)
 *   MUSIC=MP3  PROGDIR:baseq2/music/trackNN.mp3, decoded by XACP Core0
 *   MUSIC=MHI  the same files, decoded through the MHI library
 * Nothing is queued unless the launcher has a backend active
 * (SH_MUSIC_ENABLE). Same queue, same commands, same counters
 * (SH_CD_*) as the Quake I code of the release (zzq_patch_engine.py,
 * cd_null.c part): the launcher report "musique : interface CDAudio"
 * reads them unchanged.
 *
 * Quake II side, as the original Windows code (cd_win.c) does it:
 *   cd_nocd 1      ("CD music" off in the options menu) stops the
 *                  music; back to 0, the last track asked is played
 *                  again.
 *   track < 1      stops the music.
 *   console "cd"   play / loop <track>, stop, pause, resume, on, off,
 *                  info.
 *
 * ASCII only.
 */

#include "client/client.h"
#include "zzquake_config.h"

extern volatile unsigned int *shared;

static cvar_t *cd_nocd;
static int     zzq2_cd_track;      /* last track asked, 0 = none */
static int     zzq2_cd_loop;
static int     zzq2_cd_on;         /* !cd_nocd, as last seen */

/* Queue a music command without blocking the engine (release code). */
static void zzq2_mus_push(unsigned int cmd, unsigned int arg)
{
    unsigned int w, r, nxt;
    if (!shared[SH_MUSIC_ENABLE])
        return;
    w = shared[SH_MUSQ_WR];
    r = shared[SH_MUSQ_RD];
    nxt = (w + 1u) & (ZZQ_MUSQ_SIZE - 1u);
    if (nxt == r)
        return;                         /* queue full: dropped */
    shared[SH_MUSQ_BASE + w * 2u + 0u] = cmd;
    shared[SH_MUSQ_BASE + w * 2u + 1u] = arg;
    __asm__ volatile("dsb sy" ::: "memory");
    shared[SH_MUSQ_WR] = nxt;
}

static void zzq2_cd_send_play(int track, int looping)
{
    shared[SH_CD_PLAY]++;
    shared[SH_CD_TRACK] = (unsigned int)track;
    shared[SH_CD_LOOP]  = looping ? 1u : 0u;
    if (track >= 1 && track <= 31)
        shared[SH_CD_TRACKMASK] |= (1u << track);
    zzq2_mus_push(ZZQ_MUS_PLAY,
                  ((unsigned int)track & 0xFFu) | (looping ? 0x100u : 0u));
}

void CDAudio_Stop(void)
{
    shared[SH_CD_STOP]++;
    zzq2_mus_push(ZZQ_MUS_STOP, 0u);
}

void CDAudio_Play(int track, qboolean looping)
{
    if (track < 1) {
        zzq2_cd_track = 0;
        CDAudio_Stop();
        return;
    }
    zzq2_cd_track = track;
    zzq2_cd_loop  = looping ? 1 : 0;
    if (cd_nocd && cd_nocd->value)
        return;                         /* played when cd_nocd goes to 0 */
    zzq2_cd_send_play(track, zzq2_cd_loop);
}

static void zzq2_cd_pause(void)
{
    shared[SH_CD_PAUSE]++;
    zzq2_mus_push(ZZQ_MUS_PAUSE, 0u);
}

void CDAudio_Resume(void)
{
    shared[SH_CD_RESUME]++;
    zzq2_mus_push(ZZQ_MUS_RESUME, 0u);
}

/* Once per frame. Only the "CD music" switch is followed here: the
   playing state and the looping belong to the 68k backend. */
void CDAudio_Update(void)
{
    int on;
    shared[SH_CD_UPDATE]++;
    if (!cd_nocd)
        return;
    on = cd_nocd->value ? 0 : 1;
    if (on == zzq2_cd_on)
        return;
    zzq2_cd_on = on;
    shared[SH_CD_VOLMUTE] = on ? 0u : 1u;
    if (!on)
        CDAudio_Stop();
    else if (zzq2_cd_track)
        zzq2_cd_send_play(zzq2_cd_track, zzq2_cd_loop);
}

/* Window activation: there is no window on Core1, nothing to do. */
void CDAudio_Activate(qboolean active)
{
    (void)active;
}

static void zzq2_cd_f(void)
{
    char *c;
    if (Cmd_Argc() < 2) {
        Com_Printf("cd play <track> | loop <track> | stop | pause | resume | on | off | info\n");
        return;
    }
    c = Cmd_Argv(1);
    if (!Q_strcasecmp(c, "play")) {
        if (Cmd_Argc() > 2) CDAudio_Play(atoi(Cmd_Argv(2)), false);
        return;
    }
    if (!Q_strcasecmp(c, "loop")) {
        if (Cmd_Argc() > 2) CDAudio_Play(atoi(Cmd_Argv(2)), true);
        return;
    }
    if (!Q_strcasecmp(c, "stop"))   { zzq2_cd_track = 0; CDAudio_Stop(); return; }
    if (!Q_strcasecmp(c, "pause"))  { zzq2_cd_pause();  return; }
    if (!Q_strcasecmp(c, "resume")) { CDAudio_Resume(); return; }
    if (!Q_strcasecmp(c, "on"))     { Cvar_Set("cd_nocd", "0"); return; }
    if (!Q_strcasecmp(c, "off"))    { Cvar_Set("cd_nocd", "1"); return; }
    if (!Q_strcasecmp(c, "info")) {
        Com_Printf("music backend %s, %u plays asked, track %d%s, cd_nocd %d\n",
                   shared[SH_MUSIC_ENABLE] ? "active" : "NONE (launcher without MUSIC=)",
                   shared[SH_CD_PLAY], zzq2_cd_track,
                   zzq2_cd_loop ? " (loop)" : "",
                   cd_nocd ? (int)cd_nocd->value : 0);
        return;
    }
    Com_Printf("cd: unknown subcommand\n");
}

int CDAudio_Init(void)
{
    shared[SH_CD_INIT]++;
    cd_nocd = Cvar_Get("cd_nocd", "0", CVAR_ARCHIVE);
    zzq2_cd_on = cd_nocd->value ? 0 : 1;
    Cmd_AddCommand("cd", zzq2_cd_f);
    return 0;
}

void CDAudio_Shutdown(void)
{
    shared[SH_CD_SHUTDOWN]++;
    zzq2_mus_push(ZZQ_MUS_SHUTDOWN, 0u);
}
