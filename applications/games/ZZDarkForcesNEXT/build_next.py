#!/usr/bin/env python3
# build_next.py - ZZDarkForces NEXT reproducible build.
#
# Pipeline (per project discipline):
#   1. locate the pinned TFE upstream (FETCH_UPSTREAM.cmd downloads it)
#   2. copy the SELECTED engine sources into build_next/engine
#      (upstream tree is never modified)
#   3. patch ONLY the copies (patch_engine below, reproducible)
#   4. compile every unit, collecting errors in batches (no stop at
#      first failure)
#   5. census link WITHOUT --gc-sections -> all undefined refs at once
#   6. only when the census closes: runnable link with --gc-sections,
#      2MB assert, objcopy, real .bin existence+size check
#
# Usage:  python3 build_next.py [--upstream DIR] [--census-only]
#                                [--return-test [--outer-nc]]
#   DEFAULT        zzdf.bin - cooperative return to core1_loop()
#                  (hardware-validated 17.09.2026). core1/zzdf_return.c
#                  is part of the build.
#   --legacy-park  ARCHIVED zzdf_legacy_park.bin: the old single-shot
#                  WFE park (-DZZDF_LEGACY_WFE_PARK), zzdf_return.c not
#                  linked. Kept only to rebuild the previous behaviour
#                  deliberately.
# Windows: BUILD_NEXT_WINDOWS.cmd wraps this with the right PATH.
# ASCII only.

import os, sys, subprocess, shutil, glob, re, hashlib

ROOT = os.path.dirname(os.path.abspath(__file__))
BUILD = os.path.join(ROOT, "build_next")
ENG = os.path.join(BUILD, "engine")
OBJ = os.path.join(BUILD, "obj")          # engine objects, shared
ZZOBJ = OBJ                               # core1+platform, per variant
VARIANT = "baseline"
VARIANT_DEFS = []
UPSTREAM_DEFAULT = os.path.join(ROOT, "upstream", "TheForceEngine")
PIN = "ed9e51c315078d6460551593e48fd294e131fc83"

CROSS = os.environ.get("ZZDF_CROSS", "arm-none-eabi-")
GCC = CROSS + "gcc"
GXX = CROSS + "g++"
OBJCOPY = CROSS + "objcopy"
SIZE = CROSS + "size"
NM = CROSS + "nm"

MACH = ["-mcpu=cortex-a9", "-marm", "-mfpu=vfpv3-d16",
        "-mfloat-abi=hard", "-mno-unaligned-access", "-fno-short-enums"]
SECT = ["-ffunction-sections", "-fdata-sections"]

# Engine C++ units: HOSTED libstdc++ (brief: no -ffreestanding on STL
# users), no exceptions/RTTI (verified: zero try/dynamic_cast in the
# selected set).
CXXFLAGS = MACH + SECT + ["-O2", "-std=gnu++14", "-fno-exceptions",
                          "-fno-rtti", "-w", "-DZZDF_BAREMETAL",
                          "-D__ZZ9000__"]
# Engine C units (cJSON, miniz zip): hosted too.
CFLAGS_ENG = MACH + SECT + ["-O2", "-w", "-DZZDF_BAREMETAL"]
# Core1 C: freestanding like ZZQuake.
CFLAGS_CORE = MACH + SECT + ["-O2", "-ffreestanding", "-DZZDF_BAREMETAL"]

# ---------------------------------------------------------------- #
# Source selection: SECBASE-first. GPU/UI/audio/script excluded and
# replaced by platform stubs. Paths relative to upstream/TheForceEngine.
# ---------------------------------------------------------------- #
SELECT_DIRS = [
    # (dir, recursive, excludes)
    ("TFE_DarkForces", True, ["Scripting/", "Remaster/"]),
    ("TFE_Jedi", True, ["RClassic_GPU/", "RClassic_Float/screenDraw.cpp"]),
    ("TFE_Memory", False, []),
    ("TFE_Settings", False, []),
    ("TFE_ExternalData", False, []),
    ("TFE_Archive", False, ["zstdCompression.cpp"]),
    ("TFE_Asset", False, ["imageAsset.cpp"]),
]
SELECT_FILES = [
    # Upstream's MIDI player, compiled rather than reimplemented.
    # Only SDL integration and the MIDI device are replaced.
    "TFE_Audio/midiPlayer.cpp",
    
    
    "TFE_Audio/MidiSynth/soundFontDevice.cpp",
    "TFE_System/log.cpp",
    "TFE_System/iniParser.cpp",
    "TFE_System/math.cpp",
    "TFE_System/memoryPool.cpp",
    "TFE_System/parser.cpp",
    "TFE_System/profiler.cpp",
    "TFE_System/tfeMessage.cpp",
    "TFE_System/utf8.cpp",
    "TFE_System/frameLimiter.cpp",
    "TFE_System/cJSON.c",
    "TFE_FileSystem/filestream.cpp",
    "TFE_FileSystem/memorystream.cpp",
    "TFE_FileSystem/paths.cpp",
    "TFE_Input/input.cpp",
    "TFE_Input/inputMapping.cpp",
    "TFE_Input/replay.cpp",
    "TFE_Game/igame.cpp",
    "TFE_Game/saveSystem.cpp",
    "TFE_Archive/zip/zip.c",
]

# Whole headers tree is copied so includes resolve; only the sources
# above are compiled.

def launcher_cksum(data):
    """Return the rotate-XOR checksum printed by the Amiga launcher."""
    s = 0
    for b in data:
        s = ((s << 1) & 0xFFFFFFFF) ^ (s >> 31) ^ b
    return s


def run(cmd, **kw):
    return subprocess.run(cmd, capture_output=True, text=True, **kw)

def die(msg):
    print("ERROR: " + msg)
    sys.exit(1)

# ---------------------------------------------------------------- #
def locate_upstream(argv):
    up = UPSTREAM_DEFAULT
    if "--upstream" in argv:
        up = argv[argv.index("--upstream") + 1]
    marker = os.path.join(up, "TFE_DarkForces", "darkForcesMain.cpp")
    if not os.path.isfile(marker):
        die("upstream not found at %s - run FETCH_UPSTREAM first (pin %s)"
            % (up, PIN))
    return up

def copy_engine(up):
    if os.path.isdir(ENG):
        shutil.rmtree(ENG)
    os.makedirs(ENG)
    # headers: copy every TFE_* tree minus the fat excluded ones we
    # never include headers from
    for d in sorted(os.listdir(up)):
        src = os.path.join(up, d)
        if not os.path.isdir(src):
            continue
        if d in ("Captions", "Documentation", "EditorDef", "ExternalData",
                 "Fonts", "Mods", "ScriptTests", "Shaders", "SoundFonts",
                 "Tests", "UI_Images", "UI_Text", "ogg_theora_win32",
                 "sdl2_win32"):
            continue

        shutil.copytree(src, os.path.join(ENG, d),
                        copy_function=shutil.copyfile)

def selected_sources():
    out = []
    for d, rec, excl in SELECT_DIRS:
        pat = os.path.join(ENG, d, "**", "*.cpp") if rec else \
              os.path.join(ENG, d, "*.cpp")
        for f in sorted(glob.glob(pat, recursive=rec)):
            rel = os.path.relpath(f, ENG).replace("\\", "/")
            if any(e in rel for e in excl):
                continue
            out.append(rel)
    for f in SELECT_FILES:
        if os.path.isfile(os.path.join(ENG, f)):
            out.append(f)
        else:
            print("WARN: selected file missing: " + f)
    return out

# ---------------------------------------------------------------- #
# patch_engine: every patch targets the COPY, is idempotent, and is
# listed here so the whole delta vs upstream is auditable.
# ---------------------------------------------------------------- #
def sub_file(rel, pairs, must=True):
    p = os.path.join(ENG, rel)
    if not os.path.isfile(p):
        if must: die("patch target missing: " + rel)
        return
    s = open(p, "r", encoding="utf-8", errors="replace").read()
    orig = s
    for a, b in pairs:
        if isinstance(a, str):
            s = s.replace(a, b)
        else:
            s = a.sub(b, s)
    if s != orig:
        open(p, "w", encoding="utf-8", newline="\n").write(s)
        print("  patched " + rel)


def append_file(rel, text, marker=None):
    """Append text to a copied engine file once, using a marker to avoid duplicates."""
    p = os.path.join(ENG, rel)
    if not os.path.isfile(p):
        die("append target missing: " + rel)
    key = marker if marker else text.strip().splitlines()[-1]
    s = open(p, "r", encoding="utf-8", errors="replace").read()
    if key in s:
        print("  append already present in " + rel)
        return
    open(p, "a", encoding="utf-8", newline="\n").write(text)
    print("  appended to " + rel)


def require_in(rel, needles, why):
    """Assert a patch really landed in the copied engine file."""
    p = os.path.join(ENG, rel)
    if not os.path.isfile(p):
        die("verify target missing: " + rel)
    s = open(p, "r", encoding="utf-8", errors="replace").read()
    for n in needles:
        if n not in s:
            die("PATCH DID NOT APPLY in %s (%s): missing %r" % (rel, why, n))
    print("  verified %s (%s)" % (rel, why))


