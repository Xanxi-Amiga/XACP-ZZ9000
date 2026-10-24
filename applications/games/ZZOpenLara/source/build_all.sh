#!/bin/sh
# In this ARM-only source package, build all four ARM renderer variants.
set -e
cd "$(dirname "$0")"
sh build_arm.sh "$@"
