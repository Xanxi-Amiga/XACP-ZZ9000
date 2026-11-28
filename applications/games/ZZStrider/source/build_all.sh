#!/bin/sh
# ZZStrider - complete OFFLINE build (Core1 blob + Amiga launcher + GUI).
# The only command needed:   sh build_all.sh
# Nothing is downloaded, PATH is not modified; the compilers are called by their explicit
# Windows paths, which are set at the top of tools/build.py.
cd "$(dirname "$0")" || exit 1
for PY in python3 python py; do
  if command -v "$PY" >/dev/null 2>&1; then
    exec "$PY" tools/build.py "$@"
  fi
done
echo "ERROR: no Python interpreter found (python3, python or py)."
exit 1
