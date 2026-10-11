#!/usr/bin/env bash
# Regenerates the before/after pictures. "before" needs an export from a build of the parent of the junction-fix
# commit (export_mesh interchange); "after" from the current build. Usage: regen.sh <export_mesh.exe> <name: before|after>
set -e
HERE="$(cd "$(dirname "$0")" && pwd -W)"
EXPORT_MESH="$1"; NAME="$2"
TMP="${TMPDIR:-/tmp}/junction-fixes-$NAME"
export PATH="/c/Program Files/nodejs:$PATH"
"$EXPORT_MESH" --out "$TMP" interchange > /dev/null
cd "$HERE/../../../prototypes/mesh-view"
node render.mjs --in "$TMP" --out "$HERE/$NAME" --views "$HERE/views.json"
node render.mjs --in "$TMP" --out "$HERE/$NAME" --views "$HERE/views_extra.json"
