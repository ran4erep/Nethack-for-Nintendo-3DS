#!/usr/bin/env bash
set -e
cd /c/Users/ran4erep/Downloads/NetHack-NetHack-5.0
HOSTCC=C:/msys64/ucrt64/bin/gcc.exe
COMMON="-O2 -Iutil/hostinc -Iinclude -Inhlua/lua/src -D__3DS__ -DARM11 -DCROSSCOMPILE -DCROSSCOMPILE_TARGET -DNINTENDO_3DS"
mkdir -p util/hostbuild
echo "building tile2bmp host tool"
for f in win/share/tile2bmp.c win/share/tiletext.c src/monst.c src/objects.c src/drawing.c src/decl.c src/alloc.c util/panic.c src/hacklib.c util/hostinc/stub_utils.c; do
  echo "CC $f"
  $HOSTCC $COMMON -c "$f" -o "util/hostbuild/$(basename $f .c).o"
done
echo "CC tilemap.c (TILETEXT)"
$HOSTCC $COMMON -DTILETEXT -c win/share/tilemap.c -o util/hostbuild/tiletxt.o
echo "LINK tile2bmp.exe"
$HOSTCC util/hostbuild/*.o -o util/tile2bmp.exe
echo OK
