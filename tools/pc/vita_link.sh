#!/bin/bash
# Links eboot.elf for the Vita, making sure there is room after the code
# segment for the SCE module info vita-elf-create appends there.
# usage: vita_link.sh <out.elf> <gcc> <link args...>
set -e
OUT="$1"; CC="$2"; shift 2
NEEDED=4000   # bytes of free space wanted at the end of segment 0 (SCE data is ~3 KB)

"$CC" "$@" -o "$OUT"

read_gap() {
    # end of the executable LOAD segment and its distance to the next page
    arm-vita-eabi-readelf -lW "$OUT" | python3 -c '
import sys
for line in sys.stdin:
    f = line.split()
    if f and f[0] == "LOAD" and "E" in f[6:-1]:
        end = int(f[2], 16) + int(f[5], 16)
        print((4096 - end % 4096) % 4096)
        break'
}

GAP=$(read_gap)
if [ "$GAP" -lt "$NEEDED" ]; then
    PAD=$((GAP + 64))
    PADOBJ="$(dirname "$OUT")/vita_pad.o"
    printf '\t.section .rodata.vita_pad,"a"\n\t.global vita_segment_pad\nvita_segment_pad:\n\t.space %d\n' "$PAD" \
        | arm-vita-eabi-as -o "$PADOBJ" -
    "$CC" "$@" "$PADOBJ" -Wl,--undefined=vita_segment_pad -o "$OUT"
    echo "vita_link: padded code segment by $PAD bytes (gap was $GAP, now $(read_gap))"
fi
