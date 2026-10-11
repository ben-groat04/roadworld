#!/usr/bin/env bash
# Regenerates the structure/grading pictures. Usage: regen.sh [scenario ...]   (default: all)
# Needs: BUILD dir with export_mesh + libs (cmake build), g++ 16 for export_graded, node 24 + Edge (mesh-view).
set -e
ROOT="$(cd "$(dirname "$0")/../../.." && pwd -W)"
BUILD="${BUILD:-$ROOT/build-agent-g4}"
OUT="$ROOT/docs/progress/2026-10-10-structures-r5"
TMP="${TMPDIR:-/tmp}/structures-r5-export"
export PATH="/c/msys64/mingw64/bin:/c/Program Files/nodejs:$PATH"
mkdir -p "$TMP"
GRADED="$BUILD/export_graded.exe"
if [ ! -x "$GRADED" ] || [ "$ROOT/prototypes/mesh-view/tools/export_graded.cpp" -nt "$GRADED" ]; then
  g++ -std=c++20 -O2 -DCP_DATA_DIR="\"$ROOT/data\"" -I"$ROOT/sim_core/include" -I"$ROOT/sim_core/tools" \
    "$ROOT/prototypes/mesh-view/tools/export_graded.cpp" "$BUILD/sim_core/tools/libscene_tools.a" \
    "$BUILD/sim_core/libsim_core.a" "$BUILD/clipper2/libclipper2.a" -o "$GRADED"
fi
# name | terrain | terrain mesh step (m) | margin (m)
SCEN="
s1_bridge_over_road|flat|2|40
s2_bridge_short_gap|ditch|0.5|40
s3_tunnel_portal|hill|1|120
s4_retaining_wall_run|sidehill_wavy|0.5|60
s4b_side_hill_realistic|sidehill_gentle|0.5|60
s5_cutting|ridge|0.5|70
s6_embankment|flat|1|70
s7_adjacent_levels|terrace|0.5|60
"
want=" $* "
echo "$SCEN" | while IFS='|' read -r name terrain step margin; do
  [ -z "$name" ] && continue
  [ $# -gt 0 ] && [[ "$want" != *" $name "* ]] && continue
  rm -rf "$TMP/$name"
  "$BUILD/sim_core/tools/export_mesh.exe" --out "$TMP/$name" --layout "$OUT/layouts/$name.json" > /dev/null
  "$GRADED" --terrain "$terrain" --step "$step" --margin "$margin" --out "$TMP/$name" --layout "$OUT/layouts/$name.json"
  node "$ROOT/prototypes/mesh-view/render.mjs" --in "$TMP/$name" --out "$OUT" --views "$OUT/views/$name.json"
done
if [ $# -eq 0 ] || [[ "$want" == *" s8_interchange "* ]]; then
  rm -rf "$TMP/s8_interchange"
  "$BUILD/sim_core/tools/export_mesh.exe" --out "$TMP/s8_interchange" interchange > /dev/null
  "$GRADED" --terrain flat --step 2 --margin 30 --out "$TMP/s8_interchange" interchange
  node "$ROOT/prototypes/mesh-view/render.mjs" --in "$TMP/s8_interchange" --out "$OUT" --views "$OUT/views/s8_interchange.json"
fi
