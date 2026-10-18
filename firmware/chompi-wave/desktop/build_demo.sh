#!/bin/sh
# Builds the desktop audition harness against the vendored DaisySP.
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
SP="$HERE/../code/libs/DaisySP/Source"
g++ -O2 -std=c++14 -I"$HERE/../code/src" -I"$SP" -I"$SP/Control" -I"$SP/Utility" \
    "$HERE/render_demo.cpp" "$SP/Control/adsr.cpp" -o "$HERE/render_demo"
echo built "$HERE/render_demo"
