
"""
zzq_patch_engine.py - applies ZZQuake's diagnostic patches to COPIES of
the quakegeneric sources. The user's tree is never touched: the build
script copies the engine into build/engine and calls us on that copy.

Everything here is observational except CVARFIX, which is gated by a
shared slot and only alters R_Init when the 68k explicitly asks.

Patches:
  zone.c    Hunk_AllocName publishes the FAILING allocation's name,
            raw and adjusted size, and the hunk accounting, before
            calling Sys_Error. This is what finally names the culprit:
            the arithmetic already rules out "surfaces" (would be
            16 mod 64) and "edges" (16 mod 32), since 0x694871E0
            gives 32 and 0.
  model.c   sets zzq_alias_stage before each allocation of the alias
            model path, so a failure inside player.mdl says WHICH
            stage of the variable-length walk went wrong.
  r_main.c  snapshots r_maxsurfs/r_maxedges after R_Init and again in
            R_NewMap, and offers CVARFIX (Cvar_Set with a literal
            string instead of Cvar_SetValue, which goes through
            sprintf("%f")).
ASCII only.
"""
import sys, os, re

d = sys.argv[1]
def rd(f): return open(os.path.join(d, f), encoding='latin-1').read()
def wr(f, s): open(os.path.join(d, f), 'w', encoding='latin-1').write(s)

HDR = '''
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
'''


z = rd('zone.c')
old = """	if (size < 0)
		Sys_Error ("Hunk_Alloc: bad size: %i", size);

	size = sizeof(hunk_t) + ((size+15)&~15);
	
	if (hunk_size - hunk_low_used - hunk_high_used < size)
		Sys_Error ("Hunk_Alloc: failed on %i bytes",size);"""
new = """	if (size < 0)
		Sys_Error ("Hunk_Alloc: bad size: %i", size);

	{ int zzq_raw = size;
	size = sizeof(hunk_t) + ((size+15)&~15);
	
	if (hunk_size - hunk_low_used - hunk_high_used < size) {
		zzq_hunk_fail (name, zzq_raw, size,
		               hunk_low_used, hunk_high_used, hunk_size);
		Sys_Error ("Hunk_Alloc: failed on %i bytes",size);
	} }"""
if old not in z:
    
    pat = re.compile(r"size = sizeof\(hunk_t\) \+ \(\(size\+15\)&~15\);\s*\n\s*\n?\s*if \(hunk_size - hunk_low_used - hunk_high_used < size\)\s*\n\s*Sys_Error \(\"Hunk_Alloc: failed on %i bytes\",size\);")
    m = pat.search(z)
    if not m:
        print("PATCH ZONE.C: pattern not found", file=sys.stderr); sys.exit(1)
    z = z[:m.start()] + """{ int zzq_raw = size;
	size = sizeof(hunk_t) + ((size+15)&~15);
	if (hunk_size - hunk_low_used - hunk_high_used < size) {
		zzq_hunk_fail (name, zzq_raw, size,
		               hunk_low_used, hunk_high_used, hunk_size);
		Sys_Error ("Hunk_Alloc: failed on %i bytes",size);
	} }""" + z[m.end():]
else:
    z = z.replace(old, new)
z = z.replace('#include "quakedef.h"', '#include "quakedef.h"\n' + HDR, 1)
wr('zone.c', z)


m = rd('model.c')
m = m.replace('#include "quakedef.h"', '#include "quakedef.h"\n' + HDR, 1)
stages = [
    ("pheader = Hunk_AllocName (size, loadname);", 1),
    ("pskindesc = Hunk_AllocName (numskins * sizeof (maliasskindesc_t),", 2),
    ("pskin = Hunk_AllocName (skinsize, loadname);", 3),
    ("paliasskingroup = Hunk_AllocName (sizeof (maliasskingroup_t) +", 4),
    ("poutskinintervals = Hunk_AllocName (numskins * sizeof (float),loadname);", 5),
    ("pframe = Hunk_AllocName (numv * sizeof(*pframe), loadname);", 6),
    ("paliasgroup = Hunk_AllocName (sizeof (maliasgroup_t) +", 7),
    ("poutintervals = Hunk_AllocName (numframes * sizeof (float), loadname);", 8),
]
hit = 0
for needle, st in stages:
    if needle in m:
        m = m.replace(needle, "zzq_alias_stage = %d; %s" % (st, needle), 1)
        hit += 1
wr('model.c', m)






cv = rd('cvar.c')
old_sv = """void Cvar_SetValue (char *var_name, float value)
{
\tchar\tval[32];
\t
\tsprintf (val, "%f",value);"""
if old_sv in cv:
    cv = cv.replace(old_sv, """void Cvar_SetValue (char *var_name, float value)
{
\tchar\tval[32];
\t
\tzzq_ftoa (val, sizeof(val), value);""")
    cvfix = True
