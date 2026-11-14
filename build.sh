#!/usr/bin/env sh
set -e
GXX=${GXX:-g++}
mkdir -p dist
"$GXX" -m32 -shared -O2 -static-libgcc -static-libstdc++ -o dist/re5_char_tweaks.asi src/dllmain.cpp
echo OK
