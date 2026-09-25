@echo off
rem RUN_PYTHON.cmd - run an arbitrary python tool with the ARM
rem toolchain on PATH (no PowerShell).
set "PATH=C:\ArmGNUToolchain\13.2 Rel1\bin;%PATH%"
python %*