else:
    import re as _re
    m2 = _re.search(r'void Cvar_SetValue \(char \*var_name, float value\)\s*\{[^}]*?sprintf *\( *val *, *"%f" *, *value *\);', cv, _re.S)
    if m2:
        cv = cv[:m2.start()] + m2.group(0).replace('sprintf (val, "%f",value);', 'zzq_ftoa (val, sizeof(val), value);').replace('sprintf(val,"%f",value);', 'zzq_ftoa (val, sizeof(val), value);') + cv[m2.end():]
        cvfix = True
    else:
        cvfix = False
cv = cv.replace('#include "quakedef.h"', '#include "quakedef.h"\n' + HDR, 1)
wr('cvar.c', cv)





ho = rd('host.c')
old_cap = "if (!cls.timedemo && realtime - oldrealtime < 1.0/72.0)"
if old_cap in ho:
    ho = ho.replace(old_cap,
                    "if (!cls.timedemo && realtime - oldrealtime < zzq_min_frametime())")
    capfix = True
else:
    capfix = False
ho = ho.replace('#include "quakedef.h"', '#include "quakedef.h"\n' + HDR, 1)
wr('host.c', ho)
print("  host.c : configurable engine frame cap = %s" % capfix)

































vn = rd('vid_null.c')
_o = "\tvid.maxwarpwidth = vid.width = vid.conwidth = BASEWIDTH;\n\tvid.maxwarpheight = vid.height = vid.conheight = BASEHEIGHT;"
if _o in vn:
    vn = vn.replace(_o,
        "\tvid.width = vid.conwidth = BASEWIDTH;\n"
        "\tvid.height = vid.conheight = BASEHEIGHT;\n"
        "\t/* Warp limits describe the buffer, not the display. */\n"
        "\tvid.maxwarpwidth  = WARP_WIDTH;\n"
        "\tvid.maxwarpheight = WARP_HEIGHT;")
    print("  vid_null.c : maxwarp = WARP_WIDTH x WARP_HEIGHT")
else:
    raise SystemExit("PATCH: video init pattern not found")
wr('vid_null.c', vn)


rm2 = rd('r_misc.c')
rm2 = rm2.replace('#include "quakedef.h"', '#include "quakedef.h"\n' + HDR, 1)
_o2 = "\t\tif (r_dowarp)\n\t\t{"
if _o2 in rm2:
    rm2 = rm2.replace(_o2,
        "\t\tif (r_dowarp)\n\t\t{\n"
        "\t\t\t{ extern volatile unsigned int *shared;\n"
        "\t\t\t  shared[SH_WARP_VID]    = (unsigned int)vid.width\n"
        "\t\t\t                        | ((unsigned int)vid.height << 16);\n"
        "\t\t\t  shared[SH_WARP_MAX]    = (unsigned int)vid.maxwarpwidth\n"
        "\t\t\t                        | ((unsigned int)vid.maxwarpheight << 16);\n"
        "\t\t\t  shared[SH_WARP_CONST]  = (unsigned int)WARP_WIDTH\n"
        "\t\t\t                        | ((unsigned int)WARP_HEIGHT << 16);\n"
        "\t\t\t  shared[SH_WARP_SBLINES]= (unsigned int)sb_lines;\n"
        "\t\t\t  shared[SH_WARP_ACTIVE]++; }", 1)
    print("  r_misc.c : warp state published on activation")
wr('r_misc.c', rm2)


di2 = rd('d_init.c')
di2 = di2.replace('#include "quakedef.h"', '#include "quakedef.h"\n' + HDR, 1)
_o3 = "\t\td_viewbuffer = r_warpbuffer;"
if _o3 in di2:
    
    
    
    di2 = di2.replace(_o3,
        "\t\t{ d_viewbuffer = r_warpbuffer;\n" +
        "\t\t{ extern volatile unsigned int *shared;\n"
        "\t\t  shared[SH_WARP_VRECT] = (unsigned int)r_refdef.vrect.width\n"
        "\t\t                        | ((unsigned int)r_refdef.vrect.height << 16);\n"
        "\t\t  shared[SH_WARP_SCRW]  = (unsigned int)screenwidth;\n"
        "\t\t  shared[SH_WARP_ISBUF] = 1; } }")
    print("  d_init.c : final vrect and screenwidth published")
wr('d_init.c', di2)










