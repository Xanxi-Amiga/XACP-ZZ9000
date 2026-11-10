"""zzqgl_patch_gl.py - ZZQuakeGL: the adjustments of id's GL renderer files.

Called by zzqgl_patch_engine.py on the COPIES in the build tree; can also
be run alone:  python3 zzqgl_patch_gl.py <dir with the gl_*.c copies>

Each change is an exact-match replacement that must occur once.  They are,
file by file:

  gl_model.c   COM_FileBase has three arguments in quakegeneric (buffer size).
  gl_draw.c    gl_max_size 256 unconditionally (the TMU of RasterIX loads
               256x256 at most; id only did it for 3dfx/Glide renderers).
               Console background version stamp without "%4.2f" (doubles
               through "..." on the 4 mod 8 Core1 stack; the release patched
               draw.c the same way), ZZQuakeGL's version in it.
               GL_LoadTexture: (a) the texture cache really keeps named
               textures (id's code forgot numgltextures++ on that path, so
               every map load re-uploaded everything under a new texture
               number: with the 2304 numbers of ZZGL that ends in a few
               map loads); (b) a same-named texture of another size is a
               new entry instead of Sys_Error("cache mismatch");
               (c) a cached texture that ZZGL dropped from the GPU pool
               is uploaded again under its number (zzqgl_tex_resident).
  gl_rsurf.c   lightmaps: ZZQGL_LM=q2 (default) stores the light value
               itself and blends (GL_ZERO, GL_SRC_COLOR), the path of
               Quake II's ref_gl validated on the Amiga since ZZQuake2RIX
               0.1; ZZQGL_LM=id keeps id's inverted lightmaps and
               (GL_ZERO, GL_ONE_MINUS_SRC_COLOR), a RasterIX blend factor
               never run on the hardware (ZZGL maps it, "not yet probed").
  gl_rmisc.c   two stack buffers made static (256 KiB of R_Envmap_f,
  gl_warp.c    64 KiB of R_InitSky): the release's Core1 stack is 1 MiB and
               starts 4 mod 8; same rule as zzq2rix_patch_refgl.py.
  gl_mesh.c    ZZQGL_NOMS2=1 only: the glquake/*.ms2 mesh cache is neither
               read nor written (every map load rebuilds the strips).
               Default: id's behaviour (the 68k file service serves the
               files when the glquake/ folder exists).

ASCII only.
"""
import os
import sys


def rd(path):
    return open(path, encoding='latin-1').read()


def wr(path, s):
    open(path, 'w', encoding='latin-1', newline='').write(s)


def rep(s, old, new, where):
    c = s.count(old)
    if c != 1:
        raise SystemExit("PATCH %s: expected 1 occurrence, found %d:\n%s"
                         % (where, c, old[:120]))
    return s.replace(old, new, 1)


