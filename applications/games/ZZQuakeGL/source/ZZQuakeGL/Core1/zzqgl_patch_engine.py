"""zzqgl_patch_engine.py - ZZQuakeGL: the Quake engine tree of the GL blob.

    python3 zzqgl_patch_engine.py <quakegeneric_source> <id WinQuake> <out dir>

Builds <out dir> (emptied first) from COPIES:
  1. the common engine files of the ZZQuake640 release tree
     (Core1_GPL/quakegeneric_source, erysdren/quakegeneric 13052102 plus
     the three snd_*.c of id), without the software renderer;
  2. the GL renderer of id Software's WinQuake (commit bf4ac424):
     gl_*.c, glquake.h, gl_model.h, gl_warp_sin.h, and vid.h / r_local.h
     (the two headers whose GLQUAKE parts quakegeneric removed); the
     software-only headers of quakegeneric are copied too (unused);
  3. the patches of the release engine patcher (zzq_patch_engine.py of
     ZZQuake640) that still apply to a GL build: hunk failure report,
     Cvar_SetValue without "%f", frame cap, stage probes, music events
     (cd_null.c), command trace, timedemo result, float formats, saves
     listing.  Nothing of what that patcher did to d_*.c / r_*.c /
     screen.c / vid_null.c / draw.c (files absent from a GL build);
  4. the GLQUAKE blocks of id's common files that quakegeneric removed
     (quakedef.h, cl_parse.c, r_part.c, view.c): 12 blocks, reinserted
     verbatim, with their #ifdef GLQUAKE / #else / #endif structure, so
     the files compile to id's GL variant with -DGLQUAKE;
  5. the GL files' own adjustments (see zzqgl_patch_gl.py, called here).

Every replacement is an exact match that must occur exactly once;
anything else stops the build.  ASCII only.
"""
import os
import shutil
import sys

QG, IDQ, OUT = sys.argv[1], sys.argv[2], sys.argv[3]

# ---------------------------------------------------------------------------
# file lists
# ---------------------------------------------------------------------------

# common engine, from the release quakegeneric tree
QG_C = """chase cl_demo cl_input cl_main cl_parse cl_tent cmd common console crc
cvar host host_cmd in_null keys mathlib menu net_loop net_main net_none
net_vcr pr_cmds pr_edict pr_exec r_part sbar snd_dma snd_mem snd_mix
sv_main sv_move sv_phys sv_user view wad world zone cd_null""".split()

QG_H = """adivtab anorms bspfile cdaudio client cmd common console crc cvar
d_iface d_ifacea d_local draw input keys mathlib menu model modelgen
net net_loop net_vcr pr_comp progdefs progs protocol quakedef
quakegeneric quakekeys r_shared render sbar screen server sound
spritegn sys view wad world zone""".split()

# GL renderer, from id WinQuake
ID_C = """gl_draw gl_mesh gl_model gl_refrag gl_rlight gl_rmain gl_rmisc
gl_rsurf gl_screen gl_warp""".split()
ID_H = "glquake gl_model gl_warp_sin anorm_dots vid r_local".split()


def rd(path):
    return open(path, encoding='latin-1').read()


def wr(path, s):
    open(path, 'w', encoding='latin-1', newline='').write(s)


def must(s, old, where):
    c = s.count(old)
    if c != 1:
        raise SystemExit("PATCH %s: expected 1 occurrence, found %d:\n%s"
                         % (where, c, old[:120]))


def rep(s, old, new, where):
    must(s, old, where)
    return s.replace(old, new, 1)


if os.path.isdir(OUT):
    shutil.rmtree(OUT)
os.makedirs(OUT)
for f in QG_C:
    shutil.copyfile(os.path.join(QG, f + '.c'), os.path.join(OUT, f + '.c'))
for f in QG_H:
    shutil.copyfile(os.path.join(QG, f + '.h'), os.path.join(OUT, f + '.h'))
for f in ID_C:
    shutil.copyfile(os.path.join(IDQ, f + '.c'), os.path.join(OUT, f + '.c'))
for f in ID_H:
    shutil.copyfile(os.path.join(IDQ, f + '.h'), os.path.join(OUT, f + '.h'))
print("  %d common .c, %d common .h (quakegeneric), %d gl .c, %d .h (id)"
      % (len(QG_C), len(QG_H), len(ID_C), len(ID_H)))


def rdo(f):
    return rd(os.path.join(OUT, f))


def wro(f, s):
    wr(os.path.join(OUT, f), s)


# ---------------------------------------------------------------------------
# 3. release patches (zzq_patch_engine.py of ZZQuake640, GL-relevant part)
# ---------------------------------------------------------------------------