def patch_engine():
    print("== patch_engine (copies only) ==")

    # system.h: MSVC/glibc leak time_t + strcasecmp; make explicit
    sub_file("TFE_System/system.h", [
        ("#include <ctype.h>",
         "#include <ctype.h>\n#include <time.h>\n#include <strings.h>"),
    ])

    # types.h: int32_t is 'long' on arm-none-eabi newlib ->
    # qsort comparators, min/clamp overloads and PRI macros all break.
    # ARM EABI: int and long are both 32 bit; use plain int types.
    sub_file("TFE_System/types.h", [
        ("typedef uint32_t u32;", "typedef unsigned int u32;"),
        ("typedef int32_t s32;", "typedef signed int s32;"),
    ])

    # log.cpp: PRIu64 needs cinttypes explicitly
    sub_file("TFE_System/log.cpp", [
        ('".%03" PRIu64 " - ", milliseconds.count()',
         '".%03llu - ", (unsigned long long)milliseconds.count()'),
    ], must=False)

    # log.cpp: route engine WARNING/ERROR to the ZZDF log ring.
    # Upstream logWrite() returns immediately when no log file is open,
    # and this target has no writable filesystem, so EVERY engine
    # warning was dropped in silence - including
    #   "Could not open '%s', using 'default.bm' instead."
    # which is the one that names a missing level texture. The tap is
    # placed BEFORE the original early-return, reuses s_msgStr (unused
    # on this target, since the original path returns), and is capped
    # so a noisy level cannot flood the ring the launcher drains.
    # LOG_MSG=0 is skipped on purpose: only 1/2/3 (Warning, Error,
    # Critical) are emitted. Behaviour is otherwise unchanged.
    sub_file("TFE_System/log.cpp", [
        ('	void logWrite(LogWriteType type, const char* tag, const char* str, ...)\n'
         '	{\n'
         '		if (type >= LOG_COUNT || !s_logFile.isOpen() || !tag || !str) { return; }',
         '	void logWrite(LogWriteType type, const char* tag, const char* str, ...)\n'
         '	{\n'
         '		/* ZZDF tap: no log file on this target, so mirror engine\n'
         '		   warnings and errors into the shared log ring. */\n'
         '		if (tag && str && (int)type >= 1 && (int)type < (int)LOG_COUNT)\n'
         '		{\n'
         '			static int s_zzdfLogBudget = 192;\n'
         '			if (s_zzdfLogBudget > 0)\n'
         '			{\n'
         '				s_zzdfLogBudget--;\n'
         '				va_list zzarg;\n'
         '				va_start(zzarg, str);\n'
         '				vsnprintf(s_msgStr, sizeof(s_msgStr), str, zzarg);\n'
         '				va_end(zzarg);\n'
         '				zzdf_log_puts("[TFE] ");\n'
         '				zzdf_log_puts(tag);\n'
         '				zzdf_log_puts(": ");\n'
         '				zzdf_log_puts(s_msgStr);\n'
         '				zzdf_log_puts("\\n");\n'
         '				if (s_zzdfLogBudget == 0)\n'
         '				{\n'
         '					zzdf_log_puts("[TFE] (log budget exhausted)\\n");\n'
         '				}\n'
         '			}\n'
         '		}\n'
         '		if (type >= LOG_COUNT || !s_logFile.isOpen() || !tag || !str) { return; }'),
        ('namespace TFE_System\n{',
         'extern "C" void zzdf_log_puts(const char *s);\n\nnamespace TFE_System\n{'),
    ])

    sub_file("TFE_DarkForces/darkForcesMain.cpp", [
        ('#include "darkForcesMain.h"',
         'extern "C" void zzdf_log_puts(const char *s);\n#include "darkForcesMain.h"'),
    ])

    # main(): upstream loads the TFE message table in main.cpp,
    # which this port replaces, so getMessage() returned null for every
    # id and the strings TFE adds on top of Dark Forces never appeared
    # ("You found a secret!", "Using the %s key", the key colours).
    # Load it from the staged TfeMessages.txt at game startup instead.
    sub_file("TFE_DarkForces/darkForcesMain.cpp", [
        ('	void gameStartup()\n'
         '	{\n'
         '		hud_loadGraphics();',
         '	void gameStartup()\n'
         '	{\n'
         '		/* ZZDF: done by main.cpp upstream, which this port replaces. */\n'
         '		if (!TFE_System::loadMessages("TfeMessages.txt"))\n'
         '		{\n'
         '			zzdf_log_puts("[ZZDF] TfeMessages.txt not loaded\\n");\n'
         '		}\n'
         '		hud_loadGraphics();'),
    ])

    # ExternalData: the four loaders bail out with a bare
    # "return" when the file is not there, so weapons.json could be
    # missing for weeks with nothing said anywhere - which is exactly
    # what happened. Make each failure speak.
    for f, fn in (("TFE_ExternalData/weaponExternal.cpp", "projectiles.json"),
                  ("TFE_ExternalData/weaponExternal.cpp", "effects.json"),
                  ("TFE_ExternalData/weaponExternal.cpp", "weapons.json"),
                  ("TFE_ExternalData/pickupExternal.cpp", "pickups.json")):
        sub_file(f, [
            ('		strcpy(extDataFile, "ExternalData/DarkForces/%s");' % fn,
             '		strcpy(extDataFile, "ExternalData/DarkForces/%s");\n'
             '		zzdf_extdata_mark("%s");' % (fn, fn)),
        ], must=False)
    sub_file("TFE_ExternalData/weaponExternal.cpp", [
        ('		FileStream file;\n'
         '		if (!file.open(extDataFile, FileStream::MODE_READ)) { return; }',
         '		FileStream file;\n'
         '		if (!file.open(extDataFile, FileStream::MODE_READ))\n'
         '		{ zzdf_extdata_fail(); return; }'),
        ('namespace TFE_ExternalData\n{',
         'extern "C" void zzdf_extdata_mark(const char *name);\n'
         'extern "C" void zzdf_extdata_fail(void);\n\n'
         'namespace TFE_ExternalData\n{'),
    ], must=False)
    sub_file("TFE_ExternalData/pickupExternal.cpp", [
        ('		FileStream file;\n'
         '		if (!file.open(extDataFile, FileStream::MODE_READ)) { return; }',
         '		FileStream file;\n'
         '		if (!file.open(extDataFile, FileStream::MODE_READ))\n'
         '		{ zzdf_extdata_fail(); return; }'),
        ('namespace TFE_ExternalData\n{',
         'extern "C" void zzdf_extdata_mark(const char *name);\n'
         'extern "C" void zzdf_extdata_fail(void);\n\n'
         'namespace TFE_ExternalData\n{'),
    ], must=False)

    # infSystem: say how big the INF actually was. SECBASE
    # reports "58/60 items" and we cannot tell from here whether the
    # file is short in our archive read or simply declares more items
    # than it holds (a known quirk of some Dark Forces data sets). One
    # line settles it on the next run instead of guessing.
    sub_file("TFE_Jedi/InfSystem/infSystem.cpp", [
        ('''		// Then loop through all of the items and parse their classes.
		s32 wallNum = 0;''',
         '''		TFE_System::logWrite(LOG_WARNING, "level_loadINF",
			"ZZDF: INF '%s' buffer %u bytes, declares ITEMS %d",
			levelName, (u32)s_buffer.size(), itemCount);

		// Then loop through all of the items and parse their classes.
		s32 wallNum = 0;'''),
    ], must=False)

    # inputMapping: hooks header (no SDL.h include upstream, the
    # SDL decls came in transitively on desktop)
    sub_file("TFE_Input/inputMapping.cpp", [
        ('#include "inputMapping.h"',
         '#include "inputMapping.h"\n#include "zzdf_platform_hooks.h"'),
    ], must=False)

    # ForceScript math headers: angelscript.h -> forward decl
    for rel in ("TFE_ForceScript/float2.h", "TFE_ForceScript/float3.h",
                "TFE_ForceScript/float4.h", "TFE_ForceScript/float2x2.h",
                "TFE_ForceScript/float3x3.h", "TFE_ForceScript/float4x4.h"):
        sub_file(rel, [
            ("#include <angelscript.h>", "class asIScriptEngine;"),
        ])

    # imageAsset.h: SDL_image removed, minimal surface type kept
    p = os.path.join(ENG, "TFE_Asset", "imageAsset.h")
    open(p, "w", newline="\n").write(ZZDF_IMAGEASSET_H)
    print("  replaced TFE_Asset/imageAsset.h (SDL-free)")

    # clamp ambiguity (std::clamp vs TFE_Jedi::clamp on s16)
    sub_file("TFE_DarkForces/Landru/lrect.cpp", [
        ("*x = clamp(*x, rect->left, rect->right);",
         "*x = (s16)clamp((s32)*x, (s32)rect->left, (s32)rect->right);"),
        ("*y = clamp(*y, rect->top, rect->bottom);",
         "*y = (s16)clamp((s32)*y, (s32)rect->top, (s32)rect->bottom);"),
    ], must=False)
    sub_file("TFE_DarkForces/Landru/lview.cpp", [
        ("xVel = clamp(xVel, -s_view->maxTrackXVel[viewIndex], s_view->maxTrackXVel[viewIndex]);",
         "xVel = (s16)clamp((s32)xVel, -(s32)s_view->maxTrackXVel[viewIndex], (s32)s_view->maxTrackXVel[viewIndex]);"),
        ("yVel = clamp(yVel, -s_view->maxTrackYVel[viewIndex], s_view->maxTrackYVel[viewIndex]);",
         "yVel = (s16)clamp((s32)yVel, -(s32)s_view->maxTrackYVel[viewIndex], (s32)s_view->maxTrackYVel[viewIndex]);"),
    ], must=False)

    # endian: keep the TFE_Endian API, drop SDL (brief). Target is LE.
    sub_file("TFE_Jedi/Serialization/serialization.h", [], must=False)
    endian_h = None
    for cand in glob.glob(os.path.join(ENG, "**", "endian*.h"),
                          recursive=True):
        endian_h = os.path.relpath(cand, ENG)
    if endian_h:
        p = os.path.join(ENG, endian_h)
        s = open(p).read()
        if "SDL" in s:
            open(p, "w", newline="\n").write(ZZDF_ENDIAN_H)
            print("  replaced " + endian_h + " (SDL-free, LE identity)")

    # inputMapping: SDL relative mouse -> platform hook
    sub_file("TFE_Input/inputMapping.cpp", [
        ("#include <SDL.h>", "#include \"zzdf_platform_hooks.h\""),
        ("SDL_GetRelativeMouseState(&mouseX, &mouseY);",
         "zzdf_getRelativeMouse(&mouseX, &mouseY);"),
        ("SDL_GetMouseState(&mouseAbsX, &mouseAbsY);",
         "zzdf_getAbsoluteMouse(&mouseAbsX, &mouseAbsY);"),
    ], must=False)

    # saveSystem / reticle / textureAsset: SDL include -> hooks header
    for rel in ("TFE_Game/saveSystem.cpp", "TFE_Game/reticle.cpp",
                "TFE_Asset/textureAsset.cpp"):
        sub_file(rel, [
            ("#include <SDL.h>", "#include \"zzdf_platform_hooks.h\""),
            ("#include <SDL_image.h>", ""),
        ], must=False)

    # saveSystem: ONE quicksave slot on this port.
    #
    # Upstream alternates quicksave.tfe and quicksave2.tfe and decides
    # which is which with FileUtil::getModifiedTime(). This port has no
    # timestamps to give it: saves live in the MemFS, and across a
    # session they are staged back in from DATA by the preload, which
    # carries a name and a size and nothing else.
    #
    # With getModifiedTime() returning 0 for both files the arithmetic
    # does not degrade gracefully, it inverts:
    #     slot0IsOlder = (0 < 0) = false
    #     save -> getQuickSaveSlotIndex(false) -> 1  (quicksave2.tfe)
    #     load -> getQuickSaveSlotIndex(true)  -> 0  (quicksave.tfe)
    # so once both files exist, every Alt+F5 writes slot 2 and every
    # Alt+F9 reads slot 1. The player saves, reloads, and silently gets
    # a save from several missions ago.
    #
    # Implementing getModifiedTime() does NOT fix this, and that is the
    # reason this patch exists rather than that one. Within a session a
    # counter would work; across a session both files arrive from the
    # manifest with no timestamp at all, so the FIRST load after a
    # relaunch - the case that matters most - would still pick wrong. A
    # scheme that is right sometimes is worse here than one that is
    # always right, so both slots are the same file and the choice
    # stops mattering. The cost is upstream's "a bad quicksave never
    # overwrites your only recent save", which is a real loss, and it
    # is written down here so it can be bought back later with real
    # timestamps in the manifest.
    sub_file("TFE_Game/saveSystem.cpp", [
        ('static const char* c_quickSaveSlotNames[2] = '
         '{ c_quickSaveName, "quicksave2.tfe" };',
         'static const char* c_quickSaveSlotNames[2] = '
         '{ c_quickSaveName, c_quickSaveName }; /* ZZ9000: one slot */'),
        ('static const char* c_quickSaveSlotLabels[2] = '
         '{ "Quicksave", "Quicksave 2" };',
         'static const char* c_quickSaveSlotLabels[2] = '
         '{ "Quicksave", "Quicksave" }; /* ZZ9000: one slot */'),
    ])
    require_in("TFE_Game/saveSystem.cpp", [
        "{ c_quickSaveName, c_quickSaveName }",
        '{ "Quicksave", "Quicksave" }',
    ], "single quicksave slot")

    # jediRenderer: never instantiate the GPU sector renderer and
    # never call its cache flush members (class not compiled).
    sub_file("TFE_Jedi/Renderer/jediRenderer.cpp", [
        ("s_sectorRendererCache[renderer] = new TFE_Sectors_GPU();",
         "s_sectorRendererCache[renderer] = nullptr; /* ZZ9000: no GPU */"),
        ("sectorRendererGpu->flushTextureCache();",
         "/* ZZ9000: no GPU sector renderer */"),
        ("sectorRendererGpu->flushCache();",
         "/* ZZ9000: no GPU sector renderer */"),
    ])

    # igame: Outlaws is out of scope (not compiled)
    sub_file("TFE_Game/igame.cpp", [
        ("game = new TFE_Outlaws::Outlaws();",
         "game = nullptr; /* ZZ9000: Outlaws out of scope */"),
    ])

    # Engine breadcrumbs for startup and level-loading milestones.
    sub_file("TFE_DarkForces/darkForcesMain.cpp", [
        ('#include "darkForcesMain.h"',
         '#include "darkForcesMain.h"\n#include "zzdf_crumbs.h"'),
        ("\t\tloadAgentAndLevelData();\n\t\tlsystem_init();",
         "\t\tloadAgentAndLevelData();\n"
         "\t\tzzdf_crumb(0xE040, \"level list loaded\");\n"
         "\t\tlsystem_init();"),
        ("\t\tsetInitialLevel(startLevel);\n",
         "\t\tsetInitialLevel(startLevel);\n"
         "\t\tzzdf_crumb(0xE050, \"SECBASE requested\");\n"),
    ])

    sub_file("TFE_DarkForces/darkForcesMain.cpp", [
        ("\t\t\tconst char* msg = TFE_System::getMessage(TFE_MSG_SAVE);\n",
         "\t\t\tconst char* msg = TFE_System::getMessage(TFE_MSG_SAVE);\n"
         "\t\t\tif (!msg) { msg = \"GAME SAVED\"; }   /* ZZDF */\n"),
    ])
    sub_file("TFE_DarkForces/mission.cpp", [
        ('#include "mission.h"',
         '#include "mission.h"\n#include "zzdf_crumbs.h"'),

        ("\t\t\t\t\tif (action == ESC_CONFIG)\n"
         "\t\t\t\t\t{\n"
         "\t\t\t\t\t\tTFE_System::postSystemUiRequest();\n"
         "\t\t\t\t\t}\n",
         "\t\t\t\t\tif (action == ESC_CONFIG)\n"
         "\t\t\t\t\t{\n"
         "\t\t\t\t\t\tTFE_System::postSystemUiRequest();\n"
         "\t\t\t\t\t\tzzdf_cfgscreen_request();\n"
         "\t\t\t\t\t}\n"),
        # E051 just before level_load, E060 only if it returned true
        ("\t\t\t\t\tif (level_load(levelName, s_agentData[s_agentId].difficulty))\n"
         "\t\t\t\t\t{\n",
         "\t\t\t\t\tzzdf_crumb(0xE051, \"level_load entered\");\n"
         "\t\t\t\t\tif (level_load(levelName, s_agentData[s_agentId].difficulty))\n"
         "\t\t\t\t\t{\n"
         "\t\t\t\t\t\tzzdf_crumb(0xE060, \"SECBASE level_load OK\");\n"
         "\t\t\t\t\t\tzzdf_log_heap(\"level_load\");\n"),
        # E070 after mission_createRenderDisplay + hud_startup(JFALSE)
        ("\t\t\t\t\t\tmission_createRenderDisplay();\n"
         "\t\t\t\t\t\thud_startup(JFALSE);\n",
         "\t\t\t\t\t\tmission_createRenderDisplay();\n"
         "\t\t\t\t\t\thud_startup(JFALSE);\n"
         "\t\t\t\t\t\tzzdf_crumb(0xE070, \"mission/player/render init OK\");\n"),

        ("\t\t\t\telse if (s_missionMode == MISSION_MODE_MAIN)\n"
         "\t\t\t\t{\n"
         "\t\t\t\t\t// TFE - Level Script Support.\n"
         "\t\t\t\t\tupdateLevelScript(fixed16ToFloat(s_deltaTime));",
         "\t\t\t\telse if (s_missionMode == MISSION_MODE_MAIN)\n"
         "\t\t\t\t{\n"
         "\t\t\t\t\tif (!zzdf_first_frame_seen()) zzdf_crumb(0xE080, \"first main-task frame\");\n"
         "\t\t\t\t\t// TFE - Level Script Support.\n"
         "\t\t\t\t\tupdateLevelScript(fixed16ToFloat(s_deltaTime));"),
        ("\t\t\t// vgaSwapBuffers() in the DOS code.\n"
         "\t\t\tTFE_Jedi::endRender();\n"
         "\t\t\tif (!cutscene_isPlaying())\n"
         "\t\t\t{\n"
         "\t\t\t\t// Only do it if you are not in cutscene mode\n"
         "\t\t\t\tvfb_swap();\n"
         "\t\t\t}",
         "\t\t\t// vgaSwapBuffers() in the DOS code.\n"
         "\t\t\tTFE_Jedi::endRender();\n"
         "\t\t\tif (!cutscene_isPlaying())\n"
         "\t\t\t{\n"
         "\t\t\t\t// Only do it if you are not in cutscene mode\n"
         "\t\t\t\tvfb_swap();\n"
         "\t\t\t\tif (s_missionMode == MISSION_MODE_MAIN && s_playerEye)\n"
         "\t\t\t\t\t// Hash the complete rendered frame at the active resolution.\n"
         "\t\t\t\t\t{ u32 zw = 0, zh = 0; vfb_getResolution(&zw, &zh);\n"
         "\t\t\t\t\t  zzdf_first_frame_evidence(s_framebuffer, zw, zh,\n"
         "\t\t\t\t\t                            vfb_getPalette()); }\n"
         "\t\t\t}"),
    ])

    sub_file("TFE_Audio/midiPlayer.cpp", [
        # --- includes: SDL out, our device in
        ('#include <SDL_mutex.h>\n#include <SDL_thread.h>\n',
         '// ZZDF: no SDL on bare metal. One core, cooperative: the\n'
         '// mutexes become no-ops and the thread becomes one pass.\n'
         'typedef int SDL_mutex;\n'
         'typedef int SDL_Thread;\n'
         '#define SDL_CreateMutex()    ((SDL_mutex*)1)\n'
         '#define SDL_DestroyMutex(m)  ((void)0)\n'
         '#define SDL_LockMutex(m)     ((void)0)\n'
         '#define SDL_UnlockMutex(m)   ((void)0)\n'
         '#define SDL_WaitThread(t,r)  ((void)0)\n'
         '#include "zzdf_midi_device.h"\n'),

        ('#include <TFE_Audio/MidiSynth/soundFontDevice.h>\n'
         '#include <TFE_Audio/MidiSynth/fm4Opl3Device.h>\n',
         '#include <TFE_Audio/MidiSynth/soundFontDevice.h>\n'),
        ('#include "audioDevice.h"\n', ''),
        # --- device allocation
        ('\t\t\tcase MIDI_TYPE_SF2:\n'
         '\t\t\t\ts_midiDevice = new SoundFontDevice();\n'
         '\t\t\t\tbreak;\n'
         '\t\t\tcase MIDI_TYPE_OPL3:\n'
         '\t\t\t\ts_midiDevice = new Fm4Opl3Device();\n'
         '\t\t\t\tbreak;\n'
         '\t\t\tdefault:\n'
         '\t\t\t\tTFE_System::logWrite(LOG_ERROR, "Midi", "Invalid midi type selected: %d", (s32)type);\n'
         '\t\t\t\ts_midiDevice = new Fm4Opl3Device();\n'
         '\t\t\t\tbreak;\n',
         '\t\t\t// ZZDF: the 68k chooses. Either the notes leave this\n'
         '\t\t\t// blob as MIDI for camd (out.0, real synth), or TFE\'s own\n'
         '\t\t\t// SoundFont device renders them into our PCM mix.\n'
         '\t\t\tdefault:\n'
         '\t\t\t\tif (zzdf_midi_use_sf2()) { s_midiDevice = new SoundFontDevice(); }\n'
         '\t\t\t\telse                      { s_midiDevice = new ZZDFRingMidiDevice(); }\n'
         '\t\t\t\tbreak;\n'),
        # --- no thread to create
        ('\t\ts_thread = SDL_CreateThread(midiUpdateFunc, "TFE_MidiThread", nullptr);',
         '\t\ts_thread = (SDL_Thread*)1;   // ZZDF: driven from the game loop'),
        # --- the thread body becomes ONE pass
        ('\t\tbool runThread  = true;\n'
         '\t\tbool wasPlaying = false;\n'
         '\t\tbool isPlaying  = false;\n'
         '\t\tbool isPaused = false;\n'
         '\t\ts32 loopStart = -1;\n'
         '\t\tu64 localTime = 0;\n'
         '\t\tu64 localTimeCallback = 0;\n'
         '\t\tf64 dt = 0.0;\n'
         '\t\twhile (runThread)\n'
         '\t\t{\n',
         '\t\t// ZZDF: ONE pass. What the loop used to carry across its\n'
         '\t\t// iterations is file-static above, or it would reset on\n'
         '\t\t// every call and the accumulator would never fill.\n'
         '\t\t{\n'),
        ('\t\t\tSDL_UnlockMutex(s_midiThreadMutex);\n'
         '\t\t\trunThread = s_runMusicThread.load();\n'
         '\t\t};\n',
         '\t\t\tSDL_UnlockMutex(s_midiThreadMutex);\n'
         '\t\t}\n'),
        # --- the hoisted state, declared just before the function
        ('\tint midiUpdateFunc(void* userData)\n\t{\n',
         '\t// ZZDF: loop-carried state of the former thread.\n'
         '\tstatic bool isPaused = false;\n'
         '\tstatic u64  localTimeCallback = 0;\n'
         '\n'
         '\tint midiUpdateFunc(void* userData)\n\t{\n'),

        ('\t\t\t\twhile (s_midiCallback.callback && s_midiCallback.accumulator >= s_midiCallback.timeStep)\n'
         '\t\t\t\t{\n'
         '\t\t\t\t\ts_midiCallback.callback();\n'
         '\t\t\t\t\ts_midiCallback.accumulator -= s_midiCallback.timeStep;\n'
         '\t\t\t\t\ts_curNoteTime += s_midiCallback.timeStep;\n'
         '\t\t\t\t}\n',
         '\t\t\t\t// ZZDF: cooperative MIDI scheduler with a bounded catch-up loop.\n'
         '\t\t\t\t// DROPDEBT optionally discards excess accumulated steps.\n'
         '\t\t\t\ts32 zzdfMax = zzdf_midi_maxstep();\n'
         '\t\t\t\ts32 zzdfStep = 0;\n'
         '\t\t\t\tfor (; zzdfStep < zzdfMax && s_midiCallback.callback &&\n'
         '\t\t\t\t     s_midiCallback.accumulator >= s_midiCallback.timeStep; zzdfStep++)\n'
         '\t\t\t\t{\n'
         '\t\t\t\t\t// how far past its deadline this tick runs.\n'
         '\t\t\t\t\tzzdf_sched_tick((unsigned int)((s_midiCallback.accumulator -\n'
         '\t\t\t\t\t\ts_midiCallback.timeStep) * 1000000.0));\n'
         '\t\t\t\t\ts_midiCallback.callback();\n'
         '\t\t\t\t\ts_midiCallback.accumulator -= s_midiCallback.timeStep;\n'
         '\t\t\t\t\ts_curNoteTime += s_midiCallback.timeStep;\n'
         '\t\t\t\t\tzzdf_imuse_step();\n'
         '\t\t\t\t}\n'
         '\t\t\t\t// Track catch-up passes for diagnostics.\n'
         '\t\t\t\tif (zzdfStep > 1) { zzdf_sched_multi(); }\n'
         '\t\t\t\t// DROPDEBT discards any remaining whole sequencer steps.\n'
         '\t\t\t\tif (zzdf_midi_dropdebt() && zzdfStep >= zzdfMax &&\n'
         '\t\t\t\t    s_midiCallback.accumulator >= s_midiCallback.timeStep)\n'
         '\t\t\t\t{\n'
         '\t\t\t\t\ts32 zzdfLost = 0;\n'
         '\t\t\t\t\twhile (s_midiCallback.accumulator >= s_midiCallback.timeStep)\n'
         '\t\t\t\t\t{\n'
         '\t\t\t\t\t\ts_midiCallback.accumulator -= s_midiCallback.timeStep;\n'
         '\t\t\t\t\t\tzzdfLost++;\n'
         '\t\t\t\t\t}\n'
         '\t\t\t\t\tzzdf_midi_step_dropped(zzdfLost);\n'
         '\t\t\t\t}\n'),
    ])

    sub_file("TFE_Jedi/IMuse/imuse.cpp", [
        
        
        ('#include "imuse.h"',
         'extern "C" {\n'
         'void zzdf_sus_jump(void);\n'
         'void zzdf_sus_captured(void);\n'
         'void zzdf_sus_released(void);\n'
         'void zzdf_sus_purged(void);\n'
         'void zzdf_sus_net_safety(void);\n'
         'void zzdf_sus_alloc_fail(int logChan, int physChan, int note);\n'
         '}\n'
         '#include "imuse.h"'),
    ])

    sub_file("TFE_Jedi/IMuse/imuse.cpp", [
        # declarations
        ('#include "imuse.h"',
         'extern "C" {\n'
         'void zzdf_noteon_prepare(int src, int logChan, int physChan,\n'
         '                         int shared, int maskBefore,\n'
         '                         int mask2Before, int playerId);\n'
         'void zzdf_assign_bump(void);\n'
         '}\n'
         '#include "imuse.h"'),

        ('\tvoid ImHandleNoteOn(ImMidiChannel* channel, s32 instrumentId, s32 velocity)\n'
         '\t{\n'
         '\t\tif (!channel) { return; }\n'
         '\n'
         '\t\tu32 channelMask = c_channelMask[channel->channelId];\n',
         '\tvoid ImHandleNoteOn(ImMidiChannel* channel, s32 instrumentId, s32 velocity)\n'
         '\t{\n'
         '\t\tif (!channel) { return; }\n'
         '\n'
         '\t\tu32 channelMask = c_channelMask[channel->channelId];\n'
         '\t\t// ZZDF: instrumentation only.\n'
         '\t\t{\n'
         '\t\t\tconst int zzLog = (channel->channel && channel->player)\n'
         '\t\t\t\t? (int)(channel->channel - channel->player->channels) : 0xF;\n'
         '\t\t\tzzdf_noteon_prepare(0 /*SRC_IMHANDLE*/, zzLog,\n'
         '\t\t\t\t(int)channel->channelId,\n'
         '\t\t\t\tchannel->sharedPart ? 1 : 0,\n'
         '\t\t\t\t(channel->instrumentMask[instrumentId]  & channelMask) ? 1 : 0,\n'
         '\t\t\t\t(channel->instrumentMask2[instrumentId] & channelMask) ? 1 : 0,\n'
         '\t\t\t\tzzdf_player_index(channel->player));\n'
         '\t\t}\n'),
        # ImMidi_DrumOut_NoteOn: the percussion path, declared as such
        # so it is never confused with a melodic part on channel 9.
        ('\t\tImNoteOn(imOutDrumChannel, instrumentId, velocity);\n'
         '\t}\n',
         '\t\tzzdf_noteon_prepare(1 /*SRC_DRUMOUT*/, 0xF,\n'
         '\t\t\t(int)imOutDrumChannel, 0, 0, 0, 0xFF);\n'
         '\t\tImNoteOn(imOutDrumChannel, instrumentId, velocity);\n'
         '\t}\n'),
        # assignment generation: every change of channel->data. If a
        # melodic double arrives under a different generation from the
        # Note On before it, the note changed owner in between - which
        # is the hypothesis under test.
        ('\t\tchannel->data = midiChannel;\n'
         '\t\tmidiChannel->player  = player;\n',
         '\t\tchannel->data = midiChannel;\n'
         '\t\tzzdf_assign_bump();   // ZZDF\n'
         '\t\tmidiChannel->player  = player;\n'),
        ('\t\t\tchannel->data = nullptr;\n'
         '\t\t\tchannel->partStatus = 0;\n',
         '\t\t\tif (channel->data) { zzdf_assign_bump(); }   // ZZDF\n'
         '\t\t\tchannel->data = nullptr;\n'
         '\t\t\tchannel->partStatus = 0;\n'),
    ])

    # The player index, appended inside the namespace: only code in
    # imuse.cpp can walk s_midiPlayerList.
    append_file("TFE_Jedi/IMuse/imuse.cpp",
        '\n'
        '// ZZDF: stable player identifier for diagnostics.\n'
        '// Pointers are not stored in the compact event log.\n'
        '// would not survive the 8 bits the entry has for it.\n'
        'namespace TFE_Jedi\n'
        '{\n'
        '\tint zzdf_player_index(ImMidiPlayer* p)\n'
        '\t{\n'
        '\t\tint i = 0;\n'
        '\t\tImMidiPlayer* cur = s_midiPlayerList;\n'
        '\t\twhile (cur && i < 254) { if (cur == p) { return i; } cur = cur->next; i++; }\n'
        '\t\treturn 0xFF;\n'
        '\t}\n'
        '}\n',
        marker="zzdf_player_index(ImMidiPlayer* p)")

    # and its forward declaration, before the first use
    sub_file("TFE_Jedi/IMuse/imuse.cpp", [
        ('\tvoid ImHandleNoteOn(ImMidiChannel* channel, s32 instrumentId, s32 velocity);',
         '\tint  zzdf_player_index(ImMidiPlayer* p);   // ZZDF\n'
         '\tvoid ImHandleNoteOn(ImMidiChannel* channel, s32 instrumentId, s32 velocity);'),
    ])

    # imMidiPlayer.cpp: the other channel->data teardown.
    sub_file("TFE_Jedi/IMuse/imMidiPlayer.cpp", [
        # This patch also prepends to the same include and runs
        # AFTER this block, so anchoring on its declaration would miss.
        # Both prepend, so the order between them does not matter.
        ('#include "imuse.h"',
         'extern "C" void zzdf_assign_bump(void);\n'
         '#include "imuse.h"'),
        ('\t\t\tdata->channel = nullptr;\n'
         '\t\t\tchannel->data = nullptr;\n',
         '\t\t\tdata->channel = nullptr;\n'
         '\t\t\tzzdf_assign_bump();   // ZZDF\n'
         '\t\t\tchannel->data = nullptr;\n'),
    ])

    # imMidiCmd.cpp: the choke point. Every Note On passes here, and
    # here we can ask the low-level tracker whether the DEVICE already
    # held this note on this physical channel. iMuse's masks say what
    # iMuse believes; the tracker says what actually went out.
    sub_file("TFE_Jedi/IMuse/imMidiCmd.cpp", [
        ('#include "imMidiCmd.h"',
         'extern "C" {\n'
         'void zzdf_noteon_event(int physChan, int note, int velocity,\n'
         '                       int trackerBefore);\n'
         'int  zzdf_instr_bit(int channel, int note);\n'
         '}\n'
         '#include "imMidiCmd.h"'),
        ('\tvoid ImNoteOn(s32 channelId, s32 instrId, s32 velocity)\n\t{',
         '\tvoid ImNoteOn(s32 channelId, s32 instrId, s32 velocity)\n\t{\n'
         '\t\t// ZZDF: sample state before sending the message.\n'
         '\t\tzzdf_noteon_event((int)channelId, (int)instrId, (int)velocity,\n'
         '\t\t\tzzdf_instr_bit((int)channelId, (int)instrId));\n'),
    ])
    require_in("TFE_Jedi/IMuse/imuse.cpp", [
        "zzdf_noteon_prepare(0 /*SRC_IMHANDLE*/",
        "zzdf_noteon_prepare(1 /*SRC_DRUMOUT*/",
        "zzdf_assign_bump();",
        "int zzdf_player_index(ImMidiPlayer* p)",
    ], "note-on journal, imuse")
    require_in("TFE_Jedi/IMuse/imMidiCmd.cpp", [
        "zzdf_noteon_event((int)channelId,",
    ], "note-on journal, ImNoteOn choke point")
    require_in("TFE_Jedi/IMuse/imMidiPlayer.cpp", [
        "zzdf_assign_bump();",
    ], "assignment generation")

    sub_file("TFE_Jedi/IMuse/imuse.cpp", [
        ('\tvoid ImHandleNoteOff(ImMidiChannel* midiChannel, s32 instrumentId);',
         '\textern "C" void zzdf_imoff_check(int logical, int note, int phys,\n'
         '\t                                 int curHeld, unsigned int allMask,\n'
         '\t                                 int sustain);   // ZZDF\n'
         '\textern "C" void zzdf_imoff_nodata(int logical, int note);  // ZZDF\n'
         '\tvoid ImHandleNoteOff(ImMidiChannel* midiChannel, s32 instrumentId);'),
        ('\t\tif (channel->partStatus)\n'
         '\t\t{\n'
         '\t\t\tif (channel->data)\n'
         '\t\t\t{\n'
         '\t\t\t\tImHandleNoteOff(channel->data, instrumentId);\n'
         '\t\t\t}\n'
         '\t\t}\n',
         '\t\tif (channel->partStatus)\n'
         '\t\t{\n'
         '\t\t\tif (channel->data)\n'
         '\t\t\t{\n'
         '\t\t\t\t// ZZDF: observe only.\n'
         '\t\t\t\tzzdf_imoff_check((int)channelId, instrumentId,\n'
         '\t\t\t\t\t(int)channel->data->channelId,\n'
         '\t\t\t\t\t(channel->data->instrumentMask[instrumentId] &\n'
         '\t\t\t\t\t c_channelMask[channel->data->channelId]) ? 1 : 0,\n'
         '\t\t\t\t\ts_midiInstrumentChannelMask[instrumentId] |\n'
         '\t\t\t\t\t s_midiInstrumentChannelMaskShared[instrumentId],\n'
         '\t\t\t\t\t(int)channel->data->sustain);\n'
         '\t\t\t\tImHandleNoteOff(channel->data, instrumentId);\n'
         '\t\t\t}\n'
         '\t\t\telse\n'
         '\t\t\t{\n'
         '\t\t\t\tzzdf_imoff_nodata((int)channelId, instrumentId);   // ZZDF\n'
         '\t\t\t}\n'
         '\t\t}\n'),
    ])
    # ------------------------------------------------------------------
    # soundFontDevice.cpp: TFE's SoundFont synth, AS UPSTREAM SHIPS
    # IT. Three bare-metal changes, nothing in the synthesis:
    #   - the output list: upstream scans SoundFonts/ for *.sf2; MemFS
    #     is flat and refuses directory scans, so the list is the ONE
    #     file the launcher staged, under the fixed name SOUNDFNT.SF2.
    #     beginStream() is untouched: it builds SoundFonts/SOUNDFNT.sf2
    #     and fopen() resolves the basename in MemFS.
    #   - the sample rate: upstream renders at 44100 because its mixer
    #     runs there. Ours runs at ZZDF_PCM_RATE (11025) and TSF must
    #     render at the rate it is played at, or pitch and tempo drop
    #     by four.
    #   - two observers: load result and active voice count.
    sub_file("TFE_Audio/MidiSynth/soundFontDevice.cpp", [
        ('#include "soundFontDevice.h"\n',
         '#include "soundFontDevice.h"\n'
         'extern "C" void zzdf_sf2_loaded(int ok);        // ZZDF\n'
         'extern "C" void zzdf_sf2_voices(int active);    // ZZDF\n'),
        ('\t\tSFD_SAMPLE_RATE  = 44100,\n',
         '\t\tSFD_SAMPLE_RATE  = 11025,   // ZZDF: = ZZDF_PCM_RATE, our mix rate\n'),
        ('\t\tif (m_outputs.empty())\n'
         '\t\t{\n'
         '\t\t\tchar dir[TFE_MAX_PATH];\n'
         '\t\t\tconst char* programDir = TFE_Paths::getPath(PATH_PROGRAM);\n'
         '\t\t\tsprintf(dir, "%s", "SoundFonts/");\n'
         '\t\t\tif (!TFE_Paths::mapSystemPath(dir))\n'
         '\t\t\t\tsprintf(dir, "%sSoundFonts/", programDir);\n'
         '\n'
         '\t\t\tFileUtil::readDirectory(dir, "sf2", m_outputs);\n'
         '\t\t\t// Remove the extension.\n'
         '\t\t\tfor (size_t i = 0; i < m_outputs.size(); i++)\n'
         '\t\t\t{\n'
         '\t\t\t\tchar name[TFE_MAX_PATH];\n'
         '\t\t\t\tFileUtil::getFileNameFromPath(m_outputs[i].c_str(), name);\n'
         '\t\t\t\tm_outputs[i] = name;\n'
         '\t\t\t}\n'
         '\t\t}\n',
         '\t\t// ZZDF: MemFS is flat - the one SoundFont the launcher staged.\n'
         '\t\tif (m_outputs.empty())\n'
         '\t\t{\n'
         '\t\t\tm_outputs.push_back("SOUNDFNT");\n'
         '\t\t}\n'),
        ('\t\tm_soundFont = tsf_load_filename(filePath);\n',
         '\t\tm_soundFont = tsf_load_filename(filePath);\n'
         '\t\tzzdf_sf2_loaded(m_soundFont ? 1 : 0);   // ZZDF\n'),
        ('\t\ttsf_render_float(m_soundFont, buffer, sampleCount);\n',
         '\t\ttsf_render_float(m_soundFont, buffer, sampleCount);\n'
         '\t\tzzdf_sf2_voices(tsf_active_voice_count(m_soundFont));   // ZZDF\n'),
    ])
    require_in("TFE_Audio/MidiSynth/soundFontDevice.cpp", [
        'm_outputs.push_back("SOUNDFNT");',
        "SFD_SAMPLE_RATE  = 11025,",
        "zzdf_sf2_loaded(m_soundFont ? 1 : 0);",
        "zzdf_sf2_voices(tsf_active_voice_count(m_soundFont));",
    ], "SoundFont device")
    require_in("TFE_Audio/midiPlayer.cpp", [
        "if (zzdf_midi_use_sf2()) { s_midiDevice = new SoundFontDevice(); }",
        "#include <TFE_Audio/MidiSynth/soundFontDevice.h>",
    ], "device choice")

    require_in("TFE_Jedi/IMuse/imuse.cpp", [
        "zzdf_imoff_check((int)channelId, instrumentId,",
        "zzdf_imoff_nodata((int)channelId, instrumentId);",
        'extern "C" void zzdf_imoff_nodata(int logical, int note);',
    ], "suppressed note off")

    sub_file("TFE_Jedi/IMuse/imuse.cpp", [
        # 1. every jump that can arm sustained sounds
        ('\tvoid ImJumpSustain(ImMidiPlayer* player, u8* sndData, '
         'ImPlayerData* playerPrevData, ImPlayerData* playerNextData)\n'
         '\t{\n',
         '\tvoid ImJumpSustain(ImMidiPlayer* player, u8* sndData, '
         'ImPlayerData* playerPrevData, ImPlayerData* playerNextData)\n'
         '\t{\n'
         '\t\tzzdf_sus_jump();   // ZZDF: instrument, see build_next.py\n'),
        # 2. the allocation site: success, and the failure that is the
        #    whole point of the run. The physical channel is resolved
        #    here because only here are both numbers in scope.
        ('\t\t\tImSustainedSound* sound = s_imFreeSustainedSounds;\n'
         '\t\t\tif (!sound)\n'
         '\t\t\t{\n'
         '\t\t\t\tIM_LOG_ERR("%s", "su unable to alloc Sustain...");\n'
         '\t\t\t\treturn;\n'
         '\t\t\t}\n',
         '\t\t\tImSustainedSound* sound = s_imFreeSustainedSounds;\n'
         '\t\t\tif (!sound)\n'
         '\t\t\t{\n'
         '\t\t\t\t// ZZDF: the note has ALREADY lost its bit and its\n'
         '\t\t\t\t// place in the count, three lines above, and no Note\n'
         '\t\t\t\t// Off is sent here. Record it with the channel number\n'
         '\t\t\t\t// that actually leaves for camd, so the 68k report can\n'
         '\t\t\t\t// be searched for the same pair.\n'
         '\t\t\t\t{\n'
         '\t\t\t\t\tImMidiChannel* zzdfCh = player ? '
         'player->channels[channelId].data : nullptr;\n'
         '\t\t\t\t\tzzdf_sus_alloc_fail((int)channelId,\n'
         '\t\t\t\t\t\tzzdfCh ? (int)zzdfCh->channelId : 0xF,\n'
         '\t\t\t\t\t\t(int)instrumentId);\n'
         '\t\t\t\t}\n'
         '\t\t\t\tIM_LOG_ERR("%s", "su unable to alloc Sustain...");\n'
         '\t\t\t\treturn;\n'
         '\t\t\t}\n'
         '\t\t\tzzdf_sus_captured();   // ZZDF\n'),
        # 3. the countdown that returns an entry to the pool
        ('\t\t\tif (sustainedSound->curTick < 0)\n'
         '\t\t\t{\n'
         '\t\t\t\tImMidiNoteOff(sustainedSound->midiPlayer, '
         'sustainedSound->channelId, sustainedSound->instrumentId, 0);\n',
         '\t\t\tif (sustainedSound->curTick < 0)\n'
         '\t\t\t{\n'
         '\t\t\t\tzzdf_sus_released();   // ZZDF\n'
         '\t\t\t\tImMidiNoteOff(sustainedSound->midiPlayer, '
         'sustainedSound->channelId, sustainedSound->instrumentId, 0);\n'),
        # 4. the safety net. If the pool theory is right this stays at
        #    zero even while ALLOC_FAIL climbs - that contrast IS the
        #    proof that the net is being bypassed rather than failing.
        ('\t\t\t\t\t\t\tIM_LOG_WRN("missing note %d on chan %d...", i, c);\n',
         '\t\t\t\t\t\t\tIM_LOG_WRN("missing note %d on chan %d...", i, c);\n'
         '\t\t\t\t\t\t\tzzdf_sus_net_safety();   // ZZDF\n'),
    ])
    require_in("TFE_Jedi/IMuse/imuse.cpp", [
        "zzdf_sus_jump();",
        "zzdf_sus_alloc_fail((int)channelId,",
        "zzdf_sus_captured();",
        "zzdf_sus_released();",
        "zzdf_sus_net_safety();",
    ], "sustained-sound instrument")

    sub_file("TFE_Jedi/IMuse/imMidiPlayer.cpp", [
        ('#include "imuse.h"',
         'extern "C" void zzdf_sus_purged(void);\n'
         '#include "imuse.h"'),
        ('\t\t\tif (player == sustainedSound->midiPlayer)\n'
         '\t\t\t{\n'
         '\t\t\t\tImMidiNoteOff(sustainedSound->midiPlayer, '
         'sustainedSound->channelId, sustainedSound->instrumentId, 0);\n',
         '\t\t\tif (player == sustainedSound->midiPlayer)\n'
         '\t\t\t{\n'
         '\t\t\t\tzzdf_sus_purged();   // ZZDF\n'
         '\t\t\t\tImMidiNoteOff(sustainedSound->midiPlayer, '
         'sustainedSound->channelId, sustainedSound->instrumentId, 0);\n'),
    ])
    require_in("TFE_Jedi/IMuse/imMidiPlayer.cpp", [
        "zzdf_sus_purged();",
    ], "player-release purge counter")

    sub_file("TFE_Audio/midiPlayer.cpp", [
        ('\t\t\tif (msgType == MID_NOTE_OFF || (msgType == MID_NOTE_ON && arg2 == 0))'
         '\t// note on + velocity = 0 is the same as note off.\n'
         '\t\t\t{\n'
         '\t\t\t\ts_instrOn[instr].channelMask  &= ~(1 << channel);\n'
         '\t\t\t\ts_instrOn[instr].time[channel] = 0.0;\n'
         '\t\t\t}\n'
         '\t\t\telse  // MID_NOTE_ON\n'
         '\t\t\t{\n'
         '\t\t\t\ts_instrOn[instr].channelMask  |= (1 << channel);\n'
         '\t\t\t\ts_instrOn[instr].time[channel] = s_curNoteTime;\n'
         '\t\t\t}\n',
         '\t\t\tif (msgType == MID_NOTE_OFF || (msgType == MID_NOTE_ON && arg2 == 0))'
         '\t// note on + velocity = 0 is the same as note off.\n'
         '\t\t\t{\n'
         '\t\t\t\t// ZZDF: an Off for a bit already clear.\n'
         '\t\t\t\tif (!(s_instrOn[instr].channelMask & (1 << channel)))\n'
         '\t\t\t\t{\n'
         '\t\t\t\t\tzzdf_instr_off_orphan((int)channel, (int)instr);\n'
         '\t\t\t\t}\n'
         '\t\t\t\ts_instrOn[instr].channelMask  &= ~(1 << channel);\n'
         '\t\t\t\ts_instrOn[instr].time[channel] = 0.0;\n'
         '\t\t\t}\n'
         '\t\t\telse  // MID_NOTE_ON\n'
         '\t\t\t{\n'
         '\t\t\t\t// ZZDF: a second On with no Off between.\n'
         '\t\t\t\t// One bit, two voices on the module: the next single\n'
         '\t\t\t\t// Off clears the tracker while the synth may not be.\n'
         '\t\t\t\tif (s_instrOn[instr].channelMask & (1 << channel))\n'
         '\t\t\t\t{\n'
         '\t\t\t\t\tzzdf_instr_on_double((int)channel, (int)instr);\n'
         '\t\t\t\t}\n'
         '\t\t\t\ts_instrOn[instr].channelMask  |= (1 << channel);\n'
         '\t\t\t\ts_instrOn[instr].time[channel] = s_curNoteTime;\n'
         '\t\t\t}\n'),
    ])
    # expose the tracker bit so imMidiCmd.cpp can ask, at the
    # moment a Note On is emitted, whether the DEVICE already held that
    # note on that physical channel. s_instrOn is static to this file,
    # so the query has to live here.
    append_file("TFE_Audio/midiPlayer.cpp",
        '\n'
        '// ZZDF: read-only view of the low-level note tracker.\n'
        '// This reports device-side note state.\n'
        ''
        'extern "C" int zzdf_instr_bit(int channel, int note)\n'
        '{\n'
        '\tif (channel < 0 || channel > 15 || note < 0 ||\n'
        '\t    note >= MIDI_INSTRUMENT_COUNT) { return 0; }\n'
        '\treturn (TFE_MidiPlayer::zzdf_instrOnMask(note) & (1u << channel)) ? 1 : 0;\n'
        '}\n',
        marker='int zzdf_instr_bit(int channel, int note)')

    # the accessor inside the namespace, where s_instrOn is visible
    sub_file("TFE_Audio/midiPlayer.cpp", [
        ('\tvoid detectHangingNotes()\n\t{',
         '\t// ZZDF: expose tracker state to zzdf_instr_bit().\n'
         '\tu32 zzdf_instrOnMask(s32 instr)\n'
         '\t{\n'
         '\t\treturn s_instrOn[instr].channelMask;\n'
         '\t}\n'
         '\n'
         '\tvoid detectHangingNotes()\n\t{'),
    ])
    require_in("TFE_Audio/midiPlayer.cpp", [
        "zzdf_instr_off_orphan((int)channel, (int)instr);",
        "zzdf_instr_on_double((int)channel, (int)instr);",
        "u32 zzdf_instrOnMask(s32 instr)",
        "int zzdf_instr_bit(int channel, int note)",
    ], "s_instrOn tracker counters and query")

    sub_file("TFE_Jedi/Renderer/screenDraw.cpp", [
        ("\t\ts32 x1 = x0 + floor16(mul16(intToFixed16(texture->width), xScale));\n"
         "\t\ts32 y1 = y0 + floor16(mul16(intToFixed16(texture->height), yScale));\n"
         "\t\tfixed16_16 u0 = 0, v1 = 0;\n"
         "\t\tfixed16_16 v0 = intToFixed16(texture->height) - 1;\n"
         "\t\tfixed16_16 uStep = div16(intToFixed16(texture->width), intToFixed16(x1 - x0));\n"
         "\t\tfixed16_16 vStep = -div16(intToFixed16(texture->height), intToFixed16(y1 - y0));\n",
         "\t\t/* ZZDF: use width-1/height-1 endpoints, matching\n"
         "\t\t   blitTextureToScreenScaled() in this same file. With the\n"
         "\t\t   inclusive loop below, the previous 'width' endpoints\n"
         "\t\t   sampled one texel past the glyph on the last column and\n"
         "\t\t   row at an exact 2x scale. */\n"
         "\t\ts32 x1 = x0 + floor16(mul16(intToFixed16(texture->width - 1), xScale));\n"
         "\t\ts32 y1 = y0 + floor16(mul16(intToFixed16(texture->height - 1), yScale));\n"
         "\t\tfixed16_16 u0 = 0, v1 = 0;\n"
         "\t\tfixed16_16 v0 = intToFixed16(texture->height - 1);\n"
         "\t\tfixed16_16 uStep = div16(intToFixed16(texture->width - 1), intToFixed16(x1 - x0));\n"
         "\t\tfixed16_16 vStep = -div16(intToFixed16(texture->height - 1), intToFixed16(y1 - y0));\n"),
    ])
    require_in("TFE_Jedi/Renderer/screenDraw.cpp", [
        "s32 x1 = x0 + floor16(mul16(intToFixed16(texture->width - 1), xScale));\n"
        "\t\ts32 y1 = y0 + floor16(mul16(intToFixed16(texture->height - 1), yScale));\n"
        "\t\tfixed16_16 u0 = 0, v1 = 0;\n"
        "\t\tfixed16_16 v0 = intToFixed16(texture->height - 1);",
        "ZZDF: use width-1/height-1 endpoints",
    ], "scaled-text glyph endpoint fix")
    # And prove the defect is gone: no scaled blitter in this file may
    # still divide by an interval computed from the full width/height.
    _sd = open(os.path.join(ENG, "TFE_Jedi/Renderer/screenDraw.cpp"),
               "r", encoding="utf-8", errors="replace").read()
    if "div16(intToFixed16(texture->width), intToFixed16(x1 - x0))" in _sd:
        die("a scaled blitter still steps over the full width")

    sub_file("TFE_Input/replay.cpp", [
        ("\t\t// Ensure we are always in GPU mode for consistency\n"
         "\t\tTFE_Settings_Graphics* graphicSetting = TFE_Settings::getGraphicsSettings();\n"
         "\t\treplayGraphicsType = graphicSetting->rendererIndex;\n"
         "\t\tgraphicSetting->rendererIndex = 1;\n",
         "\t\t// ZZDF: keep the active software renderer during replay.\n"
         "\t\tTFE_Settings_Graphics* graphicSetting = TFE_Settings::getGraphicsSettings();\n"
         "\t\treplayGraphicsType = graphicSetting->rendererIndex;\n"),
    ])
    require_in("TFE_Input/replay.cpp", [
        "ZZDF: keep the active software renderer during replay",
        "replayGraphicsType = graphicSetting->rendererIndex;",
    ], "replay renderer override removed")
    # And prove it: no ACTIVE assignment of rendererIndex to anything but
    # the restore may survive in this file.
    _rp = open(os.path.join(ENG, "TFE_Input/replay.cpp"),
               "r", encoding="utf-8", errors="replace").read()
    for _bad in ("graphicSetting->rendererIndex = 1;",
                 "rendererIndex = 1;"):
        for _line in _rp.splitlines():
            _t = _line.strip()
            if _t.startswith("//"):
                continue
            if _bad in _t:
                die("replay.cpp still forces rendererIndex: " + _t)
    # The restore itself must still be there, or the setting would leak.
    if "getGraphicsSettings()->rendererIndex = replayGraphicsType;" not in _rp:
        die("replay.cpp lost the rendererIndex restore")

    print("== patch_engine done ==")