import os
if os.environ.get('ZZQ_HIRES') == '1':
    v = rd('vid_null.c')
    v = v.replace('#define\tBASEWIDTH\t320', '#define\tBASEWIDTH\t640')
    v = v.replace('#define\tBASEHEIGHT\t240', '#define\tBASEHEIGHT\t480')
    wr('vid_null.c', v)
    print('  vid_null.c : 640x480')
    
    
    
    
    
    
    
    
    
    g = rd('quakegeneric.h')
    g = g.replace('#define QUAKEGENERIC_RES_X 320',
                  '#ifndef QUAKEGENERIC_RES_X\n#define QUAKEGENERIC_RES_X 320\n#endif')
    g = g.replace('#define QUAKEGENERIC_RES_Y 240',
                  '#ifndef QUAKEGENERIC_RES_Y\n#define QUAKEGENERIC_RES_Y 240\n#endif')
    wr('quakegeneric.h', g)
    print('  quakegeneric.h : RES_X/Y overrideable')
    
    
    
    
    
    _di = rd('d_iface.h')
    _di = _di.replace('#define WARP_WIDTH\t\t320', '#define WARP_WIDTH\t\t640')
    _di = _di.replace('#define WARP_HEIGHT\t\t200', '#define WARP_HEIGHT\t\t480')
    wr('d_iface.h', _di)
    
    
    
    
    
    
    
    
    
    
    
    print('  warpbuffer : remains on stack, 320x200 = 64000 bytes')

    
    
    
    
    
    
    
    
    
    qh = rd('quakegeneric.h')
    if '#ifndef QUAKEGENERIC_RES_X' not in qh:
        qh = qh.replace('#define QUAKEGENERIC_RES_X 320',
                        '#ifndef QUAKEGENERIC_RES_X\n'
                        '#define QUAKEGENERIC_RES_X 320\n'
                        '#endif')
        qh = qh.replace('#define QUAKEGENERIC_RES_Y 240',
                        '#ifndef QUAKEGENERIC_RES_Y\n'
                        '#define QUAKEGENERIC_RES_Y 240\n'
                        '#endif')
        wr('quakegeneric.h', qh)
        print('  quakegeneric.h : RES_X/Y overrideable with -D')


















ds = rd('d_scan.c')
_n = ds.count("\t\t\t\t*pdest++ = *(pbase + (s >> 16) + (t >> 16) * cachewidth);")
if _n != 1:
    raise SystemExit("OPT FAIL: hot loop expected once, found %d" % _n)
ds = ds.replace("\tpbase = (unsigned char *)cacheblock;",
                "\tpbase = (unsigned char *)cacheblock;\n"
                "\t{ int zzq_cw = cachewidth; (void)zzq_cw; }", 1)
ds = ds.replace("\t\t\t\t*pdest++ = *(pbase + (s >> 16) + (t >> 16) * cachewidth);",
                "\t\t\t\t*pdest++ = *(pbase + (s >> 16) + (t >> 16) * zzq_cw);")

ds = ds.replace("\tpbase = (unsigned char *)cacheblock;\n\t{ int zzq_cw = cachewidth; (void)zzq_cw; }",
                "\tpbase = (unsigned char *)cacheblock;\n\tzzq_cw = cachewidth;")
ds = ds.replace("\tsstep = 0;\t// keep compiler happy",
                "\tzzq_cw = 0;\t// invariant du bloc, mis en locale\n"
                "\tsstep = 0;\t// keep compiler happy")


_d0 = "\tfixed16_t\t\ts, t, snext, tnext, sstep, tstep;"
if _d0 not in ds:
    raise SystemExit("OPT FAIL: declaration not found")
ds = ds.replace(_d0, _d0 + "\n\tint\t\t\tzzq_cw;", 1)








wr('d_scan.c', ds)
print('  d_scan.c : cachewidth en locale zzq_cw')








_prof = {}

def _sub_n(txt, old, new, n, tag):
    c = txt.count(old)
    if c != n:
        raise SystemExit("PROFILER FAIL: %s expected %d occurrence(s), "
                         "found %d" % (tag, n, c))
    _prof[tag] = c
    return txt.replace(old, new)

rm = rd('r_main.c')


rm = _sub_n(rm, "\tR_SetupFrame ();",
            "\tZZQ_PH(0);\n\tR_SetupFrame ();", 1, "PH_SETUP begin")


rm = _sub_n(rm, "#endif\n\n// make FDIV fast.",
            "#endif\n\tZZQ_PH_END(0);\n\n// make FDIV fast.",
            1, "PH_SETUP end")


rm = _sub_n(rm, "\tR_RenderWorld ();",
            "\tZZQ_PH(1);\n\tR_RenderWorld ();\n\tZZQ_PH_END(1);",
            1, "PH_WORLD")