HDR = '''
/* ZZQuake diagnostic hooks. */
#include "zzquake_config.h"
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
/* Disable level-2 probes in production. */
#define ZZQ_S2(p)     ((void)0)
#define ZZQ_S2_END(p) ((void)0)

#else
#define ZZQ_S2(p)     zzq_s2_begin(p)
#define ZZQ_S2_END(p) zzq_s2_end(p)
#endif
extern void zzq_surfcache_got(int bytes);

'''


def with_hdr(s):
    if 'ZZQuake diagnostic hooks' in s:
        return s
    return rep(s, '#include "quakedef.h"', '#include "quakedef.h"\n' + HDR, 'HDR')


# zone.c : Hunk_Alloc failure report (release)
z = rdo('zone.c')
# (the quakegeneric lines carry trailing tabs: kept as they are)
z = rep(z, "\tif (size < 0)\n"
           "\t\tSys_Error (\"Hunk_Alloc: bad size: %i\", size);\n"
           "\t\t\n"
           "\tsize = sizeof(hunk_t) + ((size+15)&~15);\n"
           "\t\n"
           "\tif (hunk_size - hunk_low_used - hunk_high_used < size)\n"
           "\t\tSys_Error (\"Hunk_Alloc: failed on %i bytes\",size);\n",
        "\tif (size < 0)\n"
        "\t\tSys_Error (\"Hunk_Alloc: bad size: %i\", size);\n"
        "\t\t\n"
        "\t{ int zzq_raw = size;\n"
        "\tsize = sizeof(hunk_t) + ((size+15)&~15);\n"
        "\t\n"
        "\tif (hunk_size - hunk_low_used - hunk_high_used < size) {\n"
        "\t\tzzq_hunk_fail (name, zzq_raw, size,\n"
        "\t\t               hunk_low_used, hunk_high_used, hunk_size);\n"
        "\t\tSys_Error (\"Hunk_Alloc: failed on %i bytes\",size);\n"
        "\t} }\n", 'zone.c Hunk_Alloc')
wro('zone.c', with_hdr(z))

# cvar.c : Cvar_SetValue without the "%f" formatter (misaligned stack)
cv = rdo('cvar.c')
cv = rep(cv, "void Cvar_SetValue (char *var_name, float value)\n{\n\tchar\tval[32];\n\t\n\tsprintf (val, \"%f\",value);",
         "void Cvar_SetValue (char *var_name, float value)\n{\n\tchar\tval[32];\n\t\n\tzzq_ftoa (val, sizeof(val), value);",
         'cvar.c Cvar_SetValue')
wro('cvar.c', with_hdr(cv))

# host.c : configurable frame cap, stage probes
_SW = "\n\t{ extern volatile unsigned int *shared; shared[SH_STAGE] = %d; }"
ho = rdo('host.c')
ho = rep(ho, "if (!cls.timedemo && realtime - oldrealtime < 1.0/72.0)",
         "if (!cls.timedemo && realtime - oldrealtime < zzq_min_frametime())",
         'host.c frame cap')
ho = rep(ho, "void Host_ShutdownServer(qboolean crash)\n{",
         "void Host_ShutdownServer(qboolean crash)\n{" + (_SW % 1), 'host.c SHUTSRV_IN')
ho = rep(ho, "void Host_ClearMemory (void)\n{",
         "void Host_ClearMemory (void)\n{" + (_SW % 3), 'host.c CLEARMEM')
wro('host.c', with_hdr(ho))

sv = rdo('sv_main.c')
sv = rep(sv, "void SV_SpawnServer (char *server)\n{",
         "void SV_SpawnServer (char *server)\n{" + (_SW % 4), 'sv_main.c SPAWN_IN')
wro('sv_main.c', with_hdr(sv))

# cmd.c : command trace
cm = rdo('cmd.c')
cm = rep(cm, "void	Cmd_ExecuteString (char *text, cmd_source_t src)\n{",
         "void	Cmd_ExecuteString (char *text, cmd_source_t src)\n{\n\tzzq_note_command (text);",
         'cmd.c Cmd_ExecuteString')
wro('cmd.c', with_hdr(cm))

# cl_demo.c : timedemo result exported, printed without "%f"
cd_ = rdo('cl_demo.c')
cd_ = rep(cd_, 'Con_Printf ("%i frames %5.1f seconds %5.1f fps\\n", frames, time, frames/time);',
          '{ char tb[32], fb[32];\n'
          '\t\tzzq_timedemo_result (frames, time);\n'
          '\t\tzzq_ftoa_prec (tb, sizeof(tb), time, 1);\n'
          '\t\tzzq_ftoa_prec (fb, sizeof(fb), (float)frames / time, 1);\n'
          '\t\tCon_Printf ("%i frames %s seconds %s fps\\n",'
          ' frames, tb, fb); }', 'cl_demo.c timedemo')