ZZDF_IMAGEASSET_H = """#pragma once
// ZZ9000 build: SDL_image removed. Minimal surface type preserving the
// TFE_Image API shape; implementation in platform/zzdf_image_stubs.cpp
// (PNG decode not required for SECBASE). ASCII only.
#include <TFE_System/types.h>
#include <stddef.h>

struct SDL_Surface
{
\tint w, h;
\tint pitch;
\tvoid* pixels;
};

namespace TFE_Image
{
\tvoid init();
\tvoid shutdown();

\tSDL_Surface* get(const char* imagePath);
\tSDL_Surface* loadFromMemory(const u8* buffer, size_t size);
\tvoid free(SDL_Surface* image);
\tvoid freeAll();

\tvoid writeImage(const char* path, u32 width, u32 height, u32* pixelData);

\tsize_t writeImageToMemory(u8* output, u32 srcw, u32 srch, u32 dstw, u32 dsth, const u32* pixelData);
\tvoid readImageFromMemory(SDL_Surface** output, size_t size, const u32* pixelData);
}
"""

ZZDF_ENDIAN_H = """#pragma once
// ZZ9000 target: ARM Cortex-A9 little-endian. TFE_Endian API kept,
// SDL dependency removed (build system replacement). ASCII only.
#include <TFE_System/types.h>
namespace TFE_Endian
{
\tinline u16 le16_to_cpu(u16 v) { return v; }
\tinline u32 le32_to_cpu(u32 v) { return v; }
\tinline u64 le64_to_cpu(u64 v) { return v; }
\tinline u16 cpu_to_le16(u16 v) { return v; }
\tinline u32 cpu_to_le32(u32 v) { return v; }
\tinline u64 cpu_to_le64(u64 v) { return v; }
\tinline u16 be16_to_cpu(u16 v) { return __builtin_bswap16(v); }
\tinline u32 be32_to_cpu(u32 v) { return __builtin_bswap32(v); }
\tinline u64 be64_to_cpu(u64 v) { return __builtin_bswap64(v); }
\tinline f32 lef32_to_cpu(f32 v) { return v; }
}
"""

