#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
#
# zzq2_patch_vidmenu.py - ZZQuake2 (A0 0.8.5, TGL 0.6.1, RIX 0.10.3):
# patched copy of other/vid_menu.c, the in-game Video menu.
#
#   usage: zzq2_patch_vidmenu.py <quake2generic dir> <output file>
#
# The quake2generic tree is NEVER modified: the upstream file is read,
# changed in memory, written to the build directory.
#
# Why. The upstream Video menu offers a driver (software, X11, OpenGL,
# SDL), a video mode (320x240 to 1600x1200), "fullscreen", and in the
# OpenGL menu "texture quality" and "8-bit textures". On the ZZ9000 a
# blob has ONE renderer, linked in (VID_LoadRefresh loads that one
# whatever vid_ref says), and ONE screen size, opened by the launcher
# and checked by Core1 before the first frame. "Apply" with another
# mode makes ref_soft change its frame size under the screen of the
# launcher, and makes ref_gl restart the renderer (gl_mode or
# vid_fullscreen modified -> vid_ref modified -> R_Shutdown / R_Init),
# which TinyGL and the RasterIX GPU were never meant to go through.
#
# The menu keeps what can change while the game runs:
#   software (A0):   screen size, brightness, stipple alpha
#   OpenGL (TGL, RIX): screen size
#   both:            reset to default, apply
# "apply" sets only vid_gamma and sw_stipplealpha, and only in the
# software menu. Driver, video mode, fullscreen, windowed mouse,
# texture quality and 8-bit textures are no longer in the menus and
# "apply" no longer writes vid_ref, sw_mode, gl_mode, vid_fullscreen,
# gl_picmip, gl_ext_palettedtexture or _windowed_mouse.
#
# The menu shown is the one of the renderer of the blob: the glue says
# which (zzq2_lock_video(1) in ZZQuake2TGL and ZZQuake2RIX, whose vid_ref
# keeps the default "soft" while ref_gl is the renderer linked in).
#
# zzq2_lock_video() (new, called by the glue once Quake2_Init has
# returned) closes the console route too: vid_ref, sw_mode, gl_mode
# and vid_fullscreen become write protected (CVAR_NOSET: "sw_mode is
# write protected."), and the command vid_restart is removed. The
# values in force are the ones the blob started with; config.cfg is
# read before, as before.
#
# STRICT: every anchor must be found exactly once, otherwise the script
# stops and nothing is built. ASCII only.

import os
import sys


def die(msg):
    raise SystemExit("zzq2_patch_vidmenu: " + msg)


def sub1(txt, old, new, tag):
    if txt.count(old) != 1:
        die("%s: pattern found %d times (need 1)" % (tag, txt.count(old)))
    print("  vid_menu.c : %s" % tag)
    return txt.replace(old, new)


def span(txt, start, end, new, tag):
    """Replace txt[start : end) (start included, end excluded)."""
    if txt.count(start) != 1 or txt.count(end) != 1:
        die("%s: anchors found %d / %d times (need 1 / 1)"
            % (tag, txt.count(start), txt.count(end)))
    a, b = txt.index(start), txt.index(end)
    if b <= a:
        die("%s: anchors in the wrong order" % tag)
    print("  vid_menu.c : %s" % tag)
    return txt[:a] + new + txt[b:]


