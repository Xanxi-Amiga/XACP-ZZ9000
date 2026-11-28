# Cyclone.s - generated 68000 core

`Cyclone.s` is the assembler output of the Cyclone generator (notaz/cyclone68000, commit
`3ac7cf1bdeecb60e2414980e8dc72ff092f69769`) built with `../cyclone_config_zzstrider.h`
(the ZZRastan 1.0 configuration, only the include guard renamed).

    SHA-256  3984169530a6c6e4b8cf373de66ed686644b34cf375331be37aef52fe06c1f4e   (69917 lines)

It is shipped generated so that the build needs no host compiler; `tools/check_sources.py`
refuses to build if the file differs. It is byte-identical to the file generated with the
ZZRastan 1.0 configuration.

To regenerate (optional, any PC with a native C++ compiler), from `third_party/cyclone/`:

    g++ -O1 -DCONFIG_FILE=\"cyclone_config_zzstrider.h\" -o cyclone_gen Main.cpp Ea.cpp OpAny.cpp \
        OpArith.cpp OpBranch.cpp OpLogic.cpp OpMove.cpp Disa/Disa.c
    ./cyclone_gen                               (writes Cyclone.s in the current directory)

This procedure was run on Linux x86-64 (g++ 13) and reproduces the SHA-256 above.

The file contains one non-ASCII character in a comment of the upstream generator; it is kept
as generated.