# ---------------------------------------------------------------- #
def compile_all(sources):
    os.makedirs(OBJ, exist_ok=True)
    inc = ["-I" + ENG, "-I" + os.path.join(ROOT, "platform"),
           "-I" + os.path.join(ROOT, "core1")]
    objs = []
    failures = []
    n_cpp = sum(1 for s in sources if s.endswith(".cpp"))
    print("== %d TFE .cpp units selected (+%d .c) ==" %
          (n_cpp, len(sources) - n_cpp))
    with open(os.path.join(BUILD, "SELECTED_SOURCES.txt"), "w") as f:
        f.write("\n".join(sources) + "\n")

    for rel in sources:
        src = os.path.join(ENG, rel)
        o = os.path.join(OBJ, rel.replace("/", "__") + ".o")
        objs.append(o)
        if os.path.isfile(o) and \
           os.path.getmtime(o) > os.path.getmtime(src):
            continue
        if rel.endswith(".cpp"):
            cmd = [GXX] + CXXFLAGS + inc + ["-c", src, "-o", o]
        else:
            cmd = [GCC] + CFLAGS_ENG + inc + ["-c", src, "-o", o]
        r = run(cmd)
        if r.returncode != 0:
            failures.append((rel, r.stderr))
            if os.path.isfile(o): os.remove(o)
            objs.pop()

    # platform + core1 (per variant: own object dir, own defines)
    os.makedirs(ZZOBJ, exist_ok=True)
    plat = sorted(glob.glob(os.path.join(ROOT, "platform", "*.cpp")))
    core_cpp = sorted(glob.glob(os.path.join(ROOT, "core1", "*.cpp")))
    core_c = sorted(glob.glob(os.path.join(ROOT, "core1", "*.c")))
    core_s = sorted(glob.glob(os.path.join(ROOT, "core1", "*.S")))
    # zzdf_return.c implements the cooperative return: part of the
    # default build, NOT linked into the archived WFE-park variant
    # (where zzdf_saved is only 40 bytes).
    if VARIANT == "legacy_park":
        core_c = [c for c in core_c if os.path.basename(c) != "zzdf_return.c"]
    vdefs = VARIANT_DEFS
    for src in plat + core_cpp:
        o = os.path.join(ZZOBJ, "zz__" + os.path.basename(src) + ".o")
        objs.append(o)
        if not _dep_stale(o, src):
            continue
        r = run([GXX] + CXXFLAGS + vdefs + inc +
                ["-MMD", "-MF", o + ".d", "-c", src, "-o", o])
        if r.returncode != 0:
            failures.append((os.path.basename(src), r.stderr))
            if os.path.isfile(o): os.remove(o)
            objs.pop()
    for src in core_c:
        base = os.path.basename(src)
        o = os.path.join(ZZOBJ, "zz__" + base + ".o")
        flags = CFLAGS_CORE[:]
        if base == "safe_mem.c":     # ZZDoom rule: -O0, no builtins
            flags = MACH + ["-O0", "-ffreestanding",
                            "-fno-builtin-memcpy", "-fno-builtin-memmove",
                            "-fno-builtin-memset"]
        objs.append(o)
        if not _dep_stale(o, src):
            continue
        r = run([GCC] + flags + vdefs + inc +
                ["-MMD", "-MF", o + ".d", "-c", src, "-o", o])
        if r.returncode != 0:
            failures.append((base, r.stderr))
            if os.path.isfile(o): os.remove(o)
            objs.pop()
    for src in core_s:
        o = os.path.join(ZZOBJ, "zz__" + os.path.basename(src) + ".o")
        objs.append(o)
        r = run([GCC] + MACH + vdefs + ["-c", src, "-o", o])
        if r.returncode != 0:
            failures.append((os.path.basename(src), r.stderr))
            objs.pop()
    return objs, failures

