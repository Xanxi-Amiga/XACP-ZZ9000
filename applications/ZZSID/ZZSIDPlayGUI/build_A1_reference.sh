#!/bin/sh
# Historical A1, only for regression; not the 1.0 public executable.
set -eu
cd "$(dirname "$0")"
python build.py --reference-a1