rm = _sub_n(rm, "\tR_DrawBEntitiesOnList ();",
            "\tZZQ_PH(2);\n\tR_DrawBEntitiesOnList ();\n\tZZQ_PH_END(2);",
            1, "PH_BRUSH")


rm = _sub_n(rm, "\t\tR_ScanEdges ();",
            "\t\t{ ZZQ_PH(3); R_ScanEdges (); ZZQ_PH_END(3); }",
            2, "PH_SCAN")


rm = _sub_n(rm, "\tR_DrawEntitiesOnList ();",
            "\tZZQ_PH(5);\n\tR_DrawEntitiesOnList ();\n\tZZQ_PH_END(5);",
            1, "PH_ENT")
rm = _sub_n(rm, "\tR_DrawViewModel ();",
            "\tZZQ_PH(6);\n\tR_DrawViewModel ();\n\tZZQ_PH_END(6);",
            1, "PH_VIEWMDL")
rm = _sub_n(rm, "\tR_DrawParticles ();",
            "\tZZQ_PH(7);\n\tR_DrawParticles ();\n\tZZQ_PH_END(7);",
            1, "PH_PARTIC")
wr('r_main.c', rm)



re_ = rd('r_edge.c')


if 'ZZQ_PH_END' not in re_.split('D_DrawSurfaces')[0]:
    re_ = re_.replace('#include "quakedef.h"', '#include "quakedef.h"' + chr(10) + HDR, 1)
re_ = _sub_n(re_, "D_DrawSurfaces ();",
             "{ ZZQ_PH(4); D_DrawSurfaces (); ZZQ_PH_END(4); }",
             2, "PH_SURF")
wr('r_edge.c', re_)






de = rd('d_edge.c')
if 'ZZQ_S2_END' not in de.split('D_DrawSurfaces')[0]:
    de = de.replace('#include "quakedef.h"', '#include "quakedef.h"\n' + HDR, 1)






de = _sub_n(de, "\t\t\t\tif (!r_skymade)\n\t\t\t\t{\n\t\t\t\t\tR_MakeSky ();\n\t\t\t\t}\n\n\t\t\t\tD_DrawSkyScans8 (s->spans);",
            "\t\t\t\tZZQ_S2(4);\n"
            "\t\t\t\tif (!r_skymade)\n\t\t\t\t{\n\t\t\t\t\tR_MakeSky ();\n\t\t\t\t}\n\n"
            "\t\t\t\tD_DrawSkyScans8 (s->spans);\n"
            "\t\t\t\tZZQ_S2_END(4);\n"
            "\t\t\t\tzzq_s2_count(1, s->spans);", 1, "S2 SKY")


de = _sub_n(de, "\t\t\t\tpcurrentcache = D_CacheSurface (pface, miplevel);",
            "\t\t\t\tZZQ_S2(0);\n"
            "\t\t\t\tpcurrentcache = D_CacheSurface (pface, miplevel);\n"
            "\t\t\t\tZZQ_S2_END(0);", 1, "S2 CACHE")




de = _sub_n(de, "\t\t\t\tD_CalcGradients (pface);",
            "\t\t\t\tZZQ_S2(1);\n"
            "\t\t\t\tD_CalcGradients (pface);\n"
            "\t\t\t\tZZQ_S2_END(1);", 2, "S2 GRAD")


de = _sub_n(de, "\t\t\t\tD_DrawSolidSurface (s, (int)r_clearcolor.value & 0xFF);",
            "\t\t\t\tZZQ_S2(6);\n"
            "\t\t\t\tD_DrawSolidSurface (s, (int)r_clearcolor.value & 0xFF);\n"
            "\t\t\t\tZZQ_S2_END(6);\n"
            "\t\t\t\tzzq_s2_count(3, s->spans);", 1, "S2 OTHER")


de = _sub_n(de, "\t\t\t\t(*d_drawspans) (s->spans);",
            "\t\t\t\tZZQ_S2(2);\n"
            "\t\t\t\t(*d_drawspans) (s->spans);\n"
            "\t\t\t\tZZQ_S2_END(2);\n"
            "\t\t\t\tzzq_s2_count(0, s->spans);", 1, "S2 TEX")


de = _sub_n(de, "\t\t\t\tTurbulent8 (s->spans);",
            "\t\t\t\tZZQ_S2(4);\n"
            "\t\t\t\tTurbulent8 (s->spans);\n"
            "\t\t\t\tZZQ_S2_END(4);\n"
            "\t\t\t\tzzq_s2_count(2, s->spans);", 1, "S2 SPECIAL")


