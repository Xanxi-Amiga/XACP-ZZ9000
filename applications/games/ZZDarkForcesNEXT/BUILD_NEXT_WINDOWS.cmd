@echo off
rem BUILD_NEXT_WINDOWS.cmd - one-command build (Windows).
rem Requires: C:\ArmGNUToolchain\13.2 Rel1\bin  and Python 3.
rem   BUILD_NEXT_WINDOWS.cmd                -> build_next\zzdf.bin
rem                                            (production, cooperative return)
rem   BUILD_NEXT_WINDOWS.cmd --legacy-park  -> build_next\zzdf_legacy_park.bin
rem                                            (archived WFE park)
set "PATH=C:\ArmGNUToolchain\13.2 Rel1\bin;%PATH%"
python build_next.py --upstream upstream\src\TheForceEngine %*