def _dep_stale(o, src):
    """Is object o out of date with respect to src AND EVERY HEADER IT
    INCLUDES?

    THIS FUNCTION EXISTS BECAUSE THE BUILD LIED. The rule used to be

        if os.path.isfile(o) and os.path.getmtime(o) > os.path.getmtime(src):
            continue

    which compares the object against its own .c/.cpp and NOTHING ELSE.
    Edit a header - core1/zzdf_config.h, platform/zzdf_present_geom.h -
    and every object that includes it is silently reused. The build then
    prints its usual success line and produces a blob byte-identical to
    the previous one, with the change nowhere in it. It was caught on
    23.09.2026 by raising ZZDF_GAME_H_MAX and getting the same md5 back;
    it could just as easily have been caught on the hardware, which is
    the expensive place to find it.

    gcc already knows the answer: -MMD writes the full prerequisite list
    next to the object. Read it, and rebuild if anything in it is newer.
    No .d file (a first build, or one from before this fix) means
    rebuild, because the honest answer is not known."""
    if not os.path.isfile(o):
        return True
    ot = os.path.getmtime(o)
    if os.path.getmtime(src) >= ot:
        return True
    d = o + ".d"
    if not os.path.isfile(d):
        return True
    try:
        txt = open(d, "r", encoding="utf-8", errors="replace").read()
    except Exception:
        return True
    txt = txt.replace("\\\n", " ")
    if ":" not in txt:
        return True
    for dep in txt.split(":", 1)[1].split():
        if not os.path.isfile(dep):
            return True
        if os.path.getmtime(dep) >= ot:
            return True
    return False