wro('cl_demo.c', with_hdr(cd_))

# pr_cmds.c / pr_edict.c : float formats (release)
pc = rdo('pr_cmds.c')
pc = rep(pc, 'sprintf (pr_string_temp, "%5.1f",v);',
         'zzq_ftoa_prec (pr_string_temp, 128, v, 1);', 'pr_cmds.c ftos')
wro('pr_cmds.c', with_hdr(pc))

pe = rdo('pr_edict.c')
n = 0
for a, b in (('sprintf (line, "%f", val->_float);',
              'zzq_ftoa_prec (line, 128, val->_float, 6);'),
             ('sprintf (line, "%f %f %f", val->vector[0], val->vector[1], val->vector[2]);',
              '{ char _a[40], _b[40], _c[40];\n'
              '\t\tzzq_ftoa_prec(_a, 40, val->vector[0], 6);\n'
              '\t\tzzq_ftoa_prec(_b, 40, val->vector[1], 6);\n'
              '\t\tzzq_ftoa_prec(_c, 40, val->vector[2], 6);\n'
              '\t\tsprintf (line, "%s %s %s", _a, _b, _c); }'),
             ('sprintf (line, "%5.1f", val->_float);',
              'zzq_ftoa_prec (line, 128, val->_float, 1);')):
    c = pe.count(a)
    if c:
        pe = pe.replace(a, b)
        n += c
if n == 0:
    raise SystemExit("PATCH pr_edict.c: no float format found")
wro('pr_edict.c', with_hdr(pe))
print("  pr_edict.c : %d float format(s) replaced" % n)

# menu.c : M_ScanSaves validates fscanf and the version (release)
mn = rdo('menu.c')
mn = rep(mn, """\t\tfscanf (f, \"%i\\n\", &version);
\t\tfscanf (f, \"%79s\\n\", name);
\t\tstrncpy (m_filenames[i], name, sizeof(m_filenames[i])-1);

\t// change _ back to space
\t\tfor (j=0 ; j<SAVEGAME_COMMENT_LENGTH ; j++)
\t\t\tif (m_filenames[i][j] == '_')
\t\t\t\tm_filenames[i][j] = ' ';
\t\tloadable[i] = true;
\t\tfclose (f);""",
         """\t\tif (fscanf (f, \"%i\\n\", &version) != 1 ||
\t\t    version != 5 ||   /* SAVEGAME_VERSION is defined in host_cmd.c. */
\t\t    fscanf (f, \"%79s\\n\", name) != 1)
\t\t{
\t\t\tfclose (f);
\t\t\tcontinue;
\t\t}
\t\tstrncpy (m_filenames[i], name, sizeof(m_filenames[i])-1);
\t\tm_filenames[i][sizeof(m_filenames[i])-1] = 0;

\t// change _ back to space
\t\tfor (j=0 ; j<SAVEGAME_COMMENT_LENGTH ; j++)
\t\t\tif (m_filenames[i][j] == '_')
\t\t\t\tm_filenames[i][j] = ' ';
\t\tloadable[i] = true;
\t\tfclose (f);""", 'menu.c M_ScanSaves')
wro('menu.c', mn)

# cd_null.c : rebuilt as the music event producer of the release (verbatim)
cd = rdo('cd_null.c')
_hdr = cd[:cd.find('#include "quakedef.h"')]
cd = _hdr + '#include "quakedef.h"\n' + HDR + """
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

/* Console cd command, matching the original Quake syntax. */
static void CD_f(void)
{
    const char *c;
    if (Cmd_Argc() < 2) {
        Con_Printf("cd play <track> | loop <track> | stop | pause | resume\\n");
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
        Con_Printf("%u reads requested, most recent track %u\\n",
                   shared[SH_CD_PLAY], shared[SH_CD_TRACK]);
        return;
    }
    Con_Printf("cd: unrecognized subcommand\\n");
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
"""
wro('cd_null.c', cd)
print("  release patches : zone cvar host sv_main cmd cl_demo pr_cmds pr_edict menu cd_null")

# ---------------------------------------------------------------------------
# 4. the GLQUAKE blocks of id's common files (quakegeneric removed them)
# ---------------------------------------------------------------------------

# quakedef.h
q = rdo('quakedef.h')
q = rep(q, '#include "model.h"\n#include "d_iface.h"\n',
        '#ifdef GLQUAKE\n#include "gl_model.h"\n#else\n#include "model.h"\n#include "d_iface.h"\n#endif\n',
        'quakedef.h gl_model.h')
