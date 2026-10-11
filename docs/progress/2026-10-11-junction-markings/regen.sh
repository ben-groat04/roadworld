#!/usr/bin/env bash
# Usage: regen.sh <export_mesh.exe> <out subfolder>   (renders the interchange fixture, views A-L)
set -e
HERE="$(cd "$(dirname "$0")" && pwd -W)"
EXPORT_MESH="$1"; NAME="$2"
TMP="${TMPDIR:-/tmp}/junction-markings-$NAME"
export PATH="/c/Program Files/nodejs:$PATH"
"$EXPORT_MESH" --out "$TMP" interchange > /dev/null
cd "$HERE/../../../prototypes/mesh-view"
node render.mjs --in "$TMP" --out "$HERE/$NAME" --views "$HERE/views.json"
node render.mjs --in "$TMP" --out "$HERE/$NAME" --views "$HERE/views_extra.json"
[ -f "$HERE/views_ref.json" ] && node render.mjs --in "$TMP" --out "$HERE/$NAME" --views "$HERE/views_ref.json"
true
