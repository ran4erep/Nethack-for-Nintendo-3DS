#!/usr/bin/env bash
cd /c/Users/ran4erep/Downloads/NetHack-NetHack-5.0
HOSTCC="C:/msys64/ucrt64/bin/gcc.exe"
COMMON="-O2 -Iutil/hostinc -Iinclude -Inhlua/lua/src -D__3DS__ -DARM11 -DCROSSCOMPILE -DCROSSCOMPILE_TARGET -DNINTENDO_3DS"
$HOSTCC $COMMON -c win/share/tile2bmp.c -o util/hostbuild/probe.o > util/hostinc/probe.out 2>&1
echo "rc=$?" >> util/hostinc/probe.out
