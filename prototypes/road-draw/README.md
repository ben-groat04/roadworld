# Road Draw prototype (3D, throwaway UX test tool)

Edit `src/*`, run `node build.mjs`, then open `index.html` (generated, do not edit) in Chrome or Edge (double-click it, no server needed). It loads three.js from jsdelivr, so it needs internet access.

## Controls

- Camera: right-drag rotate, middle-drag pan, wheel zoom, WASD/arrows pan, Q/E rotate
- Drawing: left-click add point, double-click / Enter / right-click finish, Esc cancel, Backspace remove point
- Snapping: hold Shift for no snapping
- Height: PgUp/PgDn or `]` / `[`
- Road types: 1-4; O toggles one-way (single carriageway for dual types)
- View: C colour mode, Tab edit mode, P profile panel, H help
- Undo/redo: Ctrl+Z / Ctrl+Y

## What it shows

Cuttings and embankments (1:2 slopes), retaining walls, bridges, tunnels and water.

## Tests

`node prototypes/road-draw/test-geom.mjs` checks `src/geom.js` directly.

Scene invariants: once, `cd prototypes/road-draw && PLAYWRIGHT_SKIP_BROWSER_DOWNLOAD=1 npm install` (uses your installed Edge/Chrome, downloads no browser); then `npm run check` (= `node build.mjs && node tools/scene.mjs --check`). Screenshots: `node tools/scene.mjs <outDir> [view ...]`.

Not production code: findings feed the real tool in Unreal.
