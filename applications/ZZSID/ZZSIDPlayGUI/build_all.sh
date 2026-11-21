#!/bin/sh
# Offline MSYS release build: ARM Core1 A2 + Amiga 68k GUI + release ZIP
set -eu
cd "$(dirname "$0")"
python build.py
python package_release.py