def write_object_manifest(objs):
    """MD5 of every object of this variant. When both the default and
    the archived manifest exist, also write RETURN_TEST_OBJECT_DIFF.txt:
    the core1/platform objects whose code differs between the shipped
    cooperative-return build and the archived WFE park."""
    man = {}
    for o in objs:
        man[os.path.basename(o)] = hashlib.md5(open(o, "rb").read()).hexdigest()
    mp = os.path.join(BUILD, "OBJECT_MD5_%s.txt" % VARIANT)
    with open(mp, "w") as f:
        for k in sorted(man):
            f.write("%s  %s\n" % (man[k], k))
    base_p = os.path.join(BUILD, "OBJECT_MD5_baseline.txt")
    if VARIANT != "baseline" and os.path.isfile(base_p):
        base = {}
        for line in open(base_p):
            h, n = line.split()
            base[n] = h
        diff = []
        for k in sorted(set(man) | set(base)):
            a = base.get(k, "-"); b = man.get(k, "-")
            if a != b:
                diff.append("%-40s baseline=%s %s=%s" % (k, a, VARIANT, b))
        dp = os.path.join(BUILD, "RETURN_TEST_OBJECT_DIFF.txt")
        with open(dp, "w") as f:
            f.write("objects that differ between baseline and %s\n" % VARIANT)
            f.write("(engine objects are shared: any engine line here is a bug)\n")
            f.write("\n".join(diff) + "\n")
        print("== object diff baseline/%s: %d objects differ (see %s) =="
              % (VARIANT, len(diff), dp))
        for d in diff:
            print("   " + d)

