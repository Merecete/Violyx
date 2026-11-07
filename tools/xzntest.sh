#!/bin/sh
# Builds the host test for XZN: tools/xzntest.sh OUTPUT
cd "$(dirname "$0")/../sdk" || exit 1
gcc -g -O1 -fsanitize=address,undefined -w -DHOSTTEST -Dmain=xzn_main -Dmalloc=xm -Dfree=xf -Drealloc=xr -Dqsort=xq -I. -Ihelpers/inc -I../third_party/stb -I../third_party/minimp3 -I../third_party/dr_flac -c examples/xzn.c -o /tmp/xzn_t.o || exit 1
gcc -g -O1 -fsanitize=address,undefined -w ../tools/xzntest.c /tmp/xzn_t.o -o "$1" -lm
