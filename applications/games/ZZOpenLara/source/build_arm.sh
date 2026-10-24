#!/bin/sh
set -e
cd "$(dirname "$0")"
if command -v python3 >/dev/null 2>&1; then PY=python3; else PY=python; fi
"$PY" tools/build.py --arm-only "$@"