q = rep(q, '#include "cdaudio.h"\n',
        '#include "cdaudio.h"\n\n#ifdef GLQUAKE\n#include "glquake.h"\n#endif\n',
        'quakedef.h glquake.h')
wro('quakedef.h', q)

# cl_parse.c
cp = rdo('cl_parse.c')
cp = rep(cp, '\t\t\tforcelink = true;\t// hack to make null model players work\n',
         '\t\t\tforcelink = true;\t// hack to make null model players work\n'
         '#ifdef GLQUAKE\n'
         '\t\tif (num > 0 && num <= cl.maxclients)\n'
         '\t\t\tR_TranslatePlayerSkin (num - 1);\n'
         '#endif\n', 'cl_parse.c skin 1')
cp = rep(cp, '\n\tif (bits & U_SKIN)\n\t\tent->skinnum = MSG_ReadByte();\n\telse\n\t\tent->skinnum = ent->baseline.skin;\n',
         '#ifdef GLQUAKE\n'
         '\tif (bits & U_SKIN)\n'
         '\t\tskin = MSG_ReadByte();\n'
         '\telse\n'
         '\t\tskin = ent->baseline.skin;\n'
         '\tif (skin != ent->skinnum) {\n'
         '\t\tent->skinnum = skin;\n'
         '\t\tif (num > 0 && num <= cl.maxclients)\n'
         '\t\t\tR_TranslatePlayerSkin (num - 1);\n'
         '\t}\n'
         '\n'
         '#else\n'
         '\n\tif (bits & U_SKIN)\n\t\tent->skinnum = MSG_ReadByte();\n\telse\n\t\tent->skinnum = ent->baseline.skin;\n'
         '#endif\n', 'cl_parse.c skin 2')
cp = rep(cp, '\tbottom = (cl.scores[slot].colors &15)<<4;\n',
         '\tbottom = (cl.scores[slot].colors &15)<<4;\n'
         '#ifdef GLQUAKE\n'
         '\tR_TranslatePlayerSkin (slot);\n'
         '#endif\n', 'cl_parse.c skin 3')
# the GL branch of CL_ParseUpdate uses a local "skin": id declares it in the
# function's variable block ("int skin;" under GLQUAKE)
must(cp, 'void CL_ParseUpdate (int bits)\n{\n', 'cl_parse.c CL_ParseUpdate')
idcp = rd(os.path.join(IDQ, 'cl_parse.c'))
# id's declaration block of CL_ParseUpdate, taken as it is
blk_start = idcp.index('void CL_ParseUpdate (int bits)\n{\n')
blk_end = idcp.index('\n\n', blk_start)
id_decl = idcp[blk_start:blk_end]
qg_start = cp.index('void CL_ParseUpdate (int bits)\n{\n')
qg_end = cp.index('\n\n', qg_start)
qg_decl = cp[qg_start:qg_end]
if 'skin' not in id_decl:
    raise SystemExit("PATCH cl_parse.c: id's CL_ParseUpdate has no skin declaration")
cp = cp[:qg_start] + id_decl + cp[qg_end:]
wro('cl_parse.c', cp)

# r_part.c
rp = rdo('r_part.c')
rp = rep(rp, '\tD_StartParticles ();\n\n\tVectorScale (vright, xscaleshrink, r_pright);\n\tVectorScale (vup, yscaleshrink, r_pup);\n\tVectorCopy (vpn, r_ppn);\n',
         '#ifdef GLQUAKE\n'
         '\tvec3_t\t\t\tup, right;\n'
         '\tfloat\t\t\tscale;\n'
         '\n'
         '    GL_Bind(particletexture);\n'
         '\tglEnable (GL_BLEND);\n'
         '\tglTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);\n'
         '\tglBegin (GL_TRIANGLES);\n'
         '\n'
         '\tVectorScale (vup, 1.5, up);\n'
         '\tVectorScale (vright, 1.5, right);\n'
         '#else\n'
         '\tD_StartParticles ();\n\n\tVectorScale (vright, xscaleshrink, r_pright);\n\tVectorScale (vup, yscaleshrink, r_pup);\n\tVectorCopy (vpn, r_ppn);\n'
         '#endif\n', 'r_part.c begin')