_nz = de.count("D_DrawZSpans (s->spans);")
de = _sub_n(de, "D_DrawZSpans (s->spans);",
            "{ ZZQ_S2(3); D_DrawZSpans (s->spans); ZZQ_S2_END(3); }",
            _nz, "S2 Z")










de = _sub_n(de, "\t\t\t\t\tcurrententity = s->entity;\t//FIXME: make this passed in to",
            "\t\t\t\t\tZZQ_S2(5);\n"
            "\t\t\t\t\tcurrententity = s->entity;\t//FIXME: make this passed in to",
            2, "S2 SUBMODL setup")
de = _sub_n(de, "\t\t\t\t\tR_RotateBmodel ();\t// FIXME: don't mess with the frustum,\n"
                "\t\t\t\t\t\t\t\t\t\t// make entity passed in\n\t\t\t\t}",
            "\t\t\t\t\tR_RotateBmodel ();\t// FIXME: don't mess with the frustum,\n"
            "\t\t\t\t\t\t\t\t\t\t// make entity passed in\n"
            "\t\t\t\t\tZZQ_S2_END(5);\n"
            "\t\t\t\t\tzzq_s2_count(4, s->spans);\n\t\t\t\t}",
            2, "S2 SUBMODL end")

de = _sub_n(de, "\t\t\t\t\tcurrententity = &cl_entities[0];",
            "\t\t\t\t\tZZQ_S2(5);\n"
            "\t\t\t\t\tcurrententity = &cl_entities[0];",
            2, "S2 SUBMODL restore")
de = _sub_n(de, "\t\t\t\t\tR_TransformFrustum ();",
            "\t\t\t\t\tR_TransformFrustum ();\n\t\t\t\t\tZZQ_S2_END(5);",
            2, "S2 SUBMODL restore end")

wr('d_edge.c', de)

print('ZZQ profiler instrumentation:')
for k in ("PH_SETUP begin", "PH_SETUP end", "PH_WORLD", "PH_BRUSH",
          "PH_SCAN", "PH_SURF", "PH_ENT", "PH_VIEWMDL", "PH_PARTIC",
          "S2 CACHE", "S2 GRAD", "S2 TEX", "S2 Z", "S2 SPECIAL",
          "S2 SUBMODL setup", "S2 SUBMODL end", "S2 SUBMODL restore",
          "S2 SUBMODL restore end", "S2 OTHER", "COUNT SKY"):
    print('  %-16s %d' % (k, _prof.get(k, 0)))


def _calls(fn, pat):
    txt = rd(fn)
    return sum(1 for l in txt.split(chr(10))
               if pat in l and '#define' not in l and 'extern' not in l)
print('  real audit, excluding macros:')
print('    r_main.c ZZQ_PH   : %d calls' % _calls('r_main.c', 'ZZQ_PH('))
print('    r_edge.c ZZQ_PH   : %d calls' % _calls('r_edge.c', 'ZZQ_PH('))
print('    d_edge.c ZZQ_S2   : %d calls' % _calls('d_edge.c', 'ZZQ_S2('))
print('    d_edge.c s2_count : %d calls' % _calls('d_edge.c', 'zzq_s2_count('))


_kn = ('NORMAL', 'SKY', 'TURB', 'BACK', 'SUBMODEL')
for _k in range(5):
    _n = _calls('d_edge.c', 'zzq_s2_count(%d,' % _k)
    print('      kind %d %-9s : %d' % (_k, _kn[_k], _n))
    if _n == 0:
        raise SystemExit("PROFILER FAIL: no zzq_s2_count(%d) - "
                         "category %s never counted" % (_k, _kn[_k]))






sc = rd('screen.c')
if 'ZZQuake diagnostic hooks' not in sc:
    sc = sc.replace('#include "quakedef.h"', '#include "quakedef.h"\n' + HDR, 1)

_MS = "\n\t{ extern volatile unsigned int *shared;" \
      " shared[SH_MODAL_STAGE] = %d; }"

_o = "\tscr_notifystring = text;"
if sc.count(_o) != 1:
    raise SystemExit("MODAL FAIL: entry point not found")
sc = sc.replace(_o, _o + (_MS % 1), 1)

_o = "\tscr_drawdialog = true;\n\tSCR_UpdateScreen ();"
if sc.count(_o) != 1:
    raise SystemExit("MODAL FAIL: SCR_UpdateScreen not found")
sc = sc.replace(_o,
    "\tscr_drawdialog = true;" + (_MS % 2) +
    "\n\tSCR_UpdateScreen ();" + (_MS % 3), 1)

_o = "\tS_ClearBuffer ();\t\t// so dma doesn't loop current sound"
if sc.count(_o) != 1:
    raise SystemExit("MODAL FAIL: S_ClearBuffer not found")
