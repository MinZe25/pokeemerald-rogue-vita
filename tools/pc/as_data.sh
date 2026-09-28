#!/bin/bash
# usage: as_data.sh <as> <objcopy> <prefix:0|1> [as args...] -o out ...
AS="$1"; OBJCOPY="$2"; PREFIX="$3"; shift 3
OUT=""
prev=""
for a in "$@"; do
    if [ "$prev" = "-o" ]; then OUT="$a"; fi
    prev="$a"
done
$AS "$@" || exit 1
if [ "$PREFIX" = "1" ]; then
    $OBJCOPY --prefix-symbol=_ "$OUT" || exit 1
fi