def write_ld(assert_on):
    tmpl = open(os.path.join(ROOT, "core1", "zzdf.ld.in")).read()
    a = ('ASSERT(_image_end <= 0x04B00000,\n'
         '  "ZZDF image overflow: 2MB blob reserve exceeded '
         '(shared page at 0x04B00000)")\n'
         'ASSERT(_bss_end <= 0x05F00000,\n'
         '  "ZZDF bss overflow: reaches the Core1 stack floor '
         '(0x05F00000)")') if assert_on else ""
    out = os.path.join(BUILD, "zzdf_census.ld" if not assert_on
                       else "zzdf_runnable.ld")
    open(out, "w", newline="\n").write(tmpl.replace("@ASSERT@", a))
    return out

def variant_stem():
    return {"baseline": "zzdf",
            "legacy_park": "zzdf_legacy_park"}[VARIANT]

def link(objs, census):
    ld = write_ld(assert_on=not census)
    elf = os.path.join(BUILD, variant_stem() + ("_census.elf" if census
                       else ".elf"))
    mapf = elf + ".map"
    # safe_mem.o FIRST (ZZDoom link order rule)
    safe = [o for o in objs if "safe_mem" in o]
    rest = [o for o in objs if "safe_mem" not in o]
    cmd = [GXX] + MACH + ["-nostartfiles", "-T", ld,
                          "-Wl,-Map=" + mapf]
    if not census:
        cmd += ["-Wl,--gc-sections"]
    cmd += safe + rest + ["-lm", "-o", elf]
    r = run(cmd)
    return elf, r

def undefined_from_link(stderr):
    und = set()
    for m in re.finditer(r"undefined reference to [`']([^']+)'", stderr):
        und.add(m.group(1))
    return sorted(und)

# ---------------------------------------------------------------- #

def check_shared_map():
    """Parse zzdf_config.h and prove no two SHARED regions overlap.
    The build FAILS here rather than producing a blob that silently
    corrupts its own key ring or fault dump (an overlap bug)."""
    cfg = os.path.join(ROOT, "core1", "zzdf_config.h")
    txt = open(cfg).read()

    def val(name):
        m = re.search(r"^#define\s+%s\s+(0x[0-9A-Fa-f]+|\d+)" % name,
                      txt, re.M)
        if not m:
            die("shared map check: %s not found in zzdf_config.h" % name)
        return int(m.group(1), 0)

    keyring_len = val("ZZDF_KEYRING_LEN")
    regions = [
        ("SCALARS", val("ZZDF_SH_SCALARS_OFF"), val("ZZDF_SH_SCALARS_SIZE")),
        ("RESV1",   val("ZZDF_SH_RESV1_OFF"),   val("ZZDF_SH_RESV1_SIZE")),
        ("KEYRING", val("ZZDF_KEYRING_OFF"),    keyring_len * 4),
        ("FAULT",   val("ZZDF_FAULT_OFF"),      val("ZZDF_FAULT_SIZE")),
        ("RESV2",   val("ZZDF_SH_RESV2_OFF"),   val("ZZDF_SH_RESV2_SIZE")),
        ("LOGRING", val("ZZDF_LOGRING_OFF"),    val("ZZDF_LOGRING_SIZE")),
    ]
    shared_size = val("ZZDF_SHARED_SIZE")

    # every scalar slot index used anywhere must land inside a region
    # that is meant to hold scalars.
    slots = {}
    for m in re.finditer(r"^#define\s+(SH_[A-Z0-9_]+)\s+(\d+)", txt, re.M):
        slots[m.group(1)] = int(m.group(2))

    print("== SHARED map check ==")
    ordered = sorted(regions, key=lambda r: r[1])
    bad = False
    for i, (name, off, size) in enumerate(ordered):
        end = off + size
        print("   %-8s 0x%03X..0x%03X  (%d bytes)" % (name, off, end - 1, size))
        if end > shared_size:
            print("   FAIL: %s runs past the %d-byte shared page" %
                  (name, shared_size))
            bad = True
        if i + 1 < len(ordered):
            nname, noff, nsize = ordered[i + 1]
            if end > noff:
                print("   FAIL: %s (0x%03X..0x%03X) overlaps %s (0x%03X)" %
                      (name, off, end - 1, nname, noff))
                bad = True

    # two names on one index = a silent collision (alias macros such as
    # SH_ENGINE_FPS_X100 are not numeric and are not in this dict)
    by_idx = {}
    for sname, idx in slots.items():
        by_idx.setdefault(idx, []).append(sname)
    for idx, names in sorted(by_idx.items()):
        if len(names) > 1:
            print("   FAIL: slot index %d used by %s" % (idx, ", ".join(sorted(names))))
            bad = True

    # scalar slots must not stray into a ring
    for sname, idx in sorted(slots.items(), key=lambda kv: kv[1]):
        off = idx * 4
        for rname, roff, rsize in regions:
            if rname in ("SCALARS", "RESV1", "RESV2"):
                continue
            if roff <= off < roff + rsize:
                print("   FAIL: slot %s (index %d = 0x%03X) lands inside "
                      "%s" % (sname, idx, off, rname))
                bad = True
        if off >= shared_size:
            print("   FAIL: slot %s (index %d) is past the shared page" %
                  (sname, idx))
            bad = True

    if bad:
        die("SHARED map has overlapping regions - fix zzdf_config.h")
    print("   OK: no overlap, all %d scalar slots clear of the rings"
          % len(slots))


def check_ddr_map():
    """Prove the DDR layout is coherent before anything is compiled.
    The heap split is dynamic (launcher-provided base), so what is
    checked here is the static frame it must fit in."""
    cfg = os.path.join(ROOT, "core1", "zzdf_config.h")
    txt = open(cfg).read()

    def val(name):
        m = re.search(r"^#define\s+%s\s+(0x[0-9A-Fa-f]+|\d+)" % name, txt, re.M)
        if not m:
            die("ddr map check: %s not found in zzdf_config.h" % name)
        return int(m.group(1), 0)

    blob      = val("ZZDF_BLOB_ARM")
    blob_lim  = val("ZZDF_BLOB_LIMIT_ARM")
    shared    = val("ZZDF_SHARED_ARM")
    staging   = val("ZZDF_STAGING_ARM")
    pcm       = val("ZZDF_PCM_ARM")
    pcm_size  = val("ZZDF_PCM_SIZE")
    hi_base   = val("ZZDF_ASSET_ARENA_BASE")   # == ZZDF_HIGH_DDR_BASE
    hi_end    = val("ZZDF_ASSET_ARENA_END")    # == ZZDF_HIGH_DDR_END
    guard_b   = val("ZZDF_GUARD_BASE")
    guard_e   = val("ZZDF_GUARD_END")
    heap_min  = 64 * 1024 * 1024
    heap_algn = 1024 * 1024
    U32 = 0xFFFFFFFF

    print("== DDR map check ==")
    print("   blob     0x%08X..0x%08X" % (blob, blob_lim - 1))
    print("   shared   0x%08X" % shared)
    print("   staging  0x%08X" % staging)
    print("   pcm ring 0x%08X..0x%08X  (%d KiB, NC)" %
          (pcm, pcm + pcm_size - 1, pcm_size >> 10))
    print("   HIGH DDR (ZZQuake super-zone, WB/WA, dynamic split):")
    print("     assets  0x%08X.. (exact staged size, 64-byte aligned)" % hi_base)
    print("     heap    align_up(asset_end, 1 MiB)..0x%08X" % (hi_end - 1))
    print("     total   %d MiB, heap must keep >= %d MiB" %
          ((hi_end - hi_base) >> 20, heap_min >> 20))
    print("   guard    0x%08X..0x%08X  NEVER touched" % (guard_b, guard_e - 1))

    bad = []
    if not (blob < blob_lim <= shared):      bad.append("blob must end at/below shared")
    if not (staging < blob):                 bad.append("staging must sit below the blob")
    # PCM ring: must not touch staging, and its size must be a power of
    # two that keeps every position inside 16 bits. Both are load-bearing:
    # the mask arithmetic assumes the first, and the single-halfword
    # atomicity of SH_PCM_WRITE_POS / SH_PCM_READ_POS assumes the second.
    if pcm + pcm_size > staging:             bad.append("PCM ring runs into staging")
    if pcm_size & (pcm_size - 1):            bad.append("PCM ring size is not a power of two")
    if pcm_size > 0x10000:                   bad.append("PCM ring > 64 KiB: ring positions "
                                                        "would not fit in 16 bits and the "
                                                        "position slots could be read torn")
    if pcm & 0xFFFFF:                        bad.append("PCM ring not 1 MiB aligned "
                                                        "(zzdf_mmu.c maps it as a section)")
    if not (hi_base < hi_end):               bad.append("high DDR base must be < end")
    if hi_end != guard_b:                    bad.append("high DDR must end exactly at the guard")
    if guard_e <= guard_b:                   bad.append("guard must be non-empty")
    if (hi_end - hi_base) < heap_min:        bad.append("super-zone smaller than the heap minimum")
    if hi_base & (heap_algn - 1):            bad.append("high DDR base not 1 MiB aligned")
    if hi_end & (heap_algn - 1):             bad.append("high DDR end not 1 MiB aligned")
    for name, v in (("blob", blob), ("shared", shared), ("hi_end", hi_end), ("guard_e", guard_e)):
        if v > U32:                          bad.append("%s overflows 32 bits" % name)
    # the smallest legal heap must still fit: assets could take
    # everything up to hi_end - heap_min
    if hi_base + heap_min > hi_end:          bad.append("heap minimum cannot fit")
    if bad:
        for b in bad: print("   FAIL: " + b)
        die("DDR map is inconsistent - fix zzdf_config.h")
    print("   OK")