sc = sc.replace(_o, _o + (_MS % 4), 1)


_o = "\t\tkey_count = -1;\t\t// wait for a key down and up\n\t\tSys_SendKeyEvents ();"
if sc.count(_o) != 1:
    raise SystemExit("MODAL FAIL: loop not found")
sc = sc.replace(_o, _o +
    "\n\t\t{ extern volatile unsigned int *shared;"
    " shared[SH_MODAL_LOOPS]++;"
    " shared[SH_MODAL_LASTKEY] = (unsigned int)key_lastpress; }", 1)

_o = "\t} while (key_lastpress != 'y' && key_lastpress != 'n'" \
     " && key_lastpress != K_ESCAPE);"
if sc.count(_o) != 1:
    raise SystemExit("MODAL FAIL: loop end not found")
sc = sc.replace(_o, _o + (_MS % 5), 1)
wr('screen.c', sc)
print('  screen.c : modal stages 1..5, loop instrumented')






_stg = {}

def _stage(txt, old, new, n, tag):
    c = txt.count(old)
    if c != n:
        raise SystemExit("STAGE FAIL: %s expected %d, found %d"
                         % (tag, n, c))
    _stg[tag] = c
    return txt.replace(old, new)

_SW = "\n\t{ extern volatile unsigned int *shared; shared[SH_STAGE] = %d; }"

hs = rd('host.c')
if 'ZZQuake diagnostic hooks' not in hs:
    hs = hs.replace('#include "quakedef.h"', '#include "quakedef.h"\n' + HDR, 1)
hs = _stage(hs, "void Host_ShutdownServer(qboolean crash)\n{",
            "void Host_ShutdownServer(qboolean crash)\n{"
            + (_SW % 1), 1, "SHUTSRV_IN")
hs = _stage(hs, "void Host_ClearMemory (void)\n{",
            "void Host_ClearMemory (void)\n{" + (_SW % 3), 1, "CLEARMEM")
wr('host.c', hs)

sv = rd('sv_main.c')
if 'ZZQuake diagnostic hooks' not in sv:
    sv = sv.replace('#include "quakedef.h"', '#include "quakedef.h"\n' + HDR, 1)
sv = _stage(sv, "void SV_SpawnServer (char *server)\n{",
            "void SV_SpawnServer (char *server)\n{" + (_SW % 4),
            1, "SPAWN_IN")
wr('sv_main.c', sv)

print('ZZQ stage probes:')
for k in ("SHUTSRV_IN", "CLEARMEM", "SPAWN_IN"):
    print('  %-12s %d' % (k, _stg.get(k, 0)))






cd = rd('cd_null.c')
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

/* Commande console "cd", absente de quakegeneric : cd_null.c ne
   l'enregistrait pas, d'ou "unknown command cd". Syntaxe d'origine
   de Quake. */
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
        Con_Printf("%u lectures demandees, derniere piste %u\\n",
                   shared[SH_CD_PLAY], shared[SH_CD_TRACK]);
        return;
    }
    Con_Printf("cd : sous-commande inconnue\\n");
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
wr('cd_null.c', cd)
print("  cd_null.c : rebuilt as event producer")







ds = rd('d_scan.c')
ds = ds.replace('#include "quakedef.h"', '#include "quakedef.h"\n' + HDR, 1)

_o = "void D_WarpScreen (void)\n{"
if _o in ds:
    ds = ds.replace(_o,
        "void D_WarpScreen (void)\n{\n"
        "\t{ extern volatile unsigned int *shared; unsigned int _sp, _lr;\n"
        "\t  __asm__ volatile(\"mov %0, sp\" : \"=r\"(_sp));\n"
        "\t  __asm__ volatile(\"mov %0, lr\" : \"=r\"(_lr));\n"
        "\t  shared[SH_W_ENTER]++;\n"
        "\t  shared[SH_W_SP_ENTER] = _sp;\n"
        "\t  shared[SH_W_LR_ENTER] = _lr;\n"
        "\t  shared[SH_WATER_VRECT] = (unsigned int)r_refdef.vrect.width\n"
        "\t         | ((unsigned int)r_refdef.vrect.height << 16);\n"
        "\t  shared[SH_WATER_STAGE] = 2; }", 1)
    _j = ds.find("void D_WarpScreen (void)")
    _k = ds.find("\n}\n", _j)
    ds = ds[:_k] + ("\n\t{ extern volatile unsigned int *shared; unsigned int _sp;\n"
                    "\t  __asm__ volatile(\"mov %0, sp\" : \"=r\"(_sp));\n"
                    "\t  shared[SH_W_EXIT]++;\n"
                    "\t  shared[SH_W_SP_EXIT] = _sp; }") + ds[_k:]
    print("  d_scan.c : D_WarpScreen entry/exit with SP/LR")

