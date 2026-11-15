#!/bin/sh
# Builds the host test for XZN: tools/xzntest.sh OUTPUT
cd "$(dirname "$0")/../sdk" || exit 1
T=$(mktemp -d) || exit 1
gcc -g -O1 -fsanitize=address,undefined -w -DHOSTTEST -Dmain=xzn_main -Dmalloc=xm -Dfree=xf -Drealloc=xr -Dqsort=xq -I. -Ihelpers/inc -I../third_party/stb -I../third_party/minimp3 -I../third_party/dr_flac -I../third_party/ogg/include -I../third_party/vorbis/include -c examples/xzn.c -o $T/xzn_t.o || exit 1
# the Vorbis encoder with the C library of the host
mkdir -p $T/inc/ogg && cp helpers/inc/ogg/config_types.h $T/inc/ogg/ || exit 1
for f in ../third_party/ogg/src/bitwise.c ../third_party/ogg/src/framing.c $(for v in analysis bitrate block codebook envelope floor0 floor1 info lpc lsp mapping0 mdct psy registry res0 sharedbook smallft vorbisenc window synthesis; do echo ../third_party/vorbis/lib/$v.c; done); do
    gcc -g -O1 -w -I$T/inc -I../third_party/ogg/include -I../third_party/vorbis/include -I../third_party/vorbis/lib -c "$f" -o "$T/vb_$(basename "$f" .c).o" || exit 1
done
gcc -g -O1 -fsanitize=address,undefined -w ../tools/xzntest.c $T/xzn_t.o $T/vb_*.o -o "$1" -lm
r=$?
rm -rf "$T"
exit $r