# ---------------------------------------------------------------- #
# Build proof files are regenerated on every build.
# ---------------------------------------------------------------- #
def _keyed_lines_update(path, updates):
    """Rewrite 'digest  name' lines, replacing only the given names."""
    keep = []
    if os.path.isfile(path):
        for line in open(path).read().splitlines():
            parts = line.split()
            if len(parts) == 2 and parts[1] in updates:
                continue
            if line.strip():
                keep.append(line)
    for name in sorted(updates):
        keep.append("%s  %s" % (updates[name], name))
    keep.sort(key=lambda l: l.split()[-1])
    open(path, "w", newline="\n").write("\n".join(keep) + "\n")

def _write_atomic(path, text):
    """Write ASCII text through a temporary file, then replace atomically."""
    tmp = path + ".new"
    with open(tmp, "w", encoding="ascii", newline="\n") as f:
        f.write(text)
    os.replace(tmp, path)


def _sub_report(path, text, pat, repl, what, count=0):
    new, n = re.subn(pat, repl, text, count=count, flags=re.M)
    if n:
        print("   %-18s %s x%d" % (os.path.basename(path), what, n))
    return new


def refresh_doc_sums(data):
    """Push the blob we just built into the sums the prose quotes."""
    stem = variant_stem()
    md5 = hashlib.md5(data).hexdigest()
    sm = "%08X" % launcher_cksum(data)
    nb = len(data)
    prod = (stem == "zzdf")

    print("== README sums refresh (%s.bin) ==" % stem)

    # --- blobs/README.txt: one stanza per variant -------------------
    p = os.path.join(ROOT, "blobs", "README.txt")
    if os.path.isfile(p):
        t = open(p, encoding="ascii").read()
        # The stanza is found by the blob it belongs to, not by
        # position, so adding a third variant later cannot silently
        # rewrite the wrong one.
        t = _sub_report(p, t,
            r"(?s)(\n  " + re.escape(stem) + r"\.bin\b.*?"
            r"launcher sum )[0-9A-F]{8}(\n[ ]+md5 )[0-9a-f]{32}",
            lambda m: m.group(1) + sm + m.group(2) + md5,
            "%s.bin stanza" % stem, count=1)
        if prod:
            t = _sub_report(p, t,
                r"^    blob: \d+ bytes  sum=[0-9A-F]{8}$",
                "    blob: %d bytes  sum=%s" % (nb, sm),
                "first-line example")
        _write_atomic(p, t)

    # --- README.md: the one line that tells the user what to check --
    p = os.path.join(ROOT, "README.md")
    if prod and os.path.isfile(p):
        t = open(p, encoding="ascii").read()
        t = _sub_report(p, t, r"\(sum=[0-9A-F]{8}\)", "(sum=%s)" % sm,
                        "startup sum")
        _write_atomic(p, t)

    # --- the narrative build-proof files -----------------------------
    # PROOFS_*.txt are written from scratch every build, but these three
    # are hand-written prose that happens to quote the blob in a header.
    # Prose the build does not own is prose that goes stale.
    for nm in ("N3a_AUDIO.txt", "N3b_MIDI.txt", "N4_RESOLUTION.txt"):
        p = os.path.join(ROOT, "build-proof", nm)
        if not os.path.isfile(p):
            continue
        t = open(p, encoding="ascii").read()
        if prod:
            t = _sub_report(p, t,
                r"^blob zzdf\.bin  \d+ bytes  md5 [0-9a-f]{32}\n"
                r"[ ]+launcher sum [0-9A-F]{8}$",
                "blob zzdf.bin  %d bytes  md5 %s\n"
                "               launcher sum %s" % (nb, md5, sm),
                "header")
        else:
            t = _sub_report(p, t,
                r"^blob zzdf_legacy_park\.bin  \d+ bytes\n"
                r"[ ]+md5 [0-9a-f]{32}\n"
                r"[ ]+launcher sum [0-9A-F]{8}$",
                "blob zzdf_legacy_park.bin  %d bytes\n"
                "               md5 %s\n"
                "               launcher sum %s" % (nb, md5, sm),
                "legacy header")
        _write_atomic(p, t)


def refresh_build_proof(elf, blob, data, sources):
    proof = os.path.join(ROOT, "build-proof")
    blobs = os.path.join(ROOT, "blobs")
    os.makedirs(proof, exist_ok=True)
    os.makedirs(blobs, exist_ok=True)
    stem = variant_stem()
    name = stem + ".bin"

    print("== build-proof refresh (%s) ==" % name)

    # the blob itself, in both places that quote it
    shutil.copyfile(blob, os.path.join(proof, name))
    shutil.copyfile(blob, os.path.join(blobs, name))

    _keyed_lines_update(os.path.join(proof, "MD5.txt"),
                        {name: hashlib.md5(data).hexdigest()})
    _keyed_lines_update(os.path.join(proof, "SHA256.txt"),
                        {name: hashlib.sha256(data).hexdigest()})
    _keyed_lines_update(os.path.join(proof, "LAUNCHER_SUM.txt"),
                        {name: "%08X" % launcher_cksum(data)})

    # per-variant, so one never overwrites the other's evidence
    sz = run([SIZE, elf]).stdout
    syms = run([NM, "-C", elf]).stdout if os.path.isfile(elf) else ""
    census = ""
    cp = os.path.join(BUILD, "CENSUS_UNDEFINED.txt")
    if os.path.isfile(cp):
        census = open(cp).read()
        shutil.copyfile(cp, os.path.join(proof, "CENSUS_UNDEFINED.txt"))

    with open(os.path.join(proof, "PROOFS_%s.txt" % stem), "w",
              newline="\n") as f:
        f.write("%s - build evidence, written by build_next.py\n" % name)
        f.write("pin %s\n\n" % PIN)
        f.write("bytes        %d\n" % len(data))
        f.write("md5          %s\n" % hashlib.md5(data).hexdigest())
        f.write("sha256       %s\n" % hashlib.sha256(data).hexdigest())
        f.write("launcher sum %08X   <- the number the Amiga prints\n\n"
                % launcher_cksum(data))
        f.write(sz + "\n")
        f.write("units compiled : %d\n" % len(sources))
        f.write("census undefined symbols : %d%s\n" %
                (len([l for l in census.splitlines() if l.strip()]),
                 "   (CLOSED)" if not census.strip() else "   *** OPEN ***"))

    if syms:
        open(os.path.join(proof, "SYMBOLS_%s.txt" % stem), "w",
             newline="\n").write(syms)
    shutil.copyfile(os.path.join(BUILD, "SELECTED_SOURCES.txt"),
                    os.path.join(proof, "SELECTED_SOURCES.txt"))

    # anything left from the hand-maintained era that now contradicts
    # the generated files must go, or it will be believed again
    for stale in ("PROOFS.txt", "SYMBOLS.txt", "N2_5_HASHES.txt"):
        sp = os.path.join(proof, stale)
        if os.path.isfile(sp):
            os.remove(sp)
            print("   removed stale hand-written " + stale)
    print("   OK")


def main(argv):
    global VARIANT, VARIANT_DEFS, ZZOBJ
    if "--legacy-park" in argv:
        VARIANT = "legacy_park"
        VARIANT_DEFS = ["-DZZDF_LEGACY_WFE_PARK"]
        ZZOBJ = os.path.join(BUILD, "obj_" + VARIANT)
        print("== VARIANT %s (ARCHIVED): %s ==" % (VARIANT, " ".join(VARIANT_DEFS)))
    check_shared_map()
    check_ddr_map()
    up = locate_upstream(argv)
    print("== upstream: %s (pin %s) ==" % (up, PIN))
    if "--keep-engine" not in argv or not os.path.isdir(ENG):
        copy_engine(up)
        # platform hooks header must be visible from engine dir too
        patch_engine()
    os.makedirs(BUILD, exist_ok=True)

    sources = selected_sources()
    objs, failures = compile_all(sources)

    if failures:
        print("\n== COMPILE FAILURES: %d units ==" % len(failures))
        logp = os.path.join(BUILD, "COMPILE_ERRORS.txt")
        with open(logp, "w") as f:
            for rel, err in failures:
                f.write("===== %s =====\n%s\n" % (rel, err))
        # print first error line of each unit for the batch overview
        for rel, err in failures[:40]:
            first = ""
            for line in err.splitlines():
                if "error:" in line:
                    first = line.strip()
                    break
            print("  FAIL %-55s %s" % (rel, first[:110]))
        print("full log: " + logp)
        sys.exit(2)

    print("== all %d units compiled ==" % len(objs))
    write_object_manifest(objs)

    print("== census link (no --gc-sections) ==")
    elf, r = link(objs, census=True)
    und = undefined_from_link(r.stderr)
    with open(os.path.join(BUILD, "CENSUS_UNDEFINED.txt"), "w") as f:
        f.write("\n".join(und) + "\n")
    if und:
        print("== census OPEN: %d undefined symbols ==" % len(und))
        for u in und[:60]:
            print("  U " + u)
        if len(und) > 60:
            print("  ... (%d more, see CENSUS_UNDEFINED.txt)" %
                  (len(und) - 60))
        sys.exit(3)
    if r.returncode != 0:
        print("== census link failed (not undefined refs) ==")
        print(r.stderr[-4000:])
        sys.exit(3)
    print("== census CLOSED ==")

    if "--census-only" in argv:
        return

    print("== runnable link (--gc-sections, 2MB assert) ==")
    elf, r = link(objs, census=False)
    if r.returncode != 0:
        print(r.stderr[-4000:])
        if "blob overflow" in r.stderr:
            print("== 2MB exceeded: measuring real footprint ==")
            r3 = run([SIZE, os.path.join(BUILD, variant_stem() + "_census.elf")])
            print(r3.stdout)
        sys.exit(4)

    blob = os.path.join(BUILD, variant_stem() + ".bin")
    r = run([OBJCOPY, "-O", "binary", elf, blob])
    if r.returncode != 0:
        die("objcopy failed: " + r.stderr)
    if not os.path.isfile(blob) or os.path.getsize(blob) < 4096:
        die("blob missing or absurdly small - NOT a success")
    print(run([SIZE, elf]).stdout)
    data = open(blob, "rb").read()
    h = hashlib.md5(data).hexdigest()
    print("SUCCESS %s  %d bytes  md5 %s" % (blob, len(data), h))
    # The number the Amiga prints at startup. Computed here, by the
    # build, with the launcher's OWN algorithm - never by hand, and
    # never with a different one. Announcing a checksum computed a
    # different way once sent a whole test round chasing a blob
    # mismatch that did not exist.
    print("LAUNCHER SUM (what ZZDarkForces shows on screen): %08X" %
          launcher_cksum(data))

    refresh_build_proof(elf, blob, data, sources)
    refresh_doc_sums(data)

if __name__ == "__main__":
    main(sys.argv[1:])
