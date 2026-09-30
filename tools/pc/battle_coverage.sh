#!/bin/bash
# Battle coverage test for the PC/Vita port (runs on Linux / WSL, headless).
#
# Plays one short, deterministic wild battle per case (see ROGUE_COVERAGE in
# src/pc_harness.c): every move (x5 terrain/weather variants), every ability,
# every mega / primal / ultra burst. Logs every NULL access, crash, division by
# zero and hang (src/platform/nulltrap.c), plus AddressSanitizer errors with an
# ASAN=1 build, and writes one de-duplicated report.
#
# usage: tools/pc/battle_coverage.sh <save file> [passes] [jobs]
#   save file: a save standing in the overworld (the hub), e.g. a Vita save
#   passes:    comma list of moves,abilities,megas (default: all)
#   jobs:      parallel games (default: number of CPUs)
# env:
#   GAME=pokeemerald_rogue_asan  use the AddressSanitizer build (make ... ASAN=1)
#   OUT=dir                      output directory (default: coverage_out)
#   RANGE=first:last             only these case ids (single pass)
#
# Build first:  make PORTABLE=1 TARGET_OS=LINUX RELEASE=1 [ASAN=1]
# NULL trap:    sudo sysctl -w vm.mmap_min_addr=0   (resets on reboot)
# Reproduce one case (see the report):
#   ROGUE_HEADLESS=1 ROGUE_SAVEFILE=<save> ROGUE_INPUT="$LOAD_INPUT" \
#     ROGUE_COVERAGE=moves:123:123 ./pokeemerald_rogue
set -u
cd "$(dirname "$0")/../.."
SAVE=${1:?usage: $0 <save file> [passes] [jobs]}
PASSES=${2:-moves,abilities,megas}
JOBS=${3:-$(nproc)}
GAME=${GAME:-pokeemerald_rogue}
OUT=${OUT:-coverage_out}
# title screen -> continue -> skip the load messages
LOAD_INPUT='250:A:5;1100:START:5;1300:A:5;1600:A:5;1690:A:5;1780:A:5;1870:A:5;1960:A:5;2050:A:5'

[ -x "$GAME" ] || { echo "build $GAME first (make PORTABLE=1 TARGET_OS=LINUX RELEASE=1)"; exit 1; }
[ -f "$SAVE" ] || { echo "no save file $SAVE"; exit 1; }
if [ "$(cat /proc/sys/vm/mmap_min_addr)" != 0 ]; then
    echo "note: NULL trap off (run: sudo sysctl -w vm.mmap_min_addr=0); only crashes/hangs are caught"
fi
rm -rf "$OUT"; mkdir -p "$OUT"
cp "$GAME" "$OUT/game"      # the exact binary, for resolving addresses later
cp "$SAVE" "$OUT/save.sav"
GAME_ABS=$(realpath "$OUT/game"); OUT_ABS=$(realpath "$OUT")

info=$(cd "$OUT" && ROGUE_HEADLESS=1 ROGUE_COVERAGE=info ./game 2>/dev/null | grep COVERAGE_INFO)
[ -n "$info" ] || { echo "the game did not report its case counts"; exit 1; }
echo "$info"

export ASAN_OPTIONS="halt_on_error=0:handle_segv=0:allow_user_segv_handler=1:detect_leaks=0:symbolize=0"

# runs cases first..last of a pass; restarts after crashes / hangs
worker() {
    local pass=$1 first=$2 last=$3 id=$4 run=0 dir next
    while [ "$first" -le "$last" ]; do
        dir="$OUT_ABS/w_${pass}_${id}_$run"; mkdir -p "$dir"; cp "$OUT_ABS/save.sav" "$dir/test.sav"
        (cd "$dir" && ROGUE_HEADLESS=1 ROGUE_SAVEFILE="$dir/test.sav" ROGUE_NULLTRAP="$dir/trap.txt" \
            ROGUE_INPUT="$LOAD_INPUT" ROGUE_COVERAGE="$pass:$first:$last" ROGUE_MAXFRAMES=50000000 \
            timeout 7200 "$GAME_ABS" > "$dir/log.txt" 2>&1)
        echo "exit=$?" >> "$dir/log.txt"
        grep -q 'COVERAGE FINISHED' "$dir/log.txt" && break
        # continue after the case that failed
        next=$(grep -o "^CASE $pass:[0-9]*" "$dir/log.txt" | tail -1 | cut -d: -f2)
        [ -n "$next" ] || { echo "worker $pass $id: game failed before the first case, see $dir"; break; }
        first=$((next + 1)); run=$((run + 1))
    done
}

start=$(date +%s)
IFS=, read -ra plist <<< "$PASSES"
for pass in "${plist[@]}"; do
    total=$(echo "$info" | grep -o "$pass=[0-9]*" | cut -d= -f2)
    [ -n "$total" ] && [ "$total" -gt 0 ] || { echo "unknown pass $pass"; continue; }
    first=0; last=$((total - 1))
    [ -n "${RANGE:-}" ] && { first=${RANGE%%:*}; last=${RANGE##*:}; }
    count=$((last - first + 1)); per=$(( (count + JOBS - 1) / JOBS ))
    echo "== $pass: cases $first..$last on $JOBS jobs"
    for ((j = 0; j < JOBS; j++)); do
        a=$((first + j * per)); b=$((a + per - 1)); [ $b -gt $last ] && b=$last
        [ $a -le $last ] && worker "$pass" $a $b $j &
    done
    wait
done
echo "== ran for $(( $(date +%s) - start ))s"
python3 tools/pc/coverage_report.py "$OUT" | tee "$OUT/report.txt"
echo "report: $OUT/report.txt"