_o2 = "\tr_turb_turb = sintable + ((int)(cl.time*SPEED)&(CYCLE-1));"
if _o2 in ds:
    ds = ds.replace(_o2,
        "\t{ extern volatile unsigned int *shared;\n"
        "\t  int _i = ((int)(cl.time*SPEED)&(CYCLE-1));\n"
        "\t  shared[SH_WATER_TURB]++;\n"
        "\t  shared[SH_WATER_TURBIDX] = (unsigned int)_i;\n"
        "\t  shared[SH_WATER_CLTIME]  = (unsigned int)(cl.time * 1000.0);\n"
        "\t  shared[SH_WATER_STAGE]   = 1;\n"
        "\t  r_turb_turb = sintable + _i; }")
    print("  d_scan.c : Turbulent8 preserved")
wr('d_scan.c', ds)

di = rd('d_init.c')
di = di.replace('#include "quakedef.h"', '#include "quakedef.h"\n' + HDR, 1)
_o3 = "\t\t\t\td_drawspans = D_DrawSpans8;"
if _o3 in di:
    di = di.replace(_o3, _o3 +
        "\n\t\t\t\t{ extern volatile unsigned int *shared;\n"
        "\t\t\t\t  shared[SH_DDS_AFTER_SETUP] = (unsigned int)d_drawspans;\n"
        "\t\t\t\t  shared[SH_DDS_EXPECTED] = (unsigned int)D_DrawSpans8; }")
    print("  d_init.c : d_drawspans published after D_SetupFrame")
wr('d_init.c', di)

de = rd('d_edge.c')


if 'ZZQuake diagnostic hooks' not in de:
    de = de.replace('#include "quakedef.h"', '#include "quakedef.h"\n' + HDR, 1)
_o4 = "\t\t\t\t(*d_drawspans) (s->spans);"
if _o4 in de:
    de = de.replace(_o4,
        "\t\t\t\t{ extern volatile unsigned int *shared;\n"
        "\t\t\t\t  unsigned int _p = (unsigned int)d_drawspans;\n"
        "\t\t\t\t  shared[SH_DDS_BEFORE_CALL] = _p;\n"
        "\t\t\t\t  if (shared[SH_DDS_EXPECTED] && _p != shared[SH_DDS_EXPECTED])\n"
        "\t\t\t\t      shared[SH_DDS_MISMATCH]++; }\n"
        + _o4)
    print("  d_edge.c : d_drawspans published immediately before indirect call")
wr('d_edge.c', de)





cm = rd('cmd.c')
old_ex = "void\tCmd_ExecuteString (char *text, cmd_source_t src)\n{"
if old_ex in cm:
    cm = cm.replace(old_ex, old_ex + "\n\tzzq_note_command (text);", 1)
    cmdfix = True
else:
    import re as _re3
    m3 = _re3.search(r'void\s+Cmd_ExecuteString\s*\([^)]*\)\s*\{', cm)
    if m3:
        cm = cm[:m3.end()] + "\n\tzzq_note_command (text);" + cm[m3.end():]
        cmdfix = True
    else:
        cmdfix = False
cm = cm.replace('#include "quakedef.h"', '#include "quakedef.h"\n' + HDR, 1)
wr('cmd.c', cm)
print("  cmd.c : command trace = %s" % cmdfix)




vn = rd('vid_null.c')
old_sc = "\tsurfcache = malloc(surfcache_size);"
if old_sc in vn:
    vn = vn.replace(old_sc, """\tsurfcache = malloc(surfcache_size);
\tzzq_surfcache_got (surfcache ? (int)surfcache_size : 0);
\tif (!surfcache)
\t\tSys_Error ("ZZQ: surface cache malloc failed (%d bytes)",
\t\t           (int)surfcache_size);""")
    scfix = True
else:
    scfix = False
vn = vn.replace('#include "quakedef.h"', '#include "quakedef.h"\n' + HDR, 1)
wr('vid_null.c', vn)
print("  vid_null.c : surfcache malloc check = %s" % scfix)





cd_ = rd('cl_demo.c')
old_td = 'Con_Printf ("%i frames %5.1f seconds %5.1f fps\\n", frames, time, frames/time);'
if old_td in cd_:
    
    
    
    
    
    
    
    cd_ = cd_.replace(old_td,
        '{ char tb[32], fb[32];\n'
        '\t\tzzq_timedemo_result (frames, time);\n'
        '\t\tzzq_ftoa_prec (tb, sizeof(tb), time, 1);\n'
        '\t\tzzq_ftoa_prec (fb, sizeof(fb), (float)frames / time, 1);\n'
        '\t\tCon_Printf ("%i frames %s seconds %s fps\\n",'
        ' frames, tb, fb); }')
    tdfix = True