rp = rep(rp, '\t\tD_DrawParticle (p);\n',
         '#ifdef GLQUAKE\n'
         '\t\t// hack a scale up to keep particles from disapearing\n'
         '\t\tscale = (p->org[0] - r_origin[0])*vpn[0] + (p->org[1] - r_origin[1])*vpn[1]\n'
         '\t\t\t+ (p->org[2] - r_origin[2])*vpn[2];\n'
         '\t\tif (scale < 20)\n'
         '\t\t\tscale = 1;\n'
         '\t\telse\n'
         '\t\t\tscale = 1 + scale * 0.004;\n'
         '\t\tglColor3ubv ((byte *)&d_8to24table[(int)p->color]);\n'
         '\t\tglTexCoord2f (0,0);\n'
         '\t\tglVertex3fv (p->org);\n'
         '\t\tglTexCoord2f (1,0);\n'
         '\t\tglVertex3f (p->org[0] + up[0]*scale, p->org[1] + up[1]*scale, p->org[2] + up[2]*scale);\n'
         '\t\tglTexCoord2f (0,1);\n'
         '\t\tglVertex3f (p->org[0] + right[0]*scale, p->org[1] + right[1]*scale, p->org[2] + right[2]*scale);\n'
         '#else\n'
         '\t\tD_DrawParticle (p);\n'
         '#endif\n', 'r_part.c particle')
rp = rep(rp, '\tD_EndParticles ();\n',
         '#ifdef GLQUAKE\n'
         '\tglEnd ();\n'
         '\tglDisable (GL_BLEND);\n'
         '\tglTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);\n'
         '#else\n'
         '\tD_EndParticles ();\n'
         '#endif\n', 'r_part.c end')
wro('r_part.c', rp)

# view.c : the four GL blocks, verbatim from id (taken from the id file so
# that the 150 lines are never retyped), placed where quakegeneric cut them
idv = rd(os.path.join(IDQ, 'view.c'))


def id_block(text, first_line, last_line):
    """the id text from the line starting with first_line up to and
    including the line equal to last_line (both unique)"""
    a = text.index(first_line)
    b = text.index(last_line, a) + len(last_line)
    return text[a:b]


vw = rdo('view.c')
blk1 = id_block(idv, '#ifdef\tGLQUAKE\nbyte\t\tramps[3][256];', '#endif\t// GLQUAKE')
vw = rep(vw, 'byte\t\tgammatable[256];\t// palette is sent through this\n',
         'byte\t\tgammatable[256];\t// palette is sent through this\n\n' + blk1 + '\n', 'view.c ramps')
blk2 = id_block(idv, '/*\n=============\nV_CalcBlend\n=============\n*/\n#ifdef\tGLQUAKE\nvoid V_CalcBlend (void)',
                '\tif (v_blend[3] < 0)\n\t\tv_blend[3] = 0;\n}\n#endif')
blk3 = id_block(idv, '#ifdef\tGLQUAKE\nvoid V_UpdatePalette (void)', '#endif\t// !GLQUAKE')
# quakegeneric kept id's "#else" variant of V_UpdatePalette: it is the
# text between "#else\t// !GLQUAKE\n" and "#endif\t// !GLQUAKE" of blk3
sw_variant = blk3[blk3.index('#else\t// !GLQUAKE\n') + len('#else\t// !GLQUAKE\n'):blk3.index('#endif\t// !GLQUAKE')]
vw = rep(vw, sw_variant, blk3, 'view.c V_UpdatePalette')
vw = rep(vw, '/*\n=============\nV_UpdatePalette\n=============\n*/\n',
         blk2 + '\n\n/*\n=============\nV_UpdatePalette\n=============\n*/\n', 'view.c V_CalcBlend')
vw = rep(vw, "\tif (crosshair.value)\n\t\tDraw_Character (scr_vrect.x + scr_vrect.width/2 + cl_crossx.value, \n\t\t\tscr_vrect.y + scr_vrect.height/2 + cl_crossy.value, '+');\n",
         "#ifndef GLQUAKE\n\tif (crosshair.value)\n\t\tDraw_Character (scr_vrect.x + scr_vrect.width/2 + cl_crossx.value, \n\t\t\tscr_vrect.y + scr_vrect.height/2 + cl_crossy.value, '+');\n#endif\n",
         'view.c crosshair')
wro('view.c', vw)
print("  GLQUAKE blocks : quakedef.h 2, cl_parse.c 3 (+ declarations), r_part.c 3, view.c 4")

# ---------------------------------------------------------------------------
# 5. the GL files
# ---------------------------------------------------------------------------
import importlib.util
spec = importlib.util.spec_from_file_location(
    'zzqgl_patch_gl', os.path.join(os.path.dirname(os.path.abspath(__file__)), 'zzqgl_patch_gl.py'))
mod = importlib.util.module_from_spec(spec)
spec.loader.exec_module(mod)
mod.patch_gl(OUT)
print("patch engine: done")
