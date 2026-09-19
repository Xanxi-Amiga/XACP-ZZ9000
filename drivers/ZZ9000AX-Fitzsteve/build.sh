#!/bin/sh
set -e
cd "$(dirname "$0")/src"
m68k-amigaos-gcc zz9000ax-ahi.c asmfuncs.s -O3 \
  -o zz9000ax.audio \
  -Wall -Wextra -Wno-unused-parameter \
  -nostartfiles -m68020 -ldebug
