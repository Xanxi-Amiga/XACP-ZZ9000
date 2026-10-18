#!/bin/sh
# Compile-only SDK example. No network operations. For executable linking use -mcrt=nix20.
set -eu
cd "$(dirname "$0")"
CC="C:/amiga-gcc/bin/m68k-amigaos-gcc"
"$CC" -O2 -m68020 -Wall -Wextra -I.. -I../include -c xacp_image_client.c -o xacp_image_client.o
printf 'Built sdk/amiga/xacp_image_client.o\n'