def main():
    if len(sys.argv) != 3:
        die("usage: zzq2_patch_vidmenu.py <quake2generic dir> <output file>")
    q2, out = sys.argv[1], sys.argv[2]
    s = open(os.path.join(q2, "other", "vid_menu.c"), encoding="latin-1").read()

    # 1. apply: only what can change while the game runs
    s = span(s,
             '\tCvar_SetValue( "vid_gamma", gamma );\n'
             '\tCvar_SetValue( "sw_stipplealpha", s_stipple_box.curvalue );\n',
             '#if 0\n'
             '\t/*\n'
             '\t** update appropriate stuff if we\'re running OpenGL and gamma\n',
             '\t/* ZZQ2: one renderer and one screen size per blob. Only the\n'
             '\t   brightness and the stipple alpha of the software menu are\n'
             '\t   applied; vid_ref, sw_mode, gl_mode, vid_fullscreen,\n'
             '\t   gl_picmip, gl_ext_palettedtexture and _windowed_mouse are\n'
             '\t   left as they are (Core1/zzq2_patch_vidmenu.py). */\n'
             '\tif ( s_current_menu_index == SOFTWARE_MENU )\n'
             '\t{\n'
             '\t\tCvar_SetValue( "vid_gamma", gamma );\n'
             '\t\tCvar_SetValue( "sw_stipplealpha", s_stipple_box.curvalue );\n'
             '\t}\n'
             '\n',
             "apply: brightness and stipple alpha (software) only")

    # 2. the items of the two menus
    s = span(s,
             '\tMenu_AddItem( &s_software_menu, ( void * ) &s_ref_list[SOFTWARE_MENU] );\n',
             '\tMenu_Center( &s_software_menu );\n',
             '\t/* ZZQ2: no driver, video mode, fullscreen, windowed mouse,\n'
             '\t   texture quality or 8-bit textures (one renderer and one\n'
             '\t   screen size per blob); the remaining items move up. */\n'
             '\ts_screensize_slider[SOFTWARE_MENU].generic.y = 0;\n'
             '\ts_brightness_slider[SOFTWARE_MENU].generic.y = 10;\n'
             '\ts_stipple_box.generic.y = 20;\n'
             '\ts_defaults_action[SOFTWARE_MENU].generic.y = 40;\n'
             '\ts_apply_action[SOFTWARE_MENU].generic.y = 50;\n'
             '\ts_screensize_slider[OPENGL_MENU].generic.y = 0;\n'
             '\ts_defaults_action[OPENGL_MENU].generic.y = 20;\n'
             '\ts_apply_action[OPENGL_MENU].generic.y = 30;\n'
             '\n'
             '\tMenu_AddItem( &s_software_menu, ( void * ) &s_screensize_slider[SOFTWARE_MENU] );\n'
             '\tMenu_AddItem( &s_software_menu, ( void * ) &s_brightness_slider[SOFTWARE_MENU] );\n'
             '\tMenu_AddItem( &s_software_menu, ( void * ) &s_stipple_box );\n'
             '\n'
             '\tMenu_AddItem( &s_opengl_menu, ( void * ) &s_screensize_slider[OPENGL_MENU] );\n'
             '\n'
             '\tMenu_AddItem( &s_software_menu, ( void * ) &s_defaults_action[SOFTWARE_MENU] );\n'
             '\tMenu_AddItem( &s_software_menu, ( void * ) &s_apply_action[SOFTWARE_MENU] );\n'
             '\tMenu_AddItem( &s_opengl_menu, ( void * ) &s_defaults_action[OPENGL_MENU] );\n'
             '\tMenu_AddItem( &s_opengl_menu, ( void * ) &s_apply_action[OPENGL_MENU] );\n'
             '\n',
             "menus: screen size, brightness, stipple alpha, defaults, apply")

    # 3. the console route: write protection, no vid_restart
    s = sub1(s,
             '/*\n'
             '================\n'
             'VID_MenuDraw\n'
             '================\n'
             '*/\n',
             '/*\n'
             '================\n'
             'zzq2_lock_video (ZZQ2)\n'
             '\n'
             'Called once Quake2_Init has returned (Core1/zzq2_glue.c). The\n'
             'renderer and the screen size of the blob can no longer be changed\n'
             'from the console either: CVAR_NOSET makes Cvar_Set refuse them\n'
             '("... is write protected."), and vid_restart is removed.\n'
             '================\n'
             '*/\n'
             'static int zzq2_menu_gl;\t/* 1: ref_gl blob (TGL, RIX) */\n'
             'void zzq2_lock_video( int gl )\n'
             '{\n'
             '\tzzq2_menu_gl = gl;\n'
             '\tstatic char *names[] = { "vid_ref", "sw_mode", "gl_mode", "vid_fullscreen", NULL };\n'
             '\tint i;\n'
             '\n'
             '\t/* only the variables that exist (sw_mode is ref_soft\'s, gl_mode\n'
             '\t   ref_gl\'s): an existing one keeps its value, Cvar_Get adds the flag */\n'
             '\tfor ( i = 0; names[i]; i++ )\n'
             '\t\tif ( Cvar_VariableString( names[i] )[0] )\n'
             '\t\t\tCvar_Get( names[i], "", CVAR_NOSET );\n'
             '\tCmd_RemoveCommand( "vid_restart" );\n'
             '}\n'
             '\n'
             '/*\n'
             '================\n'
             'VID_MenuDraw\n'
             '================\n'
             '*/\n',
             "zzq2_lock_video(): write protection, no vid_restart")

    # 4. the menu of the renderer linked in, whatever vid_ref says
    s = sub1(s,
             '\ts_software_menu.x = viddef.width * 0.50;\n',
             '\t/* ZZQ2: the menu of the renderer linked in (zzq2_lock_video) */\n'
             '\t{\n'
             '\t\textern int zzq2_video_is_gl( void );\n'
             '\t\ts_current_menu_index = zzq2_video_is_gl() ? OPENGL_MENU : SOFTWARE_MENU;\n'
             '\t}\n'
             '\n'
             '\ts_software_menu.x = viddef.width * 0.50;\n',
             "the menu of the renderer linked in")
    s = sub1(s,
             'static int zzq2_menu_gl;\t/* 1: ref_gl blob (TGL, RIX) */\n',
             'static int zzq2_menu_gl;\t/* 1: ref_gl blob (TGL, RIX) */\n'
             'int zzq2_video_is_gl( void ) { return zzq2_menu_gl; }\n',
             "zzq2_video_is_gl()")

    open(out, "w", encoding="latin-1", newline="\n").write(s)
    print("  -> %s" % out)


if __name__ == "__main__":
    main()
