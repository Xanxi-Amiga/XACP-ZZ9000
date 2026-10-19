#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
#
# zzq2_patch_save.py - ZZQuake2 0.6 / 0.8: patched copy of game/g_save.c.
#
#   usage: zzq2_patch_save.py <quake2generic dir> <output file>
#
# The quake2generic tree is NEVER modified: the upstream file is read,
# changed in memory, written to the build directory.
#
# Why. A Quake II save stores function pointers (entity callbacks, as
# offsets from InitGame) and monster moves (mmove_t, as offsets from
# mmove_reloc): upstream, it may only be loaded by the very program
# that wrote it, guarded by the compile date (__DATE__) at the head of
# game.ssv. The blob is built with SOURCE_DATE_EPOCH=1, so __DATE__ no
# longer protects anything. 0.6 used the launcher checksum of the blob
# as the stamp: any other blob, even the same game code at another
# screen size, refused the save.
#
# 0.8 (this version): the functions and the monster moves are saved by
# NAME (tables of Core1/zzq2_svtab.py, lookups in Core1/zzq2_svtab.c),
# written after the entity exactly as the strings (F_LSTRING) are: the
# field holds the length of the name, the name follows. Any blob built
# from the same game code reads the save: 320 / 640 / 800, software or
# TinyGL, and later versions.
#   1. game.ssv: __DATE__ -> zzsv_stamp(): "ZZQ2N " and a hash of the
#      sizes of the saved structures (edict_t, gclient_t, game_locals_t,
#      level_locals_t). A save of 0.6 / 0.7 ("ZZQ2 <checksum>") is
#      refused with the upstream message "Savegame from an older
#      version.", which shows both stamps (0.6.3).
#   2. WriteField1 / WriteField2 / ReadField: F_FUNCTION and F_MMOVE by
#      name. A pointer without a name (never expected: every function
#      and every mmove_t of the game code is in the tables) stops the
#      save with an error instead of writing a wrong save; a name this
#      blob does not know stops the load the same way.
#   3. Level files (.sav): the InitGame address written for checking is
#      replaced by a marker of the 0.8 layout; ReadLevel refuses a level
#      file without it (a level file of 0.7 left in save/current).
#
#   4. (ZZQuake2RIX 0.10.2) checks of the load, for a fault seen on the
#      hardware: saves of ZZQuake2 A0 0.8 load with ZZQuake2TGL 0.6 but
#      stop ZZQuake2RIX 0.10.1 with "ReadEdict: unknown field type".
#      The type comes from the field tables of the blob, not from the
#      file: the tables were overwritten in memory. Same save format,
#      same stamp; only checks and messages:
#        - the first InitGame of the session (a load calls it again)
#          takes a checksum of the three field tables (fields,
#          levelfields, clientfields); ReadGame, ReadLevel and the end of
#          every entity read compare: "save tables altered in memory"
#          says whether they were overwritten BEFORE the load (a stray
#          write elsewhere in the session) or by the entity just read;
#        - ReadLevel refuses an entity number outside 0..maxentities-1
#          (upstream writes the entity wherever the number points), with
#          the number of entities read and the offset in the file;
#        - the "unknown field type" message gives the type, the entity
#          and whether the tables are intact.
#
# STRICT: every anchor must be found exactly once, otherwise the script
# stops and nothing is built. ASCII only.

import os
import sys


def die(msg):
    raise SystemExit("zzq2_patch_save: " + msg)


def sub1(txt, old, new, tag):
    if txt.count(old) != 1:
        die("%s: pattern found %d times (need 1)" % (tag, txt.count(old)))
    print("  g_save.c : %s" % tag)
    return txt.replace(old, new)


