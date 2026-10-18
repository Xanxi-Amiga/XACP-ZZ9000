#!/bin/sh
# Backward-compatible offline entry point
set -e
ROOT="$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)"
cd "$ROOT"
sh build_all.sh
