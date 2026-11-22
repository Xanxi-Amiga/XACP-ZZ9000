# Run from the extracted ZZMiniGL_Beta1 source directory in MSYS (fully offline).
set -eu
if command -v python3 >/dev/null 2>&1 && python3 -c "import sys" >/dev/null 2>&1; then
    exec python3 tools/windows_build.py
fi
exec python tools/windows_build.py
