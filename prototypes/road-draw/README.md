# Road Draw prototype (throwaway UX test tool)

Open `index.html` in Chrome or Edge (double-click it, no server needed). Press H for controls.

Tests: clothoid click-point drawing, snapping (angle, length, road ends, onto roads, extension and parallel guidelines), heights (terrain at click, PgUp/PgDn), constant-grade segments with vertical curves, the profile panel (cut, fill, wall cap), dual-carriageway placement, handle editing, and undo/redo.

Geometry checks: `node prototypes/road-draw/test-geom.mjs`. The `<script id="geom">` block mirrors `sim_core`'s clothoid code.

Not production code: findings feed the real tool in Unreal.
