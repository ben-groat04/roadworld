# Prototype UI parity spec (what the browser road tool shows and how)

Written 11 Oct 2026 from `prototypes/road-draw/src/*` (read in full: `shell.html`, `config.js`, `scene.js`, `input.js`, `ui.js`, `earthworks.js` (constants, classify, rebuild), `terrain.js`, `roadmesh.js` (colour parts), `inspect.js`, `geom.js`), `tools/scene.mjs`, and a live run of `prototypes/road-draw/index.html` in Edge via Playwright (`playwright-core` from the prototype's `node_modules`; 1400x900 viewport; real mouse and keyboard events, no source changes). Owner's brief: this UI "was sort of what i want, that was the point of it". The Unreal game must reach parity with it. This document supplements `docs/unreal-game-plan.md`, which covers the prototype's interactions but not what it shows.

Screenshots: `docs/progress/2026-10-11-prototype-ui/` (file names cited as `NN_name.png` below). Headless captures were taken at 1400x900, world 2048 m, terrain seed 1337. Facts from code are marked (code); facts seen running are marked (seen).

Contents: A elements, B inputs, C feedback while drawing, D tuning panel, E rough edges, F parity checklist and GAPs, G what the plan missed, H update after roadtool3 (current status of every GAP and the host-side list: read H first).

## 0. Layout of the screen (seen: `02_idle.png`, `04_midstroke.png`)

- One full-window 3D canvas (`#view`, CSS cursor `crosshair`) above an optional 2D profile canvas (`#prof`). Profile height = `min(220, round(0.28 * window height))` px when shown; 0 when hidden (P). The 3D view takes the rest.
- Overlays on the 3D canvas, all HTML, dark translucent panels: HUD top-left (12,12), inspect bar top-centre, tuning panel top-right. Help is a centred modal that is open at start (`01_idle_help.png`).
- Theme tokens (CSS): bg `#0d1117`, panel `#161b22`, text `#e6edf3`, muted `#8b949e`, accent `#ffd866`, error `#ff7b72`, warning `#f5a623`, border `#30363d`. Font `13px/1.45 system-ui, -apple-system, Segoe UI, sans-serif`.
- Initial view: target (1000, 900), distance 1300 m, azimuth 0.35 rad, polar 0.95 rad (about 54 degrees from vertical). Start mode DRAW, road type 1, colour plain, help open, profile shown.
- Render: ACES filmic tone mapping (exposure 0.92), one 2048 shadow map re-rendered only when geometry changes or the view moves out of the fitted area, hazy sky gradient (`#3f7fc4` top, `#8fbbe0` mid, `#b9d3e6` horizon), distance fog, water plane `#2f7fa6` at 88% opacity. Pixel ratio capped at 1.5 and at about 3.7 Mpx. These are world look, not UI; listed so the Unreal scene can match the legibility the UI was tuned against.

## A. Every on-screen element

Pixel sizes are CSS px at the 1400x900 capture; the prototype has no DPI scaling logic beyond the canvas pixel ratio.

| Id | Element | Where | What it shows | Exact text / format and units | Colours | Appears when | Updated by | Screenshot |
|---|---|---|---|---|---|---|---|---|
| E1 | HUD panel (`#hud`) | top-left (12,12), max width `min(560px, 100vw-24px)`, padding 8x10, radius 6, 1 px border, `pointer-events:none` | Mode, road type, flags, cursor readouts, errors, advisories (see A.1) | see A.1 | bg `rgba(13,17,23,0.82)`, border `#30363d`, text `#e6edf3`, error `#ff7b72`, warn `#f5a623` | always | `renderHud()` whenever `needsUI` (any hover, key, click) | `04_midstroke`, `13_error_too_tight`, `16_advisory_bridge` |
| E2 | Profile panel (`#prof`) | bottom, full width, 1 px top border `#30363d` | Longitudinal section of the road being drawn, selected or hovered | see A.2 | bg `#161b22` | shown by default; P toggles | `renderProfile()` on `needsUI` | `04_midstroke`, `41_edit_select_viaduct_profile`, `42_edit_select_tunnel_profile`, `25_profile_hidden_P` |
| E3 | Tuning panel (`#tune`) | top-right (12,12), width 230, radius 6 | Collapsible list of 8 sliders | `⚙ Tuning` button, then label + value per slider | bg `rgba(13,17,23,0.88)`, labels `#8b949e`, values `#e6edf3`, slider accent `#ffd866` | collapsed at start; click button toggles | slider `input` events | `23_tuning_panel_open` |
| E4 | Help overlay (`#help`) | screen centre, `min(640px, 100vw-32px)` wide, max 80vh, padding 18x22, radius 10, shadow | Controls table + a paragraph about grading and colours | `Road Draw 3D prototype` title, 13-row key table, paragraph (see A.4) | bg `#161b22`, keys column `#ffd866`, rows separated by `#30363d`, paragraph `#8b949e` | open at start; H or ? toggles; click anywhere on it closes | key / click | `01_idle_help` |
| E5 | Inspect bar | top-centre, translucent, max width `min(520px, 100vw-520px)` | Button `Load scene JSON…` and a status line | idle: `or drop a file here · J = connectors`; loaded: `<file>: N roads, N junctions, N bridge / N tunnel samples, N connectors (N disabled) · J connectors on\|off` plus a warning if the scene exceeds the world | text `#e6edf3`, status `#8b949e` (error `#ff7b72`), bg `rgba(13,17,23,0.82)` | always | `inspHud()` | `02_idle`, `43_inspect_scene_loaded_connectors` |
| E6 | Frame-stats panel | top-right, over the tuning panel, monospace 12 px, `pointer-events:none` | ms/frame, fps, GPU name, JS cost buckets (pick, snap, preview, shadow, clip, render), draw calls, triangles (k), pixel size, software-GPU warning | e.g. `1486.3 ms/frame (1 fps while active)`; `GPU: ...`; `JS 38.68 ms/frame: pick 0.08 snap 0.03 preview 1.25`; `calls 14  tris 824k`; `px 1400x680` | text `#99ffdd` (`#9fd`), bg `rgba(13,17,23,0.82)` | F toggles | every 500 ms while rendering | `24_perf_panel_F` |
| E7 | Cursor / snap ring | on the ground at the snapped point, lifted 0.5 m, flat (horizontal), `RingGeometry(0.72, 1)`, screen-constant size | Where the next point will go and which snap is active | none (colour is the only label) | `end` `#3fb950`, `road` `#3fb950`, `guide` `#7ee0ff`, `angle` white, `free` white (hex `0x3fb950 / 0x7ee0ff / 0xffffff`) | draw mode, cursor over ground | `buildOverlay()` on every hover | `05`, `06`, `07`, `08`, `09`, `10` |
| E8 | Placed-point dots | at each clicked point, 0.4 m above its z, round, 4 px screen radius | The stroke's control points | none | `#ffd866` | stroke has points | overlay rebuild | `04_midstroke` |
| E9 | Ghost ribbon(s) | flat ribbon 0.35 m above each centreline sample, full `total` width of the road type (carriageway plus shoulders, e.g. 14.3 m residential), two ribbons for a dual type unless one-way | The road that would be built | none | normal `#dde6f0`, error `#e5484d`, opacity 0.55, depth test off (shows through terrain and water), render order 15 | stroke has at least two points (or one point plus a valid cursor) and no min-span error | overlay rebuild | `04`, `13`, `18`, `28` |
| E10 | Guides | dashed polylines draped on the ground every 3 m, 0.5 m up | Extension line, parallel-offset curve, angle/length line | none | extension and offset: `#7ee0ff` opaque; angle/length: white at 0.5 opacity; dash 4 m, gap 3 m (world units) | see C.2 | overlay rebuild | `07`, `08`, `08b`, `10` |
| E11 | Edit hover / selection line | thin line along the road centreline, 0.8 m above z | Which road is hovered / selected in edit mode | none | hover `#ffe9a8`, selected `#ffd866` | edit mode | overlay rebuild | `31_edit_hover_road`, `32_edit_selected_handles` |
| E12 | Edit handles | round dots at each control point of the selected road, 0.5 m above z | Draggable control points | none | normal white, 7 px; hovered `#ffd866`, 10 px | edit mode with a selection | overlay rebuild | `32`, `33_edit_handle_hover`, `34_edit_handle_dragging` |
| E13 | Mouse pointer | OS cursor over the canvas | n/a | CSS `crosshair` | n/a | always over 3D canvas | n/a | all |
| E14 | Heat-map road surface | carriageway vertex colours of committed roads (not the ghost) | Curvature or gradient risk | HUD shows `colour: plain\|curvature\|gradient` | `#3fb950` ok, `#e8d44d` caution, `#f5a623` amber, `#e5484d` red; thresholds in D.3 | C cycles; default plain | road mesh rebuild on mode change | `29_colour_curvature`, `30_colour_gradient` |
| E15 | Junction surfaces and connector paths (inspect mode only) | loaded from a sim_core scene JSON | Junction carriageway, pavements, islands, gore hatching; lane connector lines coloured by turn | none | turn colours: left `#4aa3ff`, right `#ff9f43`, straight `#3fb950`, u-turn `#c678dd`, merge `#e5484d`, diverge `#39d0d0`, other `#ffd866`, disabled grey dashed `#8a8f98` | after loading a scene file; J toggles connectors | load / J | `43_inspect_scene_loaded_connectors`, `44_inspect_connectors_off` |

The prototype has no toolbar, no road-type picker, no minimap, no scale bar, no compass and no status bar. Road type and mode are shown only as text in the HUD header. A screen-space cursor tooltip does not exist.

### A.1 HUD content, line by line (code `renderHud`; seen)

Lines are separated by `<br>`; later lines are omitted when their data does not exist.

1. Header (always): `**DRAW**` or `**EDIT**` (bold) + ` · ` + road type name + optional ` · one-way` (single-carriageway types with O) or ` · **single carriageway**` (dual types with O) + ` · colour: plain|curvature|gradient` + optional ` · snap off` (amber, while Shift is held). Example: `DRAW · A road / dual c'way · single carriageway · colour: plain` (`20_type3_single_carriageway_O`). Type names: `Residential street`, `B road / main road`, `A road / dual c'way`, `Motorway`.
2. Draw mode with a cursor on the ground: `cursor z 87.3 m · snap: angle`. z has one decimal, metres, and includes the height offset; when the offset is non-zero it adds ` (+3 m)` or ` (-22 m)` (`16_advisory_bridge`: `cursor z 58.4 m (+25 m) · snap: angle`). Snap kind words: `free`, `angle`, `guide`, `end`, `road`. Before any point exists this is the only readout (`03_hover_free`, `05_snap_end`).
3. When a preview road exists (at least two points and a fit): `span 88.0 m · bearing 44° · R at cursor ∞ m · min R 161 m`. span = length of the hover segment (1 dp); bearing = compass degrees at the cursor, 0 = north, clockwise, integer (code: `(90 - heading) mod 360`); R values are integer metres or `∞`.
4. Next line: `grade 0.6% · max 9.5% · G2 ✓ · total 224 m`. grade = mean grade of the hover segment (signed, 1 dp); max = steepest sample (unsigned); `G2 ✓` or `G1` (G1 means the smooth-curvature solve fell back); total = whole stroke length including the hover segment, integer metres.
5. Errors, red, one per line: `✕ <text>`. Texts (code): `segment shorter than 4 m` (value is the live Min segment setting); `too tight: radius 6.4 m < half-width 7.2 m (edge folds)`; `cannot fit curve`; (refused finish) `crossing too close vertically (2.6 m): raise/lower to junction or clear 5.3 m`; `cannot fit curve through crossing`.
6. Advisories, amber, one per line: `⚠ <text>`. Texts: `radius 31 m below comfortable 180 m for B road / main road`; `grade 38.2% above advisory 10%`; `fill over 12 m wall cap: becomes a bridge`; `cut over 12 m wall cap: becomes a tunnel`; `G2 solve fell back to G1 (curvature kink)`.
7. Edit mode with a selection: `selected road #1 · 200 m · drag handles, PgUp/PgDn height, Del delete`. The header in edit mode still shows the draw tool's road type, not the selected road's (rough edge E.9).

The HUD never mentions finishing a road, the available keys, or the snap's target road.

### A.2 Profile panel (code `renderProfile`; seen `04`, `16`, `17`, `41`, `42`)

Source road: the draw preview road if drawing, else the selected road, else the hovered road; otherwise it prints `Profile: draw a road, or select one in edit mode (Tab). P hides this panel.` (grey `#8b949e`, 12 px, at (16,22)). It is also empty while a min-span error blocks the preview (`12_error_min_span`).

- Margins: left 52, right 16, top 22, bottom 24. Horizontal axis = arc length 0..L (the whole stroke), plain numbers with no unit at the bottom, step = first of 10, 20, 50, 100, 200, 500, 1000 giving fewer than 10 labels.
- Vertical axis = metres, labels `N m` at x=8; range = min/max over road z and original-ground z, padded by `max(5 m, 15% of the range)`; grid step = first of 1, 2, 5, 10, 20, 50 giving fewer than 8 lines; grid lines `rgba(255,255,255,0.07)`.
- Layers, back to front: (1) solid under-ground fill from the ground line down to the axis, `#3a3f2e`; (2) per-segment cut/fill band between ground and road, coloured by the mean difference dz = road - ground: `|dz| > wall cap` red `rgba(229,72,77,0.55)` (this is the bridge/tunnel zone), dz > 0 fill blue `rgba(126,224,255,0.35)`, otherwise cut orange `rgba(245,166,35,0.4)`; (3) original ground line `#9aa57a`, 1.5 px; (4) road surface line white, 2.5 px; (5) vertical-curve bands, full height, `rgba(255,216,102,0.18)`, width = curve length centred on the control point; (6) control-point dots `#ffd866`, radius 3.5; (7) grade labels between consecutive control points, centred on the mid-station, 8 px above the road line, format `+9.5%` (sign always, 1 dp), colour `#f5a623` when `|grade| > road type max grade` else `#e6edf3`.
- Title (top-left, 12 px grey, y=15): `Profile · 224 m · vs original ground · blue = fill, orange = cut, red = over 12 m wall cap (bridge/tunnel) · yellow = vertical curves` (the 12 follows the live Wall cap).
- Measured against the ORIGINAL ground (`terrainH`), never the graded ground. No cursor marker, no station readout on hover, no scale control, no bridge/tunnel text labels, no curvature trace, no cross-section.
- Not interactive.

### A.3 Tuning panel

See section D.

### A.4 Help overlay text (code `shell.html`; seen `01_idle_help`)

Rows (key / meaning): Left click: add a point (the clothoid refits through all points); clicking onto an existing road connects and finishes; click onto a bridge deck to start from it. Double-click / Enter / right-click (no drag): finish road. Esc / Backspace: cancel road / remove last point. Hold Shift: snapping off (angle, length, guidelines, roads). PgUp / PgDn or ] / [: raise / lower the next point 1 m (Shift: 5 m); in edit mode the hovered handle. 1 2 3 4: Residential, B road, Dual carriageway, Motorway (dual types: one stroke places two one-way carriageways). O: toggle one-way (single-carriageway types). C: road colour plain / curvature / gradient. Tab: toggle draw / edit; edit: click a road, drag its handles (terrain regrades on release), Del deletes it. Ctrl+Z / Ctrl+Y: undo / redo. Right drag / middle drag / wheel: orbit / pan / zoom. W A S D or arrows, Q E: pan along the ground, rotate. P: toggle the profile panel. H: toggle this help. Closing paragraph: the terrain is graded to follow each road: cuttings and embankments at the batter slope, retaining walls where cut/fill exceeds "wall start", bridges (fill) and tunnels (cut) beyond "wall cap". Curvature and gradient are advisory (amber). Red means the road would break graphically and the click is refused. Profile: blue fill, orange cut, red = over the wall cap, measured against the original ground. Click to close.

Undocumented keys (code): F frame stats, J connector paths (inspect scene). Not in the help table.

## B. Every input

State names: DRAW / EDIT (Tab), "stroke" = a draw in progress (`state.draw` with at least one point). A click means pointer down and up with under 4 px movement; the prototype ignores any press that moved more than 4 px (code `down.moved`).

### B.1 Mouse

| Id | Input | DRAW, no stroke | DRAW, stroke active | EDIT |
|---|---|---|---|---|
| I1 | Left click | start a road at the snapped point (continues tangentially if snapped to a road end; takes bridge-deck height if clicked on a deck) | add a point; if the snap is `end` or `road` the click also finishes the road (auto-finish); blocked while a red error is showing (nothing happens, no message) | select the hovered road (or clear selection if none) |
| I2 | Left drag (more than 4 px) | nothing (OrbitControls left button disabled) | nothing | on a handle: drag it (moves in the horizontal plane at the handle's height; height follows terrain offset `dzT`); elsewhere nothing. Regrade and commit on release; one undo step; refused edits show red and revert |
| I3 | Right click (no drag) | nothing | finish if 2 or more points, else cancel stroke | nothing |
| I4 | Right drag | orbit | orbit | orbit |
| I5 | Middle drag | pan | pan | pan |
| I6 | Wheel | zoom | zoom | zoom |
| I7 | Double-click | nothing | finish (see E.2: adds a stray extra point first) | nothing |
| I8 | Pointer enters / leaves canvas | enter: hover resumes; leave: hover freezes (last ghost stays) | same | same |

Hover: pointer move over the canvas re-runs pick, snap and preview (code `processMouse`), also after any camera change.

### B.2 Camera (code `scene.js`, three.js `OrbitControls` plus custom keys)

- Perspective, vertical FOV 50 degrees; near = `max(0.5, dist/250)`, far = `max(8000, dist*6)`.
- Orbit about a target kept on the terrain surface; right-drag; rotation speed = three.js default (one full window height of drag = one full turn); damping off; polar angle clamped to [0.05, 1.42] rad (2.9 to 81.4 degrees from straight down); azimuth unlimited.
- Pan: middle-drag, `screenSpacePanning = false` (moves on the ground plane, not along the view plane); target clamped to the world square 0..2048 m; target height follows terrain every frame, camera moves with it.
- Zoom: wheel, zoomSpeed 1.1 (three.js dolly, roughly 5% of the distance per wheel notch; exact factor not measured), toward the target (not toward the cursor); distance clamped to [15, 3800] m.
- Camera never goes below ground: if its height is under terrain + 3 m it is lifted to that.
- Keyboard pan: W/Up forward, S/Down back, D/Right right, A/Left left, relative to the camera heading on the ground; speed `0.9 * distance` metres per second (so screen speed is constant), x3 with Shift held; Q rotates -1.3 rad/s, E +1.3 rad/s about the target; frame time capped at 0.1 s. Keys clear on window blur.
- Fog scales with distance (near = `max(300, 1.5*dist + 2*max(camY,20))`, far = `4.5*near + 3000`); shadow frustum refits when the target moves 20% of its half-size or zoom changes a lot.
- No smoothing, no focus-on-selection, no home key, no bookmarks.
- Scripted `freeCam` (test hook only) disables terrain following; not a user feature.

### B.3 Keyboard

Keys are ignored while a text input has focus (the sliders). Ctrl/Meta/Alt combinations other than undo/redo are ignored. Letters are case-insensitive.

| Id | Key | Effect |
|---|---|---|
| I9 | W A S D, arrow keys | pan (B.2); arrows also call `preventDefault` |
| I10 | Q / E | rotate left / right |
| I11 | Shift (held) | snapping off (`snap: free`, HUD amber `snap off`); also x3 keyboard pan speed; also 5 m steps for PgUp/PgDn; releasing re-snaps |
| I12 | 1 2 3 4 | set draw road type (works in either mode; in DRAW it rebuilds the ghost immediately) |
| I13 | O | toggle one-way: single types become one-way; dual types draw one carriageway instead of two |
| I14 | C | cycle road colour plain, curvature, gradient |
| I15 | P | toggle profile panel (the canvas resizes) |
| I16 | F | toggle frame stats (undocumented) |
| I17 | J | toggle connector paths of a loaded scene (undocumented) |
| I18 | H or ? | toggle help |
| I19 | Tab | toggle DRAW / EDIT; clears the stroke, snap and handle hover |
| I20 | Esc | DRAW with stroke: cancel it; otherwise clear the selection |
| I21 | Enter | DRAW with stroke: finish (at the last clicked point, see E.1) |
| I22 | Backspace | remove the last placed point (stroke ends if none left) |
| I23 | Delete | EDIT with selection: delete the road (one undo step) |
| I24 | PgUp or ] | height offset +1 m (Shift: +5 m): DRAW = next point only (reset to 0 by every click and by finish); EDIT = the hovered handle, applied immediately as an undoable edit |
| I25 | PgDn or [ | height offset -1 m (Shift: -5 m); same scope |
| I26 | Ctrl+Z / Cmd+Z | undo |
| I27 | Ctrl+Shift+Z, Ctrl+Y | redo |

### B.4 Other

| Id | Input | Effect |
|---|---|---|
| I28 | Tuning button | open / close the panel |
| I29 | Any tuning slider | live change (D) |
| I30 | Help overlay click | close it |
| I31 | Load scene JSON… button, file drop, `?scene=` URL | inspect mode load (E5) |

Undo/redo covers only road add, delete and handle edit (JS commands `AddRoads`, `DeleteRoad`, `EditPoint`, `EditPts`). Changing type, one-way, colour or tuning values is not undoable.

## C. Feedback while drawing

### C.1 Ghost (code `buildOverlay`, `ghostMesh`; seen)

- Geometry: the fitted clothoid chain through all placed points plus the cursor (the whole stroke refits on every move), sampled about every 2 m, as a flat ribbon at sample z + 0.35 m.
- Width: the road type's full `total` width (shoulders included): residential 14.3, B road 10.8, A road dual 10.8 per carriageway, motorway 17.45 per carriageway; the dual ghost is two ribbons centred at `±(gap/2 + total/2)` (gap 2.5 m / 3.0 m) with the median left empty (`18_type3_dual_ghost`). One-way on a dual type draws a single ribbon (`20`).
- Colour and opacity: normal `#dde6f0` at 55%; any error turns it `#e5484d` at the same opacity (`13_error_too_tight`, `28_error_crossing_too_close`). Advisories (amber) do NOT change the ghost colour. Depth test is off and render order high, so it shows through hills and water (`16_advisory_bridge`).
- Structures: the ghost is the same flat ribbon over a bridge or tunnel zone; no deck, no piers, no portal, no earthworks preview. Earthworks, walls, bridges and tunnels appear only after the road is committed, with the terrain regraded (`37`, `38`, `40`).
- Before the first click there is no ghost; just the ring.
- A min-span error removes the ghost entirely (only ring, dot and the red HUD line: `12_error_min_span`).
- The ghost is not smoothed or animated, and is not colour-coded for curvature or grade.

### C.2 Snap kinds, guides and markers (code `computeSnap`; seen)

Radii are screen pixels converted to metres at the cursor (`wpp` = ground metres per pixel); roads are ignored when more than 15 m above or below the cursor height. Order of priority, first match wins, only when Shift is not held:

| Snap | Test | Result position | Marker (E7) | Guide | HUD word | Screenshot |
|---|---|---|---|---|---|---|
| end | a free road end within 14 px (not the last placed point) | the end, at its height, continuing tangentially; commit auto-finishes | green ring 11 px | none | `end` | `05_snap_end` |
| road | centreline of any road within 10 px | exact centreline point and height; commit auto-finishes | green ring 11 px | none | `road` | `06_snap_road` |
| guide (extension) | point lies on the straight extension beyond a road end (at least 1 m out) within 9 px | the point on the extension | cyan ring 11 px | cyan dashed straight line from the road end to the point | `guide` | `07_snap_extension_guide` |
| guide (parallel offset) | a road within 80 m and the lateral distance equals k x base spacing (k = 1, 2, 3) within 7 px; base = half the existing road's total width + the new road's width (dual types: its two carriageways and median; single carriageway of a dual type: median-aware) | the point at that lateral offset | cyan ring 11 px | cyan dashed copy of the existing road's centreline at that offset (a curve) | `guide` | `08_snap_parallel_offset_guide`, `08b_snap_offset_guide_3x` |
| angle | stroke has at least one point and nothing above matched | angle quantised to the snap angle (15 degrees) relative to the previous segment (or the pinned start heading), length rounded to a multiple of 8 m, minimum 8 m | white ring 11 px | faint white dashed line from the last point to the snapped point | `angle` | `10_snap_angle_length`, `04_midstroke` |
| free | first point on the ground, or Shift held | the raw ground point | white ring 7 px | none | `free` | `03_hover_free`, `09_hover_free_second`, `11_shift_snap_off` |

Notes: guide and angle positions are suppressed when the cursor is on a bridge deck (the deck point itself is used). There is no on-screen label naming the road being snapped to, no distance/angle tooltip beside the cursor, and no highlight of the target road end or road. The ring is the only per-point indication.

### C.3 Errors (red; the click is refused; ghost turns red)

| Error | Trigger | Where it shows | Screenshot |
|---|---|---|---|
| `segment shorter than 4 m` | any segment between placed points or the cursor under Min segment | HUD only; ghost is removed; profile empties | `12_error_min_span` |
| `too tight: radius R m < half-width W m (edge folds)` | tightest radius under half the road's total width (dual not one-way: gap/2 + total) | HUD + red ghost | `13_error_too_tight` |
| `cannot fit curve` | the fit fails | HUD, no ghost | not reached |
| `crossing too close vertically (X m): raise/lower to junction or clear 5.3 m` | at finish: plan crossing with height difference 1.0 to 5.3 m | red ghost (HUD update throws, see E.1); error vanishes on the next mouse move | `28_error_crossing_too_close` |
| `cannot fit curve through crossing` | the junction-pinning refit fails | same | not reached |

Clicking while a red error is current does nothing visible (a "flash" timestamp is set; the only effect is a forced redraw). There is no sound, shake or message.

### C.4 Advisories (amber; the click is allowed; the ghost keeps its normal colour)

| Advisory | Trigger | Screenshot |
|---|---|---|
| `radius R m below comfortable M m for <type>` | tightest radius under the type's min radius (residential 20, B 180, A dual 720, motorway 1020 m) | `14_advisory_radius` |
| `grade G% above advisory N%` | steepest sample over the type's max grade (10, 8, 6, 4 %) | `15_advisory_grade` |
| `fill over 12 m wall cap: becomes a bridge` | any sample with fill beyond Wall cap (centre or either wall line, smoothed) | `16_advisory_bridge` |
| `cut over 12 m wall cap: becomes a tunnel` | same for cut | `17_advisory_tunnel` |
| `G2 solve fell back to G1 (curvature kink)` | smooth solve failed | not reached |

Advisories appear in the order radius, grade, bridge, tunnel, G1. Several can show at once (`16`, `17`). Their only other cue is the profile panel (red zone for bridge/tunnel, amber grade labels).

### C.5 Height offset

PgUp/PgDn change only a tool value (`state.dz`) for the next point. Where it shows: HUD `cursor z` line gets ` (+N m)` / ` (-N m)`; the ghost and the profile reflect it at once; the ring z follows (`16`, `17`). It is not shown as a separate gauge, vertical line or label in the 3D view, and resets to 0 after every click and every finish.

### C.6 Road type, one-way, dual

Type is shown only in the HUD header; there is no picker. Types 3 and 4 draw two carriageways from one stroke (the prototype creates two one-way roads offset by `gap/2 + total/2`, with the driving side deciding which direction goes where; on commit it is two undoable roads in one step). O on a dual type draws a single carriageway and the HUD says `single carriageway`. Shown in `18`, `19`, `20`, `21`, `22`.

### C.7 Earthworks, walls, bridge, tunnel: when and how they appear

- During drawing: not at all (only the flat ghost and the amber text, plus the profile's cut/fill bands).
- On commit (or undo/redo/handle-edit release): the road mesh, markings, kerbs and pavements appear; the terrain is regraded in tiles (a 0.5 m heightfield under all roads; cuttings and embankments at 1:batter with a 1.5 m verge, rolled-over daylight, toe ditch; retaining walls where a slope would exceed Wall start; benching above 3 m single walls) and walls / bridges / portals are meshed. Seen: `27_finished_road_idle` (earthwork scar beside a plain road), `40_finished_cut_fill_walls`, `37_finished_bridge_viaduct`, `38_finished_tunnel_portal`, `39_finished_tunnel_portal_side`.
- Cut/fill colouring in 3D: none. Cuttings and embankments take ground colour from the terrain shader (browner on steep slopes). Only the profile panel colours cut/fill.
- Bridge: a deck with parapets, piers every few tens of metres and abutments, over water or valley; tunnel: dark bore with portal headwall, wing walls, lining, lamps (`38`).
- During a handle drag: the dragged road redraws live but the terrain is NOT regraded until release (the old earthwork scar stays under the moving road: `34_edit_handle_dragging`).

### C.8 Colour modes (E14)

Plain, curvature, gradient on the committed carriageway. Thresholds (code `heatCurv`, `heatGrade`): curvature, with R the local radius and minR the type's comfortable minimum: R below 0.5 minR red, below minR amber, below 2 minR yellow, else green. Gradient, with g the local grade % and maxGrade the type's limit: above 1.5x red, above 1x amber, above 0.5x yellow, else green. Colours: red `#e5484d`, amber `#f5a623`, yellow `#e8d44d`, green `#3fb950`. The ghost is never heat-coloured. Seen `29`, `30`.

### C.9 Edit-mode feedback

Hover a road: thin light-yellow line `#ffe9a8`; select: yellow `#ffd866` line plus white handles at every control point (ends and interior) 7 px, hovered handle gold and 10 px; the profile panel shows the hovered road if nothing is selected, else the selected one; HUD gives `selected road #N · L m · drag handles, PgUp/PgDn height, Del delete`. A refused edit (crossing, cannot fit) sets a red `✕` error and reverts silently. Seen `31`–`36`, `41`, `42`.

### C.10 Inspect mode (sim_core scene viewer)

Loads `{roads, junctions}` JSON exported by sim_core, places it on the flattest dry area, draws roads with the prototype's own meshes, junction surfaces (asphalt, pavement `#bdbcb7`, kerb, island grass `#4f7a32` or paving, hatched gore `#e8e6df` stripes on `#3c3f44`) and lane connector lines (turn colours in E15). Not part of the drawing tool; the Unreal equivalent is a debug overlay.

## D. Tuning panel

All eight sliders (code `TUNE`, defaults from `T` in `config.js`). Value text is the raw number, no units, no reset button, no persistence. Whole-network values re-run grading about 120 ms after the last change (150 ms for water).

| Slider label | Key | Default | Range (step) | Affects | Recommendation for the game |
|---|---|---|---|---|---|
| Snap angle ° | `snapAngle` | 15 | 5 to 45 (5) | angle snap step | Tool setting (`cp::DrawParams::snap_angle_deg`), exposed in a settings menu |
| Snap length m | `snapLen` | 8 | 1 to 50 (1) | length snap step and minimum snapped length | Tool setting (`DrawParams::snap_length`) |
| Wall cap m (bridge/tunnel) | `wallCap` | 12 | 3 to 30 (1) | cut/fill depth beyond which a road becomes a bridge/tunnel; drives advisories, profile red zone, structure classification | Region data (`GradingParams::wall_cap`, region pack `wall_cap`: 12); debug override only |
| Wall start m | `wallStart` | 12 (range top is 12) | 2 to 12 (1) | slope height above which a retaining wall replaces the slope | Region data (region pack grading section); debug only. Note: the prototype default sits at the end of its range; the C++ `EarthworkParams::wall_start` default is 6, so check which value the region pack carries before wiring |
| Batter (H per V) | `batter` | 2 | 1 to 4 (0.1) | cutting / embankment slope | Region data (`EarthworkParams::batter`); per-road-type later; debug only |
| Vert. curve K × | `kMul` | 1 | 0.25 to 2 (0.05) | multiplies each type's crest/sag K (kc 10/30/100/182, ks 9/20/26/37 m per %) | Region data (road type `vertical_k_crest`, `vertical_k_sag`); the multiplier itself is debug only |
| Min segment m | `minSpan` | 4 | 1 to 20 (1) | shortest allowed segment (error text) | Tool setting (`DrawParams::min_span`) or region data |
| Water level m | `water` | 14 | 0 to 60 (0.5) | height of the water plane (and wet-ground tint) | World data (terrain / scene setting), not a tool control; debug only. sim_core has no water level yet |

Constants the panel does not expose but that matter for parity (code `config.js`, `earthworks.js`): road type table (name, carriageway width, total width, lanes, speed, min radius, max grade, K crest/sag, dual gap, edge colour), shoulder 1.0 m, formation drop 0.15 m, pavement height 0.12 m, bridge deck thickness 1.5 m, tunnel clear height 5.5 m, parapet 1.0 m, verge 1.5 m, wall thickness 1.5 m, wall max height 3.0 m, bench min 4.5 m, toe ditch 0.4 x 1.5 m, end taper batter 4:1, junction join dz 1.0 m and headroom 5.3 m. All of these belong in the region pack.

## E. Known rough edges in the prototype's UI

Found in code and confirmed by running unless marked (code only).

1. **Ending a road is hard, and what you see is not what you get.** There is no on-screen cue for how to finish. The only finishing gestures are Enter, right-click without moving, double-click, or snapping onto a road end/road. Finishing with Enter or right-click discards the hover segment: the road ends at the last CLICKED point, not at the ghost's end (seen: points at x=1100,1180 with the cursor at 1260 produced a road ending at 1180; `26_stroke_3pts_before_finish`, `27_finished_road_idle`). A user who sees a ghost to the cursor and presses Enter loses the last stretch.
2. **Double-click adds a stray point before finishing** (seen: the road ended `[1260,350] -> [1253,354]`, an 8 m hook). Each click of the double-click adds a point (the angle snap puts the second 8 m ahead), then `dblclick` finishes. This is probably how the owner experienced "ending a road was hard".
3. **The finish refusal for a bad crossing has no readable message.** `refuseFinish` stores a preview without readouts; `renderHud` then throws (`pv.spanLen.toFixed`, seen as a console error), so the HUD freezes on stale readouts and the `crossing too close vertically` text is never drawn. The only sign is the ghost turning red, and the error is wiped by the next mouse move (`28_error_crossing_too_close`).
4. **Refused clicks give no feedback.** A click while an error is current only sets a flash timer that has no visible effect (code: `state.flash` only forces a redraw).
5. **Snap kind is colour-only.** No label names the snapped road or end; green end vs road rings are identical; amber advisories do not tint the ghost or the ring, so they are easy to miss.
6. **Errors and warnings are text only, in the top-left HUD,** away from the cursor and not repeated near the offending spot; the profile panel is the only geometric cue (red zone, amber grade labels).
7. **No pre-click preview.** Before the first click there is no ghost, no width indicator and no hint of the selected road type beyond the HUD header; the type picker does not exist (keys 1-4 only; the help says so only in the closed-by-default modal).
8. **Earthworks only after commit.** Drawing shows no cutting, embankment, wall, bridge deck or tunnel; the user learns of a bridge or tunnel only from amber text. Tuning sliders regrade the whole network after a delay.
9. **Edit mode header shows the draw tool's road type** (not the selected road's), and edit mode can change type only via draw keys (which do nothing to the selected road).
10. **Height offset is invisible except in the HUD** (no vertical stem, no label at the ring) and is silently reset by every click; PgUp/PgDn in edit mode edit the hovered handle immediately without a ghost.
11. **Handle drag does not show earthworks following** until release (`34`), and a refused drag snaps back silently with a red HUD line.
12. **Undocumented keys** F and J; Backspace removes the last point but nothing shows that this is possible.
13. **Key conflicts and browser habits:** Tab, Backspace, PageUp/PageDown and the arrows are all captured by the page; while a tuning slider has focus every shortcut (including Ctrl+Z) is ignored until focus moves away.
14. **Frame stats panel overlaps the tuning panel** (both at top-right) and its 500 ms average showed 1486 ms/frame in headless capture because frames render only when something changes; the number is misleading when idle.
15. **Profile panel is not interactive and sized in CSS px** (min 220, 28% of height); no station readout, no cursor marker linking the profile to the 3D view.
16. **Dual types are two separate roads after commit** but the HUD and ghost treat them as one; selecting one carriageway in edit mode shows only that road (code: `finishRoad` creates `fwd` and `back` roads).
17. **Camera:** zoom goes to the target not the cursor; no focus-on-road; clamped to the 2 km world; keyboard pan speed scales with distance (can feel too fast when zoomed out).
18. **Crossing height refusal** (1 m to 5.3 m) is only found at finish; the ghost never warns while drawing near a crossing.

States not reachable headlessly or not reached: `cannot fit curve`, `cannot fit curve through crossing`, the `G1 fallback` advisory, and any state needing a GPU-driven view of the bore interior (`tunnel_inside` exists as a scripted view in `tools/scene.mjs` but was not captured here). Drag-and-drop file loading was done with the hidden file input (`setInputFiles`), not a real OS drop.

## F. Parity checklist

Legend. Milestones from `docs/unreal-game-plan.md` section C: M1 camera and ground, M2 click-click road and undo, M3 prototype-parity drawing, M4 async build, M5 earthworks and structures, M6 instances, M7 edit and other tools, M8 packaged build. `cp::` symbols are in `sim_core/include/cp/`. "Host" means the Unreal side computes it from sim data. Unreal classes: existing plan names (`UCPHudWidget`, `UCPOverlayComponent`, `UCPToolManager`, `UCPRoadDrawTool`, `UCPRoadEditTool`, `ACPCameraPawn`, `UCPInputConfig`, `UCPSettings`) plus NEW classes this spec adds (marked NEW).

### F.1 Elements

| Id | Element | sim_core data (`road_tool.h` unless noted) | Unreal class | Milestone | Status |
|---|---|---|---|---|---|
| E1 | HUD header (mode, type, one-way, colour mode, snap off) | tool state in host: `DrawSession::road_type()`, `one_way()`, `Modifiers::snap_off` | `UCPHudWidget` | M3 | available |
| E1 | HUD cursor line (z with offset, snap kind) | `DrawPreview::cursor.z`, `snap` (`SnapKind`), `DrawSession::height_offset()` | `UCPHudWidget` | M3 (cursor world position already in M1) | available |
| E1 | HUD readouts (span, bearing, R at cursor, min R, grade, max grade, G2, total) | `Readouts::span_length`, `heading_deg`, `end_radius`, `min_radius`, `grade_pct`, `max_grade_pct`, `length` | `UCPHudWidget` | M3 | available. GAP-6: no G2/G1 flag in `Readouts` (only the `G1Fallback` issue) |
| E1 | HUD errors | `DrawPreview::errors` (`Issue::code` MinSpan, Fold, CannotFit, Crossing, NoRegion; `text`) | `UCPHudWidget` (plus cursor-side toast, NEW below) | M3 | available. GAP-3: `Crossing` only arrives from `finish()` / `ClickResult`, not `hover()`, so the host must keep it visible |
| E1 | HUD advisories | `DrawPreview::advisories` (RadiusBelowMinimum, GradeAboveAdvisory, BridgeNeeded, TunnelNeeded, G1Fallback) | `UCPHudWidget` | M3 (BridgeNeeded/TunnelNeeded text M3; structures themselves M5) | available |
| E1 | HUD edit-mode selection line | edge id, length (`Edge`) | `UCPHudWidget` | M7 | GAP-2 (no `EditSession`) |
| E2 | Profile panel | needs per-station z, original-ground z, grade, vertical break list with curve lengths, per-type max grade, wall cap | NEW `UCPProfileWidget` (custom `OnPaint` or Slate canvas) | M3 (stroke) / M7 (selected road) | **GAP-1**: `DrawPreview` carries no profile series. Pieces exist: `VerticalProfile::build/breaks` (vertical_profile.h), `sample_centreline` (edge_sampling.h), `Heightfield::height`, `required_structures` (grading.h) |
| E3 | Tuning panel | `DrawParams` (snap, min span), `GradingParams`/`EarthworkParams` (wall_cap, wall_start, batter), per-type `vertical_k_*` | NEW `UCPTuningWidget` (debug build only) backed by `UCPSettings` | M3 (snap, min span), M5 (grading values) | partially available; GAP-8 (water level) |
| E4 | Help overlay | none | NEW `UCPHelpWidget`; key table generated from `UCPInputConfig` so it cannot drift | M3 | host only |
| E5 | Inspect bar / scene loader | `layout_io.h` scene export | debug command in `UCPWorldSubsystem`, not a widget | after M8 (optional) | host only |
| E6 | Frame stats panel | none (Unreal `stat unit` / own console var) | use engine stats; NEW optional `UCPDebugWidget` | M4 | host only |
| E7 | Cursor / snap ring | `DrawPreview::cursor`, `snap`, `would_finish` | `UCPOverlayComponent` | M2 (free ring), M3 (snap colours) | available; ring needs screen-constant scale (host, from the same `metres_per_pixel` it passes in `PointerInput`) |
| E8 | Placed-point dots | `DrawPreview::clicked` | `UCPOverlayComponent` | M2 | available |
| E9 | Ghost ribbon(s) | `DrawPreview::ribbons` (`committed`, `hover`, `width`); error state from `errors` | `UCPOverlayComponent` | M2 (straight), M3 (curves, dual, red) | available. Note `Ribbon::width` is the carriageway; the prototype draws `total` width: confirm which |
| E10 | Guides (extension, offset, angle) | `DrawPreview::guide` (`SnapGuide::type` Line/Offset, `polyline`, `faint`, `edge`, `offset`) | `UCPOverlayComponent` (dashed line mesh/spline) | M3 | available |
| E11 | Edit hover / selection line | `RoadIndex::nearest_point` (hover); `Edge` sampling for the line | `UCPOverlayComponent` | M7 | GAP-2 |
| E12 | Edit handles | `Edge::interior` plus end nodes | `UCPOverlayComponent` + `UCPRoadEditTool` | M7 | GAP-2 (data present, no session or drag preview) |
| E13 | Crosshair cursor | n/a | `ACPPlayerController` (`CurrentMouseCursor`) | M1 | host only |
| E14 | Heat-map road surface | `EdgeSample::curvature`, `grade` (edge_sampling.h) vs `RoadType::min_radius`, `advisory_max_grade` | road material parameter in `FCPRoadSurfaceLayer` | M5 or later polish (not M3) | available but unwired (GAP-7) |
| E15 | Junction surfaces / connectors (inspect) | `Junction` geometry, connectors (junction.h) | debug layer | after M8 | available |

### F.2 Inputs

| Id | Input | sim_core call | Unreal class | Milestone | Status |
|---|---|---|---|---|---|
| I1 | Left click | `DrawSession::click(PointerInput, Modifiers)` -> `ClickResult` | `UCPRoadDrawTool::OnClick` | M2 (free), M3 (snaps, auto-finish) | available |
| I1 (edit) | select road | `RoadIndex::nearest_point` | `UCPRoadEditTool` | M7 | GAP-2 |
| I2 | handle drag | `make_move_node`, `make_move_interior_point` (road_commands.h) | `UCPRoadEditTool` | M7 | GAP-2 (commands exist; no pick, no live preview) |
| I3 | right click finish / cancel | `DrawSession::finish()`, `cancel()` | `UCPRoadDrawTool` via `UCPInputConfig` | M3 | available (with the E.1 fix below) |
| I4-I6 | orbit, pan, zoom | none | `ACPCameraPawn` (limits in B.2) | M1 | host only |
| I7 | double-click | same as I3; recommend NOT porting the stray-point behaviour | `UCPRoadDrawTool` | M3 | available |
| I9, I10 | WASD / arrows / Q E | none | `ACPCameraPawn` | M1 | host only |
| I11 | Shift (snap off) | `Modifiers::snap_off` | `UCPRoadDrawTool` | M3 | available |
| I12 | 1-4 road type | `DrawSession::set_road_type(id)` | `UCPToolManager` | M3 | available (ids from region pack) |
| I13 | O one-way | `DrawSession::set_one_way` | `UCPToolManager` | M3 | available |
| I14 | C colour mode | none | `UCPToolManager` + surface layer | after M5 | host only |
| I15-I18 | P, F, J, H | none | `UCPHudWidget` / `UCPProfileWidget` / `UCPHelpWidget` | M3 | host only |
| I19 | Tab | none | `UCPToolManager` | M3 (stub), M7 | host only |
| I20 | Esc | `DrawSession::cancel()` | `UCPRoadDrawTool` | M2 | available |
| I21 | Enter | `DrawSession::finish()` | `UCPRoadDrawTool` | M2 | available |
| I22 | Backspace | `DrawSession::undo_point()` | `UCPRoadDrawTool` | M3 | available |
| I23 | Delete | `make_delete_edge(EdgeId)` | `UCPRoadEditTool` | M7 | available command; GAP-2 for selection |
| I24-I25 | PgUp/PgDn (+Shift) | `DrawSession::adjust_height(delta)` | `UCPRoadDrawTool` | M3 | available (draw); edit-mode handle height: GAP-2 |
| I26-I27 | undo / redo | `CommandStack` via `UCPWorldSubsystem::Undo/Redo` | `ACPPlayerController` | M2 | available |
| I28-I30 | tuning toggle, sliders, help click | n/a | `UCPTuningWidget`, `UCPHelpWidget` | M3 | host only |
| I31 | scene load | `layout_io.h` | debug | later | n/a |

### F.3 Feedback

| Id | Feedback | sim_core data | Unreal class | Milestone | Status |
|---|---|---|---|---|---|
| C1 | Ghost red on error, normal otherwise; amber does not tint | `DrawPreview::errors.empty()` | `UCPOverlayComponent` | M3 | available |
| C2 | Dual ghost (two ribbons) / single when one-way | `DrawPreview::ribbons` (one per carriageway) | `UCPOverlayComponent` | M3 | available |
| C3 | Snap ring colours and sizes | `SnapKind` | `UCPOverlayComponent` | M3 | available |
| C4 | Extension guide | `SnapGuide::Type::Line`, `faint=false` | `UCPOverlayComponent` | M3 | available |
| C5 | Parallel-offset guide | `SnapGuide::Type::Offset`, `polyline` | `UCPOverlayComponent` | M3 | available |
| C6 | Angle/length faint guide | `SnapGuide::Type::Line`, `faint=true` | `UCPOverlayComponent` | M3 | available |
| C7 | End / road snap auto-finish | `ClickResult::auto_finished`, `finished`, `finish.command` | `UCPRoadDrawTool` + `UCPWorldSubsystem::Submit` | M3 | available |
| C8 | Height offset in HUD | `height_offset()`, `Readouts::height` | `UCPHudWidget` | M3 | available |
| C9 | Profile cut/fill/bridge/tunnel bands, grade labels, vertical curves | see E2 | `UCPProfileWidget` | M3/M5 | **GAP-1** |
| C10 | Bridge/tunnel advisory text | `IssueCode::BridgeNeeded/TunnelNeeded` | `UCPHudWidget` | M3 | available |
| C11 | Structures after commit (deck, piers, portal, walls, regraded ground) | `structure_mesh.h`, `grading.h`, `earthworks.h` through `WorldCache` | `FCPStructuresLayer`, `FCPGroundLayer` | M5 | available as functions; not `WorldCache` layers (plan GAP 6) |
| C12 | Pending ghost until the new tile lands | none | `UCPOverlayComponent` + `UCPRenderSubsystem` | M4 | host only |
| C13 | Colour modes on committed roads | see E14 | surface layer | post-M5 | available, unwired |
| C14 | Edit hover / select / handle highlight, drag preview | see E11, E12 | `UCPOverlayComponent` | M7 | GAP-2 |
| C15 | Refusal message that stays visible | `FinishResult::error`, `ClickResult::errors` | `UCPHudWidget` + cursor toast | M3 | available if the host keeps it (fixes E.3, E.4) |
| C16 | Finish cue: "click here to finish" marker | `DrawPreview::would_finish` (the sim already flags a click on the last placed point) | `UCPOverlayComponent` (distinct marker) + hint text | M3 | available, **not in the prototype**; note `hover` returns early with no ribbon when `would_finish` is true, so the ghost vanishes under the cursor: keep the last ribbon visible |
| C17 | On-screen key hints for the active tool | none | NEW `UCPToolHintWidget` | M3 | host only (fixes E.1, E.7) |
| C18 | Type picker | region pack road types list | NEW `UCPRoadTypeBar` (inside `UCPHudWidget` or separate) | M3 | host only |

### F.4 GAPs, ranked (what blocked parity; status in section H.1)

1. **GAP-1, Profile series for the preview (blocks E2, C9).** `DrawPreview` has only scalar `Readouts` and ribbon centrelines. The panel needs per-station arrays: arc length `s`, road z, original-ground z, grade, curvature, structure flag (bridge/tunnel) and the vertical-curve break list (`s`, `z`, `length`) for the whole stroke including the hover segment, plus the road type's max grade and the wall cap. Proposal: add `struct ProfileSeries` to `DrawPreview` (and a matching one for a committed edge for edit mode), built from `VerticalProfile::breaks`, `sample_centreline` and `Heightfield::height`; cut/fill classification stays in the host.
2. **GAP-2, Edit session (blocks E11, E12, I1/I2/I23/I24 in edit mode, C14; M7).** There is no `EditSession` (plan gap): road pick by pointer, control-point list with hover/drag, live preview during drag without committing, one command on release (`make_move_node` / `make_move_interior_point`), height change of a point, and a regrade-deferred preview. Data (`Edge::interior`, `RoadIndex`) and the commands exist.
3. **GAP-3, Persistent refusal state (E.3, E.4, C15).** `IssueCode::Crossing` is produced at finish only, and a refused click returns `errors` once. The host must hold the refusal until the next successful hover or add a `DrawSession::last_refusal()`; and `hover()` should be able to warn about a refused crossing before the click (prototype never did; recommended: use `stroke_crossings` with `refuse_band` to put `Crossing` into `DrawPreview::errors` while drawing).
4. **GAP-4, Finish cue and finish-at-cursor semantics (E.1, E.2, C16).** `would_finish` exists, but `hover()` returns it with an empty ribbon and no readouts; the host needs the stroke ghost kept. Decide whether Enter / right-click should finish AT the cursor (include the hover point) as users expect; the prototype and `DrawSession::finish()` end at the last clicked point.
5. **GAP-5, Structure ranges in plan for the preview (C10, C11).** Advisories give text only. A `std::vector<StructureRange>` (grading.h `required_structures`) in `DrawPreview` would let the overlay shade bridge/tunnel stretches of the ghost, which the prototype never did but the owner may want ("bridge needed" text is easy to miss).
6. **GAP-6, G2/G1 flag in `Readouts`** (prototype shows `G2 ✓` or `G1`); only an issue code today.
7. **GAP-7, Heat-map data on committed roads.** `EdgeSample::curvature` and `grade` exist; the road surface layer needs a per-vertex or per-station colour/scalar and the type thresholds; not wired.
8. **GAP-8, Water level and water plane.** The prototype's `water` slider and plane have no sim_core equivalent (`structure_mesh.h` has `is_water` by threshold only). Needed for the "bridge over water" look and the viaduct scene; belongs in terrain/world data.
9. **GAP-9, Width semantics.** The prototype ghost uses road `total` width (with shoulders); `Ribbon::width` documents the carriageway. Confirm and, if needed, add `total_width` to `Ribbon` so the ghost matches the prototype's visual footprint (14.3 m residential).
10. **GAP-10, Region-data homes for the constants in D** (shoulder, verge, wall thickness, batter, K, ditch): some are in `region_pack.h` (`wall_cap`, `vertical_k_*`, `min_radius`, `advisory_max_grade`), others only exist as JS constants. Not urgent until M5.

## G. What the Unreal plan missed or under-specified (summary)

1. The HUD's exact content and the three-colour language (white ok, amber advisory, red error) and that the ghost recolours only on error.
2. The profile panel: a full 2D longitudinal section with cut/fill/bridge/tunnel colouring, grade labels and vertical curves is the prototype's main analytic UI and has no named Unreal class or sim_core data (GAP-1).
3. Finishing a road: no cue exists in the prototype, finishing discards the hover segment, and double-click adds a stray point; the plan only mentions "auto-finish".
4. Snap feedback is a coloured ring plus three guide types; the sim exposes them but the plan does not list the overlay primitives (ring, dashed draped lines, dots) or screen-constant sizing.
5. Camera numbers: distance 15 to 3800 m, polar 2.9 to 81.4 degrees, ground-plane pan, keyboard pan speed 0.9 x distance per second (x3 Shift), terrain follow with 3 m clearance.
6. Tuning panel and a settings home for each value; the prototype's sliders map to tool settings, region data or debug-only.
7. Colour modes (curvature / gradient heat map) and edit-mode highlighting are unmentioned.
8. Help and key hints, the road-type picker and the one-way/dual semantics (a dual stroke is two roads in one undo step), none of which exist in the plan's class list.

## H. Update after roadtool3 (11 Oct 2026): what sim_core now provides, and the host-side list

Source: all 45 screenshots in `docs/progress/2026-10-11-prototype-ui/` were opened for this round and every one has a row in `docs/progress/roadtool3/COMPARE.md` (screenshot, the sim_core field or behaviour that provides it, the test that pins it). Tests: `tests/test_tool_ui.cpp` (new), `tests/test_road_tool.cpp`, `tests/test_edit_tool.cpp`, `tests/test_clothoid_chain.cpp`. Sections A to G above describe the prototype and were not changed; this section supersedes the statuses in F where they differ.

### H.1 GAP status

| GAP | Status | Where |
|---|---|---|
| GAP-1 profile series | closed | `DrawPreview::profile`, `EditPreview::profile`, `ProfileSeries` (stations with road / ground z, depth, grade, water depth; grades with labels and over-limit; curves; structure ranges). Edit mode: the series of the WHOLE road chain (`join_profiles`), live while dragging. Text and axes: `profile_title`, `profile_empty_text`, `profile_axes`, `profile_band` (`cp/tool_hud.h`) |
| GAP-2 edit session | closed | `EditSession` (`cp/edit_tool.h`): hover picks, road-chain selection, handles, live drag, release as one command, point height, delete, change type / one-way |
| GAP-3 persistent refusal | closed | `DrawSession::last_refusal()`, `DrawPreview::refusal_active`, crossing errors while hovering (`DrawPreview::crossings`); `error_hud_line(text)` prints a reason a host holds (refused click, finish or edit) |
| GAP-4 finish cue, finish at cursor | closed, behaviour decided | `finish_at_cursor`, `would_finish`. The click-on-last-point finish is OFF by default now (`DrawParams::finish_pick_px = 0`): as in the prototype a click there is another point, or the min-span error (screenshot 12). Turn it on to get the cue |
| GAP-5 structure ranges in the preview | closed | `ProfileSeries::structures` (bridge / tunnel ranges from the cut / fill rule), advisories `BridgeNeeded` / `TunnelNeeded` |
| GAP-6 G2 flag | closed | `Readouts::g2`, printed as `G2 ✓` / `G1` by `draw_hud` |
| GAP-7 heat-map data | closed | `cp/road_heat.h`: `ColourMode`, `heat_vertices(graph, edge, mode)` (per centreline vertex: s, position, value, class, 0xRRGGBB), classes and colours from region data `road_tool.heat_map` |
| GAP-8 water level | partly closed | The tuning value exists (`ToolTuning::water_level`, region default `road_tool.water_level_m`), reaches `ProfileSeries::water_level` and each station's `water_depth` (`DrawParams` / `EditParams::water_level`). The water PLANE and the wet-ground tint are host / terrain work: pass the same value to `HeightfieldGround(hf, water_below)` (`structure_mesh.h`) so bridges over water follow the slider |
| GAP-9 width semantics | closed | `Ribbon::width == total_width` (the prototype's `total`: 14.3 / 10.8 / 10.8 / 17.45 m), `carriageway_width` separate; pinned by `Ghost widths and HUD headers per road type` |
| GAP-10 region-data homes | partly closed | New `road_tool` section in `region.json` (snap angle and length, min span, water level, K multiplier, chain turn limit, heat-map classes and colours) and `short_name` per road type (`RoadType::hud_name()`, "A road / dual c'way"). Still JS-only constants: shoulder, verge, wall thickness, ditch (grading round owns them) |

### H.2 New or changed sim_core interface (headers in `sim_core/include/cp/`)

- `tool_hud.h`: the words and numbers of the HUD, one source for every host. `draw_hud(DrawPreview, HudContext)` and `edit_hud(EditPreview, HudContext, tool_type_name)` return lines of runs (text, bold, tone Normal / Error / Warning / Muted) with the prototype's glyphs (middle dot, degree, infinity, check, cross, warning). Texts pinned to the screenshots: header (`DRAW · Residential street · colour: plain`, `one-way`, bold `single carriageway`, amber `snap off`), `cursor z 58.4 m (+25 m) · snap: angle`, `span 48.0 m · bearing 105° · R at cursor ∞ m · min R ∞ m`, `grade -2.5% · max 2.5% · G2 ✓ · total 48 m`, `✕ ...` errors then `⚠ ...` advisories, `selected road #N · L m · drag handles, PgUp/PgDn height, Del delete`. Also `ui_theme()` (ring colours and sizes per snap kind, ghost colours and opacity, guide colours and dash, dot, edit-line and handle colours and sizes, turn colours of the inspect view), `ring_style(SnapKind)`, `ghost_colour(preview)`, `help_rows(type names)`, `help_paragraph()`, `inspect_idle_text()`, `inspect_status_text(...)`, `snap_kind_word()`.
- `road_heat.h`: colour modes and per-vertex heat values (above). `next_colour_mode` is the C key.
- `tool_tuning.h`: the tuning panel as data. `tuning_fields()` (eight fields, label, range, step, scope Tool / Region / World, whether it regrades), `default_tuning(region)`, `clamped()`, `apply_tuning(t, DrawParams&)`, `apply_tuning(t, EditParams&)`, `tuned_region(base, t)` (a copy of the pack with wall cap, wall start, batter and the K multiplier; give it to `RoadGraph::set_region`, which refits every edge), `tuning_value_text`. Panel rules: values apply live, whole-network grading re-runs about 120 ms after the last change (150 ms for water), tuning is not undoable.
- `DrawPreview` now carries `road_type`, `road_type_name`, `one_way`, `dual_type` (no lookup by id in the host). `Readouts` unchanged. `DrawParams::water_level`, `DrawParams::finish_pick_px` default 0.
- `EditSession` (`cp/edit_tool.h`): `RoadChain` / `road_chain_of(graph, edge, max_turn)`: a road through junctions is ONE road for selection, handles, profile, length and Delete (two edges continue each other through a JUNCTION node of three or more edges when same type and one-way flag, tangents turn less than `road_tool.chain_max_turn_deg` = 25 degrees, and each is the other's best continuation; roundabout rings never chain; two roads that merely meet end to end stay two roads, as in the prototype). `EditPreview::selection` (`EditSelection`: chain, `number`, `length`, the selected road's own type name), `EditPreview::hover_chain` (the road to light up for ANY hover kind: the host highlights roads only, as the prototype did, while `hover.kind` still says node / interior point / road for clicks), `EditSession::select_at(point)`, `handles()` in order along the road with each junction node once. `delete_selected`, `change_type`, `set_one_way` act on the whole chain as one command.
- Edits make junctions as the prototype did (`conformRoad`): when a drag, a point height change or a dropped free end puts the road across another at the same level (or onto it), `EditResult::command` replaces the edited edge by the drawn stroke through its new control points (the same code as drawing: T = 3 arms, crossing = 4), still ONE undo step, and `made_junction` is set. The edited edge's id is gone afterwards: the host calls `invalidate()` and `select_at(result.anchor)`. Dragging a node shared by several roads only advises (`CrossingAtGrade`, "crosses a road at the same level (x m apart): becomes a junction" / "without a junction" with `make_junctions` off). `EditParams::make_junctions`, `chain_selection`, `water_level`, `world_margin`.
- `fit_clothoid_chain` now refuses runaway chains itself (no span more than 20 times its chord; a stroke that doubles back onto an earlier point gave 1e15 m spans). `fit_sane_chain` is the same call; the stopgap is gone.
- Advisory words: `grade 38.2% above advisory 10%` (limit printed as a whole number when it is one), road type names in advisories use `hud_name()`.

### H.3 Differences from the prototype, declared

1. Selected-road number: the prototype's `#N` is its road creation order; here `#N` is the lowest engine edge id in the chain (stable, unique, but not creation order after splits). Cosmetic.
2. Edit header shows the SELECTED road's type (the prototype showed the draw tool's, rough edge E.9).
3. Dual ghost is refitted through the offset points as committed (the prototype offset the centreline). Measured on a 60 degree arc: worst vertex distance from the exact offset of the fitted centreline 4 cm at R 400 m and 4 cm at R 60 m (`Dual ghost` test).
4. A refused drag release returns its reason (`EditResult::error`) instead of snapping back silently.
5. The finish-on-last-point click is an option, not the default (see GAP-4).
6. Hover reports nodes and interior points of any road for click logic; `hover_chain` is what to highlight.
7. A snapped road end, node or road keeps its own height; the prototype added the PgUp / PgDn offset to a snapped height too (it could put a road end 25 m off the road it joins).
8. The tool's HUD prints the road type's `short_name` when the region data has one (`A road / dual c'way`); the long name stays in `name`.
9. The help table has 14 rows, not 13 as A.4 counts.

### H.4 Host-side (Unreal) list, milestones 2 to 3

Nothing below needs a rule of its own: each item draws or routes data sim_core already returns.

1. HUD widget: draw the lines of `draw_hud` / `edit_hud` in order, runs in their styles, tones as theme colours (`ui_theme().error`, `.warning`, `.muted`, `.text`), glyphs exactly as returned (needs a font with the middle dot, degree sign, infinity, check mark, cross and warning sign), panel at (12, 12), max width `min(560, viewport - 24)`, 82% dark background. Feed `HudContext` from the host's colour mode, Shift state and `DrawSession::height_offset()`. Print refused click / finish / edit reasons with `error_hud_line`.
2. Profile panel: height `min(220, 0.28 * window)` when shown, P toggles. Axes from `profile_axes`, title from `profile_title`, empty text `profile_empty_text`. Layers back to front as in A.2: under-ground fill, per-segment band coloured by `profile_band(mean depth, wall_cap)` (fill blue, cut orange, over cap red), ground line, road line, vertical-curve bands (`curves[i].s0..s1` where `length > 0`), control dots, grade labels from `grades[i].label` at `s_mid` (amber when `over_limit`). Draw `water_level` if wanted (not in the prototype).
3. Tuning panel (debug build): eight sliders built from `tuning_fields()` (label, min, max, step, value text); on change `clamped`, `apply_tuning` to the sessions; for Region scope fields build `tuned_region(base, t)` and `RoadGraph::set_region`, then regrade after 120 ms (150 ms water); pass `water_level` to `HeightfieldGround`.
4. Help overlay from `help_rows`, `help_title`, `help_paragraph` (open at start, H or ? toggles, click closes); key table generated from the input map so it cannot drift. Inspect bar from `inspect_idle_text` / `inspect_status_text`. Frame stats panel (F): host only, 500 ms average, monospace.
5. Overlay primitives with `ui_theme()` values: cursor ring per `ring_style(snap)` (11 px, free 7 px, screen-constant, flat on the ground 0.5 m up), placed-point dots (4 px, 0.4 m up), ghost ribbon(s) from `DrawPreview::ribbons` at `Ribbon::lift` with `ghost_colour(pv)` at 55% opacity and depth test off, guides from `DrawPreview::guide` (dashed, 4 m dash 3 m gap; faint = angle / length grid at 50%), edit lines (hover `edit_hover_line`, selected `edit_selected_line`, 0.8 m up) along `EditSession::centreline` of every edge in `hover_chain` / `selection.chain`, handles from `EditSession::handles()` (7 px white, hovered 10 px gold; the hovered one is `hover.handle` on `hover.edge`).
6. Input map and gestures: left click = `click`, Enter / right click without drag = `finish` (or `finish_at_cursor` if the owner wants what-you-see), Esc = `cancel`, Backspace = `undo_point`, PgUp / PgDn and ] / [ = `adjust_height(+-1, Shift +-5)` in draw mode and `adjust_point_height(hover.edge, hover.handle, +-delta)` in edit mode, 1 to 4 = `set_road_type`, O = `set_one_way`, C = `next_colour_mode`, Tab = draw / edit, Delete = `delete_selected`, Ctrl+Z / Ctrl+Y = the command stack. A press that moves more than 4 px (`EditParams::drag_threshold_px`) is a drag, not a click. Pass `metres_per_pixel_per_metre = 2 tan(fov / 2) / viewport_height_px` (or `metres_per_pixel`) and, over a bridge deck, `surface_hit`. After executing any command call `invalidate()` on the session; after an edit result with `made_junction` call `select_at(result.anchor)`.
7. Heat-map road surface: `heat_vertices(graph, edge, mode)` per road, colour per vertex (class colours from region data), interpolated across the carriageway rows; the ghost is never heat-coloured; mode names in the HUD header.
8. Scene loader and connector overlay (inspect): `layout_io.h` scene export, connectors from the junction code, colours `ui_theme().turn_*`; J toggles; optional, after M8.
9. Profile colours and wall cap: the red zone uses `ProfileSeries::wall_cap` (the live tuning value, via `tuned_region`).
10. Structures after commit (deck, piers, portals, walls, regraded ground; screenshots 37 to 40) come from `WorldCache` / `structure_mesh.h` / `grading.h`, owned by the earthworks and world-cache rounds; not part of this tool. During a drag the terrain regrades on release only (34).
11. Not ported on purpose: the double-click stray point (E.2), a frozen HUD on a refused crossing (E.3), a silent refused click (E.4).
