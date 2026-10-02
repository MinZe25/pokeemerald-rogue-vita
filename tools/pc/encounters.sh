#!/bin/bash
# Wild encounter searcher: which route can have which Pokémon, for the dex and
# settings of a save, at every gym. Runs the PC build headless (see ROGUE_SCOUT
# in src/pc_harness.c) and writes one HTML page to search in.
#
# usage: tools/pc/encounters.sh <save file> [out.html] [jobs]
#   save file: a save standing in the hub or on the path screen; its dex and
#              difficulty settings are the ones used
# Build first:  make PORTABLE=1 TARGET_OS=LINUX RELEASE=1
set -u
cd "$(dirname "$0")/../.."
SAVE=${1:?usage: $0 <save file> [out.html] [jobs]}
OUTHTML=${2:-encounters.html}
JOBS=${3:-$(nproc)}
GAME=${GAME:-pokeemerald_rogue}
# title screen -> continue -> dismiss the load messages with B (A could confirm a prompt)
LOAD_INPUT='250:A:5;1100:START:5;1300:A:5;1600:B:5;1700:B:5;1800:B:5;1900:B:5;2000:B:5;2100:B:5'
WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT

[ -x "$GAME" ] || { echo "build $GAME first (make PORTABLE=1 TARGET_OS=LINUX RELEASE=1)"; exit 1; }
[ -f "$SAVE" ] || { echo "no save file $SAVE"; exit 1; }
GAME_ABS=$(realpath "$GAME")
SAVE_ABS=$(realpath "$SAVE")
CONFIG=$(dirname "$SAVE_ABS")/config.txt

# one game run: scout <dir> <spec> <difficulty>
scout() {
    local dir=$1
    mkdir -p "$dir"
    cp "$SAVE_ABS" "$dir/pokeemerald.sav"
    # the port menu's "Save anywhere" must match the save, or loading a save
    # made during a run retires the run
    [ -f "$CONFIG" ] && cp "$CONFIG" "$dir/config.txt"
    (cd "$dir" && ROGUE_HEADLESS=1 ROGUE_SAVEFILE="$dir/pokeemerald.sav" ROGUE_INPUT="$LOAD_INPUT" \
        ROGUE_SCOUT="$2" ROGUE_SCOUT_DIFF="$3" ROGUE_MAXFRAMES=40000 \
        timeout 600 "$GAME_ABS" > "$dir/log.txt" 2>&1)
    echo "exit=$?" >> "$dir/log.txt"
}
export -f scout
export SAVE_ABS CONFIG GAME_ABS LOAD_INPUT

echo "== wild pools at every gym"
for d in $(seq 0 13); do echo "$WORK/pool_$d pools $d"; done | xargs -P "$JOBS" -n 3 bash -c 'scout "$@"' _
echo "== a look at every route map (difficulty 0)"
routes=$(grep -c '^POOL' "$WORK/pool_0/log.txt")
for r in $(seq 0 $((routes - 1))); do echo "$WORK/route_$r route:$r 0"; done | xargs -P "$JOBS" -n 3 bash -c 'scout "$@"' _

python3 tools/pc/encounters_page.py "$WORK" "$OUTHTML"