def patch_gl(out):
    def p(f):
        return os.path.join(out, f)

    # ---- gl_model.c ----------------------------------------------------
    s = rd(p('gl_model.c'))
    s = rep(s, '\tCOM_FileBase (mod->name, loadname);',
            '\tCOM_FileBase (mod->name, loadname, 32);', 'gl_model.c COM_FileBase')
    wr(p('gl_model.c'), s)

    # ---- gl_draw.c -----------------------------------------------------
    s = rd(p('gl_draw.c'))
    s = rep(s, '\t// 3dfx can only handle 256 wide textures\n'
               '\tif (!Q_strncasecmp ((char *)gl_renderer, "3dfx",4) ||\n'
               '\t\tstrstr((char *)gl_renderer, "Glide"))\n'
               '\t\tCvar_Set ("gl_max_size", "256");\n',
            '\t// ZZQuakeGL: the RasterIX TMU loads 256x256 at most (id: 3dfx and Glide only)\n'
            '\tCvar_Set ("gl_max_size", "256");\n', 'gl_draw.c gl_max_size')
    s = rep(s, '''	// see if the texture is allready present
	if (identifier[0])
	{
		for (i=0, glt=gltextures ; i<numgltextures ; i++, glt++)
		{
			if (!strcmp (identifier, glt->identifier))
			{
				if (width != glt->width || height != glt->height)
					Sys_Error ("GL_LoadTexture: cache mismatch");
				return gltextures[i].texnum;
			}
		}
	}
	else {
		glt = &gltextures[numgltextures];
		numgltextures++;
	}
''', '''	// see if the texture is allready present
	// ZZQuakeGL: a same-named texture of another size is a new entry (id:
	// Sys_Error "cache mismatch"); a cached texture dropped from the GPU
	// pool is uploaded again under its number; named textures do enter the
	// cache (id forgot numgltextures++ on this path)
	if (identifier[0])
	{
		for (i=0, glt=gltextures ; i<numgltextures ; i++, glt++)
		{
			if (!strcmp (identifier, glt->identifier)
				&& width == glt->width && height == glt->height)
			{
				if (!zzqgl_tex_resident (glt->texnum))
				{
					GL_Bind (glt->texnum);
					GL_Upload8 (data, width, height, mipmap, alpha);
				}
				return gltextures[i].texnum;
			}
		}
	}
	if (numgltextures == MAX_GLTEXTURES)
		Sys_Error ("GL_LoadTexture: numgltextures == MAX_GLTEXTURES");
	glt = &gltextures[numgltextures];
	numgltextures++;
''', 'gl_draw.c GL_LoadTexture')
    # the version stamped on the console background: "(gl 1.00) 1.09" without the "%4.2f" of
    # sprintf (doubles through "..." on the release's 4 mod 8 stack: same reason as the release
    # patched draw.c), ZZQuakeGL's own version in place of GLQUAKE_VERSION (a define of id's
    # quakedef.h that quakegeneric removed)
    s = rep(s, '#if defined(__linux__)\n'
               '\tsprintf (ver, "(Linux %2.2f, gl %4.2f) %4.2f", (float)LINUX_VERSION, (float)GLQUAKE_VERSION, (float)VERSION);\n'
               '#else\n'
               '\tsprintf (ver, "(gl %4.2f) %4.2f", (float)GLQUAKE_VERSION, (float)VERSION);\n'
               '#endif\n',
            '\t{	// ZZQuakeGL: no "%f" through "..." on the Core1 stack\n'
            '\t\textern void zzq_ftoa_prec(char *dst, int cap, float v, int prec);\n'
            '\t\tchar vq[16];\n'
            '\t\tzzq_ftoa_prec (vq, sizeof(vq), (float)VERSION, 2);\n'
            '\t\tsprintf (ver, "%s", vq);	/* as the software release: the version alone */\n'
            '\t}\n', 'gl_draw.c version string')
    s = rep(s, 'int GL_LoadTexture (char *identifier, int width, int height, byte *data, qboolean mipmap, qboolean alpha)\n{\n',
            'extern int zzqgl_tex_resident (int texnum);	/* ZZQuakeGL: Core1/zzqgl_gl.c */\n\n'
            'int GL_LoadTexture (char *identifier, int width, int height, byte *data, qboolean mipmap, qboolean alpha)\n{\n',
            'gl_draw.c GL_LoadTexture prototype')
    wr(p('gl_draw.c'), s)

    # ---- gl_rsurf.c ----------------------------------------------------
    lm = os.environ.get('ZZQGL_LM', 'q2')
    s = rd(p('gl_rsurf.c'))
    if lm == 'q2':
        s = rep(s, '''	case GL_ALPHA:
	case GL_LUMINANCE:
	case GL_INTENSITY:
		bl = blocklights;
		for (i=0 ; i<tmax ; i++, dest += stride)
		{
			for (j=0 ; j<smax ; j++)
			{
				t = *bl++;
				t >>= 7;
				if (t > 255)
					t = 255;
				dest[j] = 255-t;
			}
		}
		break;
''', '''	case GL_ALPHA:
	case GL_LUMINANCE:
	case GL_INTENSITY:
		bl = blocklights;
		for (i=0 ; i<tmax ; i++, dest += stride)
		{
			for (j=0 ; j<smax ; j++)
			{
				t = *bl++;
				t >>= 7;
				if (t > 255)
					t = 255;
				dest[j] = t;	// ZZQuakeGL: not inverted, blended (GL_ZERO, GL_SRC_COLOR)
			}
		}
		break;
''', 'gl_rsurf.c R_BuildLightMap')
        c = s.count('glBlendFunc (GL_ZERO, GL_ONE_MINUS_SRC_COLOR);')
        if c != 2:
            raise SystemExit("PATCH gl_rsurf.c: expected 2 lightmap blend functions, found %d" % c)
        s = s.replace('glBlendFunc (GL_ZERO, GL_ONE_MINUS_SRC_COLOR);',
                      'glBlendFunc (GL_ZERO, GL_SRC_COLOR);	// ZZQuakeGL: ref_gl lightmap blend')
        print("  gl_rsurf.c : lightmaps not inverted, (GL_ZERO, GL_SRC_COLOR) [ZZQGL_LM=q2]")
    elif lm == 'id':
        print("  gl_rsurf.c : id lightmap path kept (GL_ZERO, GL_ONE_MINUS_SRC_COLOR) [ZZQGL_LM=id]")
    else:
        raise SystemExit("ZZQGL_LM must be q2 or id")
    wr(p('gl_rsurf.c'), s)

    # ---- stack buffers -------------------------------------------------
    s = rd(p('gl_rmisc.c'))
    s = rep(s, 'void R_Envmap_f (void)\n{\n\tbyte\tbuffer[256*256*4];',
            'void R_Envmap_f (void)\n{\n\tstatic byte\tbuffer[256*256*4];	// ZZQuakeGL: off the Core1 stack',
            'gl_rmisc.c R_Envmap_f buffer')
    wr(p('gl_rmisc.c'), s)
    s = rd(p('gl_warp.c'))
    s = rep(s, '\tunsigned\ttrans[128*128];\n\tunsigned\ttranspix;',
            '\tstatic unsigned\ttrans[128*128];	// ZZQuakeGL: off the Core1 stack\n\tunsigned\ttranspix;',
            'gl_warp.c R_InitSky trans')
    wr(p('gl_warp.c'), s)

    # ---- gl_mesh.c (option) --------------------------------------------
    if os.environ.get('ZZQGL_NOMS2', '0') == '1':
        s = rd(p('gl_mesh.c'))
        s = rep(s, '\tCOM_FOpenFile (cache, &f);\t\n\tif (f)\n',
                '\tf = NULL;	// ZZQuakeGL (ZZQGL_NOMS2): no mesh cache file\n\tif (f)\n',
                'gl_mesh.c cache read')
        s = rep(s, '\t\tf = fopen (fullpath, "wb");\n\t\tif (f)\n',
                '\t\tf = NULL;	// ZZQuakeGL (ZZQGL_NOMS2): no mesh cache file\n\t\tif (f)\n',
                'gl_mesh.c cache write')
        wr(p('gl_mesh.c'), s)
        print("  gl_mesh.c : glquake/*.ms2 cache disabled [ZZQGL_NOMS2=1]")
    else:
        print("  gl_mesh.c : id mesh cache kept (glquake/*.ms2 through the 68k file service)")
    print("  gl patches : gl_model gl_draw gl_rsurf gl_rmisc gl_warp")


if __name__ == '__main__':
    patch_gl(sys.argv[1])
