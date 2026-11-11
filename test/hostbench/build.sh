#!/bin/bash
# Build NovaMesa plus the benchmark for the host, or for m68k-linux to run
# under qemu-m68k. Nothing here is used by the Amiga build.
#
#   ./build.sh                                   native build -> out/bench
#   CC="m68k-linux-gnu-gcc -m68040" LDFLAGS=-static TAG=m68k ./build.sh
#   CFLAGS=-DDEBUG TAG=debug ./build.sh          also prints the triangle function chosen
#
# Run:  out/bench [width height frames [ppm-prefix]]
#       ONLY=hint,filter,mode out/bench ...      run a single configuration
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
SRC=$HERE/../../src
CC=${CC:-gcc}
TAG=${TAG:-host}
OBJ=$HERE/out/obj_$TAG
FLAGS="-O2 -fno-strict-aliasing -w -std=gnu99 $CFLAGS -include $HERE/stub/libraries/Picasso96.h -I$HERE/stub -I$SRC -I$SRC/amiga"
mkdir -p "$OBJ"
for f in $SRC/*.c $SRC/swrast/*.c $SRC/swrast_setup/*.c $SRC/tnl/*.c $SRC/math/*.c $SRC/array_cache/*.c $SRC/amiga/*.c; do
  b=$(basename "$f" .c)
  case $b in m_debug_*) continue;; esac
  $CC -c $FLAGS "$f" -o "$OBJ/$b.o" &
  while [ "$(jobs -r | wc -l)" -ge "$(nproc)" ]; do sleep 0.05; done
done
wait
OUT=$HERE/out/bench; [ "$TAG" = host ] || OUT=$HERE/out/bench_$TAG
$CC $FLAGS "$HERE/bench.c" "$OBJ"/*.o -lm $LDFLAGS -o "$OUT"
echo "built $OUT"