def main():
    if len(sys.argv) != 3:
        die("usage: zzq2_patch_save.py <quake2generic dir> <output file>")
    q2, out = sys.argv[1], sys.argv[2]
    s = open(os.path.join(q2, "game", "g_save.c"), encoding="latin-1").read()

    s = sub1(s, '#include "g_local.h"\n',
             '#include "g_local.h"\n'
             '/* ZZQ2 0.8: functions and monster moves saved by name (Core1/zzq2_svtab.c) */\n'
             'const char *zzsv_name(int move, const void *addr);\n'
             'void *zzsv_addr(int move, const char *name);\n'
             '#define ZZSV_NAME_MAX 64\n'
             '#define ZZSV_LEVEL_MARK ((void *)0x4e325a5a)\t/* "ZZ2N" */\n'
             'static const char *zzsv_name_of(int move, const void *addr)\n'
             '{\n'
             '\tconst char *n = zzsv_name(move, addr);\n'
             '\tif (!n)\n'
             '\t\tgi.error ("ZZQ2: %s at %p has no name: save stopped\\n",\n'
             '\t\t           move ? "monster move" : "function", addr);\n'
             '\treturn n;\n'
             '}\n'
             'static char *zzsv_stamp(void)\n'
             '{\n'
             '\tstatic char s[16];\n'
             '\tunsigned int h = 2166136261u, v[4];\n'
             '\tint i;\n'
             '\tv[0] = sizeof(edict_t); v[1] = sizeof(gclient_t);\n'
             '\tv[2] = sizeof(game_locals_t); v[3] = sizeof(level_locals_t);\n'
             '\tfor (i = 0; i < 16; i++)\n'
             '\t\th = (h ^ ((v[i / 4] >> (8 * (i % 4))) & 255u)) * 16777619u;\n'
             '\tCom_sprintf (s, sizeof(s), "ZZQ2N %08x", h);\n'
             '\treturn s;\n'
             '}\n',
             "declarations, zzsv_name_of, zzsv_stamp")
    s = sub1(s, "void InitGame (void)\n{\n\tgi.dprintf (\"==== InitGame ====\\n\");\n",
             "/* ZZQ2 (RIX 0.10.2): checksum of the field tables, taken by InitGame and\n"
             "   compared by the loads, to tell a table overwritten in memory. */\n"
             "static unsigned int zzsv_sum0, zzsv_have0;\n"
             "static int zzsv_cur_ent = -1;\n"
             "static unsigned int zzsv_sum1 (const field_t *t, unsigned int h)\n"
             "{\n"
             "\tint i, k;\n"
             "\tfor (i = 0; i < 256; i++, t++)\n"
             "\t{\n"
             "\t\tconst unsigned char *b = (const unsigned char *)t;\n"
             "\t\tfor (k = 0; k < (int)sizeof(*t); k++)\n"
             "\t\t\th = (h ^ b[k]) * 16777619u;\n"
             "\t\tif (!t->name)\n"
             "\t\t\tbreak;\n"
             "\t}\n"
             "\treturn h;\n"
             "}\n"
             "static unsigned int zzsv_tabsum (void)\n"
             "{\n"
             "\treturn zzsv_sum1 (clientfields, zzsv_sum1 (levelfields, zzsv_sum1 (fields, 2166136261u)));\n"
             "}\n"
             "static void zzsv_check_tables (FILE *f, const char *where)\n"
             "{\n"
             "\tunsigned int s;\n"
             "\tif (!zzsv_have0)\n"
             "\t\treturn;\n"
             "\ts = zzsv_tabsum ();\n"
             "\tif (s != zzsv_sum0)\n"
             "\t{\n"
             "\t\tif (f)\n"
             "\t\t\tfclose (f);\n"
             "\t\tgi.error (\"ZZQ2 %s: save tables altered in memory (%08x, start %08x), entity %d\\n\",\n"
             "\t\t           where, s, zzsv_sum0, zzsv_cur_ent);\n"
             "\t}\n"
             "}\n"
             "\n"
             "void InitGame (void)\n{\n\tgi.dprintf (\"==== InitGame ====\\n\");\n"
             "\tif (!zzsv_have0)\t/* ZZQ2 (RIX 0.10.2): the first InitGame only (a load calls it again) */\n"
             "\t{\n"
             "\t\tzzsv_sum0 = zzsv_tabsum ();\n"
             "\t\tzzsv_have0 = 1;\n"
             "\t}\n",
             "InitGame: checksum of the field tables")

    s = sub1(s, "\tstrcpy (str, __DATE__);\n",
             "\tstrcpy (str, zzsv_stamp());\t/* ZZQ2 0.8: was the compile date */\n",
             "WriteGame: stamp of the save layout instead of __DATE__")
    s = sub1(s, "\tif (strcmp (str, __DATE__))\n",
             "\tif (strcmp (str, zzsv_stamp()))\t/* ZZQ2 0.8: was the compile date */\n",
             "ReadGame: stamp of the save layout instead of __DATE__")
    s = sub1(s, '\t\tgi.error ("Savegame from an older version.\\n");\n',
             '\t\tstr[15] = 0;\t/* ZZQ2 0.6.3: show both stamps */\n'
             '\t\tgi.error ("Savegame from an older version. (file \'%s\', blob \'%s\')\\n",\n'
             '\t\t           str, zzsv_stamp());\n',
             "ReadGame: refusal message shows both stamps")

    s = sub1(s,
             "\t//relative to code segment\n"
             "\tcase F_FUNCTION:\n"
             "\t\tif (*(byte **)p == NULL)\n"
             "\t\t\tindex = 0;\n"
             "\t\telse\n"
             "\t\t\tindex = *(byte **)p - ((byte *)InitGame);\n"
             "\t\t*(int *)p = index;\n"
             "\t\tbreak;\n"
             "\n"
             "\t//relative to data segment\n"
             "\tcase F_MMOVE:\n"
             "\t\tif (*(byte **)p == NULL)\n"
             "\t\t\tindex = 0;\n"
             "\t\telse\n"
             "\t\t\tindex = *(byte **)p - (byte *)&mmove_reloc;\n"
             "\t\t*(int *)p = index;\n"
             "\t\tbreak;\n",
             "\t// ZZQ2 0.8: by name, length here, the name in WriteField2\n"
             "\tcase F_FUNCTION:\n"
             "\tcase F_MMOVE:\n"
             "\t\tif (*(void **)p == NULL)\n"
             "\t\t\tlen = 0;\n"
             "\t\telse\n"
             "\t\t\tlen = strlen (zzsv_name_of (field->type == F_MMOVE, *(void **)p)) + 1;\n"
             "\t\t*(int *)p = len;\n"
             "\t\tbreak;\n",
             "WriteField1: F_FUNCTION / F_MMOVE by name")
    s = sub1(s,
             "\tcase F_LSTRING:\n"
             "\t\tif ( *(char **)p )\n"
             "\t\t{\n"
             "\t\t\tlen = strlen(*(char **)p) + 1;\n"
             "\t\t\tfwrite (*(char **)p, len, 1, f);\n"
             "\t\t}\n"
             "\t\tbreak;\n",
             "\tcase F_LSTRING:\n"
             "\t\tif ( *(char **)p )\n"
             "\t\t{\n"
             "\t\t\tlen = strlen(*(char **)p) + 1;\n"
             "\t\t\tfwrite (*(char **)p, len, 1, f);\n"
             "\t\t}\n"
             "\t\tbreak;\n"
             "\tcase F_FUNCTION:\t/* ZZQ2 0.8: the name */\n"
             "\tcase F_MMOVE:\n"
             "\t\tif ( *(void **)p )\n"
             "\t\t{\n"
             "\t\t\tconst char *zzn = zzsv_name_of (field->type == F_MMOVE, *(void **)p);\n"
             "\t\t\tfwrite ((void *)zzn, strlen (zzn) + 1, 1, f);\n"
             "\t\t}\n"
             "\t\tbreak;\n",
             "WriteField2: names after the strings")
    s = sub1(s,
             "\t//relative to code segment\n"
             "\tcase F_FUNCTION:\n"
             "\t\tindex = *(int *)p;\n"
             "\t\tif ( index == 0 )\n"
             "\t\t\t*(byte **)p = NULL;\n"
             "\t\telse\n"
             "\t\t\t*(byte **)p = ((byte *)InitGame) + index;\n"
             "\t\tbreak;\n"
             "\n"
             "\t//relative to data segment\n"
             "\tcase F_MMOVE:\n"
             "\t\tindex = *(int *)p;\n"
             "\t\tif (index == 0)\n"
             "\t\t\t*(byte **)p = NULL;\n"
             "\t\telse\n"
             "\t\t\t*(byte **)p = (byte *)&mmove_reloc + index;\n"
             "\t\tbreak;\n",
             "\t// ZZQ2 0.8: by name\n"
             "\tcase F_FUNCTION:\n"
             "\tcase F_MMOVE:\n"
             "\t\tlen = *(int *)p;\n"
             "\t\tif (!len)\n"
             "\t\t\t*(void **)p = NULL;\n"
             "\t\telse\n"
             "\t\t{\n"
             "\t\t\tchar zzn[ZZSV_NAME_MAX];\n"
             "\t\t\tif (len < 2 || len > ZZSV_NAME_MAX)\n"
             "\t\t\t{\n"
             "\t\t\t\tfclose (f);\n"
             "\t\t\t\tgi.error (\"ReadField: name of %d bytes, damaged save\\n\", len);\n"
             "\t\t\t}\n"
             "\t\t\tfread (zzn, len, 1, f);\n"
             "\t\t\tzzn[len - 1] = 0;\n"
             "\t\t\t*(void **)p = zzsv_addr (field->type == F_MMOVE, zzn);\n"
             "\t\t\tif (!*(void **)p)\n"
             "\t\t\t{\n"
             "\t\t\t\tfclose (f);\n"
             "\t\t\t\tgi.error (\"ReadField: '%s' unknown to this blob\\n\", zzn);\n"
             "\t\t\t}\n"
             "\t\t}\n"
             "\t\tbreak;\n",
             "ReadField: F_FUNCTION / F_MMOVE by name")
    s = sub1(s,
             "\t\t           str, zzsv_stamp());\n\t}\n",
             "\t\t           str, zzsv_stamp());\n\t}\n"
             "\tzzsv_check_tables (f, \"ReadGame\");\t/* ZZQ2 (RIX 0.10.2) */\n",
             "ReadGame: field tables checked")
    s = sub1(s,
             "\t// load all the entities\n"
             "\twhile (1)\n"
             "\t{\n"
             "\t\tif (fread (&entnum, sizeof(entnum), 1, f) != 1)\n"
             "\t\t{\n"
             "\t\t\tfclose (f);\n"
             "\t\t\tgi.error (\"ReadLevel: failed to read entnum\");\n"
             "\t\t}\n"
             "\t\tif (entnum == -1)\n"
             "\t\t\tbreak;\n"
             "\t\tif (entnum >= globals.num_edicts)\n"
             "\t\t\tglobals.num_edicts = entnum+1;\n"
             "\n"
             "\t\tent = &g_edicts[entnum];\n"
             "\t\tReadEdict (f, ent);\n",
             "\t// load all the entities\n"
             "\tzzsv_check_tables (f, \"ReadLevel\");\t/* ZZQ2 (RIX 0.10.2) */\n"
             "\ti = 0;\n"
             "\twhile (1)\n"
             "\t{\n"
             "\t\tif (fread (&entnum, sizeof(entnum), 1, f) != 1)\n"
             "\t\t{\n"
             "\t\t\tfclose (f);\n"
             "\t\t\tgi.error (\"ReadLevel: failed to read entnum\");\n"
             "\t\t}\n"
             "\t\tif (entnum == -1)\n"
             "\t\t\tbreak;\n"
             "\t\tif (entnum < 0 || entnum >= game.maxentities)\t/* ZZQ2 (RIX 0.10.2) */\n"
             "\t\t{\n"
             "\t\t\tlong zzpos = ftell (f);\n"
             "\t\t\tfclose (f);\n"
             "\t\t\tgi.error (\"ReadLevel: entity %d out of 0..%d (%d read, offset %ld)\\n\",\n"
             "\t\t\t           entnum, game.maxentities - 1, i, zzpos);\n"
             "\t\t}\n"
             "\t\tif (entnum >= globals.num_edicts)\n"
             "\t\t\tglobals.num_edicts = entnum+1;\n"
             "\n"
             "\t\tent = &g_edicts[entnum];\n"
             "\t\tzzsv_cur_ent = entnum;\n"
             "\t\tReadEdict (f, ent);\n"
             "\t\tzzsv_check_tables (f, \"ReadEdict\");\t/* ZZQ2 (RIX 0.10.2) */\n"
             "\t\ti++;\n",
             "ReadLevel: entity numbers bounded, tables checked after each entity")
    s = sub1(s,
             "\tdefault:\n"
             "\t\tgi.error (\"ReadEdict: unknown field type\");\n",
             "\tdefault:\t/* ZZQ2 (RIX 0.10.2): which type, which entity, tables intact? */\n"
             "\t\tgi.error (\"ReadEdict: unknown field type %d at %p, entity %d, tables %s\\n\",\n"
             "\t\t           (int)field->type, (void *)field, zzsv_cur_ent,\n"
             "\t\t           zzsv_tabsum () == zzsv_sum0 ? \"intact\" : \"ALTERED\");\n",
             "ReadField: unknown field type message")

    s = sub1(s,
             "\t// write out a function pointer for checking\n"
             "\tbase = (void *)InitGame;\n",
             "\t// ZZQ2 0.8: a marker of the by-name layout instead of InitGame\n"
             "\tbase = ZZSV_LEVEL_MARK;\n",
             "WriteLevel: layout marker")
    s = sub1(s,
             "#ifdef _WIN32\n"
             "\tif (base != (void *)InitGame)\n"
             "\t{\n"
             "\t\tfclose (f);\n"
             "\t\tgi.error (\"ReadLevel: function pointers have moved\");\n"
             "\t}\n"
             "#else\n"
             "\tgi.dprintf(\"Function offsets %d\\n\", ((byte *)base) - ((byte *)InitGame));\n"
             "#endif\n",
             "\tif (base != ZZSV_LEVEL_MARK)\t/* ZZQ2 0.8 */\n"
             "\t{\n"
             "\t\tfclose (f);\n"
             "\t\tgi.error (\"ReadLevel: level file of an older version\\n\");\n"
             "\t}\n",
             "ReadLevel: layout marker checked")

    # gamedate (a server info cvar) keeps __DATE__: display only.
    if s.count("__DATE__") != 1:
        die("__DATE__ found %d times after patching (need 1: gamedate)"
            % s.count("__DATE__"))

    open(out, "w", encoding="latin-1", newline="\n").write(s)
    print("  -> %s" % out)


if __name__ == "__main__":
    main()
