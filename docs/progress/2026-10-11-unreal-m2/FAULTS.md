# M2 fault list (what the owner, who knows the prototype, would say differs)

Written against the 26 opened prototype screenshots (see STATUS.md). Nothing was rendered, so these come from code against pictures.

## Pass 1
Found against 04, 13, 26, 28 and the HUD in 03 to 22: HUD was top-right (prototype top-left); a refused finish lost its red ghost and text (28); no cue how to finish (spec E.1). Fixed: HUD top-left; refusal text kept 5 s and ghost stays red; bottom cue bar; ghost held when the cursor is on the last point.

## Pass 2
Re-read the glyphs and widths in 13, 16, 18 to 22: glyphs, ghost width, ghost opacity and depth test differ; no G2 flag. Not fixable in M2 without the editor or sim changes (see Residual).

## Residual
No profile panel, tuning, help, frame stats, colour modes (not M2). Ghost drawn at carriageway width not total width (sim GAP-9). Ghost opacity and depth test differ (material unverified, depth tested). Glyphs X, ! and inf instead of the prototype's cross, warning sign and infinity. No G2 flag (sim GAP-6). Rings are thin circles. Overlay lines do not draw in Shipping builds. Esc may stop play-in-editor. Unknown until run: whether the sim hides the ghost on the min-span error as the prototype does.