else:
    tdfix = False
cd_ = cd_.replace('#include "quakedef.h"', '#include "quakedef.h"\n' + HDR, 1)
wr('cl_demo.c', cd_)
print("  cl_demo.c : timedemo result export = %s" % tdfix)









for fname, pairs in [
    ('draw.c',     [('sprintf (ver, "%4.2f", VERSION);',
                     'zzq_ftoa_prec (ver, sizeof(ver), (float)VERSION, 2);')]),
    ('pr_cmds.c',  [('sprintf (pr_string_temp, "%5.1f",v);',
                     'zzq_ftoa_prec (pr_string_temp, 128, v, 1);')]),
    
    
    
    
    ('pr_edict.c', [('sprintf (line, "%f", val->_float);',
                     'zzq_ftoa_prec (line, 128, val->_float, 6);'),
                    ('sprintf (line, "%f %f %f", val->vector[0], val->vector[1], val->vector[2]);',
                     '{ char _a[40], _b[40], _c[40];\n'
                     '\t\tzzq_ftoa_prec(_a, 40, val->vector[0], 6);\n'
                     '\t\tzzq_ftoa_prec(_b, 40, val->vector[1], 6);\n'
                     '\t\tzzq_ftoa_prec(_c, 40, val->vector[2], 6);\n'
                     '\t\tsprintf (line, "%s %s %s", _a, _b, _c); }'),
                    ('sprintf (line, "%5.1f", val->_float);',
                     'zzq_ftoa_prec (line, 128, val->_float, 1);'),
                    ('sprintf (line, "%f", val->_float);',
                     'zzq_ftoa_prec (line, 128, val->_float, 6);')]),
]:
    try:
        s = rd(fname)
    except Exception:
        continue
    changed = 0
    for a, b in pairs:
        if a in s:
            s = s.replace(a, b); changed += 1
    if changed:
        s = s.replace('#include "quakedef.h"', '#include "quakedef.h"\n' + HDR, 1)
        wr(fname, s)
    print("  %s : %d format(s) flottant(s) remplace(s)" % (fname, changed))


r = rd('r_main.c')
r = r.replace('#include "quakedef.h"', '#include "quakedef.h"\n' + HDR, 1)
old_init = """	Cvar_SetValue ("r_maxedges", (float)NUMSTACKEDGES);
	Cvar_SetValue ("r_maxsurfs", (float)NUMSTACKSURFACES);"""
new_init = """	if (zzq_cvarfix_enabled()) {
		/* CVARFIX: set the strings directly, bypassing
		   Cvar_SetValue -> sprintf("%f") -> Q_atof. */
		Cvar_Set ("r_maxedges", "2400");
		Cvar_Set ("r_maxsurfs", "800");
	} else {
		Cvar_SetValue ("r_maxedges", (float)NUMSTACKEDGES);
		Cvar_SetValue ("r_maxsurfs", (float)NUMSTACKSURFACES);
	}
	zzq_cvar_snapshot (1);"""
ok_init = old_init in r
if ok_init:
    r = r.replace(old_init, new_init)
old_new = "	r_cnumsurfs = r_maxsurfs.value;"
ok_new = old_new in r
if ok_new:
    r = r.replace(old_new, "	zzq_cvar_snapshot (2);\n" + old_new, 1)
wr('r_main.c', r)

print("patch engine: zone.c OK, model.c %d/%d stages, r_main.c init=%s newmap=%s, cvar.c ftoa=%s"
      % (hit, len(stages), ok_init, ok_new, cvfix))






mn = rd('menu.c')
old_ms = """\t\tfscanf (f, \"%i\\n\", &version);
\t\tfscanf (f, \"%79s\\n\", name);
\t\tstrncpy (m_filenames[i], name, sizeof(m_filenames[i])-1);

\t// change _ back to space
\t\tfor (j=0 ; j<SAVEGAME_COMMENT_LENGTH ; j++)
\t\t\tif (m_filenames[i][j] == '_')
\t\t\t\tm_filenames[i][j] = ' ';
\t\tloadable[i] = true;
\t\tfclose (f);"""
new_ms = """\t\tif (fscanf (f, \"%i\\n\", &version) != 1 ||
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
\t\tfclose (f);"""
if old_ms not in mn:
    raise SystemExit("PATCH: M_ScanSaves pattern not found")
mn = mn.replace(old_ms, new_ms, 1)
wr('menu.c', mn)
print("  menu.c : M_ScanSaves valide fscanf/version et termine la chaine")
