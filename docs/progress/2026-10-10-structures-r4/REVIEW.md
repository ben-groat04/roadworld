# Structures round 4: cold-read review (geometry / engineering only)

Opened, round 4 renders: s4 street, aerial, end; s5 street, toe; s6 aerial, end; s7 aerial, between, end (10 of 18; alongA/alongB/s4 along, s5 aerial/along, s6 street, s7 street/alongA/alongB not opened).

References opened (11): street/04_cutting-netted-rock-with-wall-end, 05_gabion-wall-bank-hazard-sign, 09_cutting-sandstone-narrow-lane, 10_cutting-gneiss-rock-face-toe-ditch, 11_cutting-chalk-terraced-overhead, 13_embankment-grass-slope-gorse-ireland, 14_embankment-grass-fence-low-sun; aerial/26_a9_gelly_overbridge_earthworks_scotland, 28_a3_hindhead_tunnel_portals_cutting_uk, 29_gasteiner_strasse_tunnel_portals_rock_cutting_austria, 31_a66_gabion_retaining_wall_uk. No gsv screenshots opened.

## 1. Scores (r1 / r3 / r4)

| Scn | r1 | r3 | r4 | Notes |
|---|---|---|---|---|
| s4 retaining wall run | 3 | 3 | **4** | Single 9-10 m wall is gone, but replaced by a ziggurat (see 2). Ends better. |
| s5 cutting | 6 | 5 | **5** | Smooth ~1:2-ish grass slopes, plausible. Ditch not visible in street or toe view; claim unverifiable from images. |
| s6 embankment | 2 | 2 | **2** | Aerial shows a ribbon on a flat plane; end view shows road vanishing over a flat horizon. No bank is readable, so nothing can be credited. |
| s7 adjacent levels | 3 | 4 | **5** | Shared wall reads as one structure and the upper road no longer has a duplicate wall. East end still broken. |

What improved: s4 wall ends now taper diagonally into the slope instead of a vertical slab (s4 end, centre-left, the three upper tiers). Wall tops follow the ground. s7 shared wall is clean and continuous (s7 between).
Newly wrong or still wrong:
- s4 aerial, upper-centre: the highest tiers taper out as short lens-shaped slivers, a "pyramid of wedges". Looks procedural, not engineered.
- s4 aerial/end, lower right: fill-side tiers are thin ledge-walls each about a metre of visible face with a long bare strip below. They read as vineyard terraces.
- s4 end, near road end: a hard horizontal crease line runs from the road end across the hillside. A flat-topped notch with no transition.
- s7 end, centre-right: dark pit/gouge plus striped near-vertical faces (known). Also a faint vertical-striped scar high up the slope, upper centre.
- s7 end: upper-road wall ends in a staircase of tiny steps, not a smooth taper.

## 2. Are eight stacked ~3.8 m walls with ~3.5 m benches believable?

No: it reads as an absurd ziggurat. s4 street shows a wall face of at least four tiers receding, over 15 m of continuous brick, from the driver's seat. Eight tiers is about 30 m of masonry up a hillside.

What the opened references show for roads on steep ground:
- **One cut face in rock, often tall and near-vertical**: street/10 (rock face roughly 10+ m, a grass verge plus shallow channel of a few metres at the toe, no wall, no benches); aerial/29 (rock face directly above the portal); street/04 (netted rock face plus one concrete wall end, roughly 8 m, a single face).
- **A continuous graded slope**: street/11 chalk cutting, a single slope on both sides, perhaps 1:1.5 to 1:2 by eye, with only a faint track-like bench. Aerial/28 Hindhead: grassed slopes with gorse, one short retaining wall near the portal.
- **One low wall plus slope**: street/05 gabion wall about 2 m under a steep grass bank; aerial/31 gabion wall in 2-3 steps, each roughly 1 m, grass between. This is the only multi-tier example, and it is small.
- **Embankments**: street/13 smooth grass slope about 1:2 to 1:3, ditch or verge strip at the toe; street/14 similar.
- **Construction earthworks** (aerial/26): wide plain cut/fill slopes, no walls at all.
- **Absent**: no reference shows 3 or more stacked walls of 3+ m, no viaduct or half-bridge on hillside, and no wide-berm benched rock cut (aerial/29 shows none). Absence across 11 images suggests stacks of tall walls are rare. Not proof.

Recommended rule (numbers are my judgement; the photos support the shape, not the figures):
1. Prefer a graded slope when it fits: 1:2 in soil (s5 look and street/11 and 13 support this), steeper only in rock.
2. Walls: at most 1-2 tiers, each a maximum of about 4-5 m. Beyond that, switch to a rock-cut face (steep, 1:0.25, matches street/10 and 04) on the cut side, and a single taller wall or viaduct/half-bridge on the fill side.
3. If a bench is kept, make it a real berm of at least 4-5 m with a slope between walls (not wall-on-wall), and cap at 2-3 tiers. I cannot cite a photographed berm width.
4. Drop the "8 tiers" ceiling entirely; if the formation cannot be reached in 3 tiers, report the hill as too steep for a road and offer the viaduct option.

## 3. Specific fixes: confirmed / partly / not

- Wall ends diagonal: **partly**. Taper present (s4 end), but staircased and with a crease at the road end.
- Wall tops follow steep ground: **confirmed** (s4 aerial, s7 between).
- s7 shared wall: **confirmed**, a single clean 5.5 m wall (s7 between, aerial).
- s7 east-end pit: **not fixed** (s7 end).
- Toe ditch: **not verifiable**. Not visible in s5 street or toe. A 0.27 m depth cannot be seen at this camera distance, so it fails as a deliverable. Cannot judge whether 1.5 x 0.4 m is sensible; street/10 shows a roadside channel of a few metres but I cannot measure depth from it.
- Embankment 1:4 end: **not verifiable**; s6 views show no bank.

## 4. Is the test terrain realistic?

A uniform 0.85 gradient (about 40 degrees) hillside is not found in any reference, which show 1:2 to 1:3 grass slopes and rock only where it is cut. So s4 is an extreme stress test and the ziggurat is partly the rule failing gracefully on an impossible hill. Keep it as a stress case, but add a realistic scenario at 1:3 to 1:2 (about 0.3-0.5 gradient), where the rule should produce 0-1 walls. The rule is still wrong in that it keeps stacking instead of switching strategy.

## 5. Ranked fixes

1. Grading rules: cap tiers (2-3); beyond that choose rock-cut/steep slope or viaduct, not more walls.
2. Test scenario: add a 0.35-0.5 gradient side-hill, and an embankment scenario with a camera that sees the bank (plus top-down toe cross-section).
3. Grading rules / structure_mesh: fix s7 east-end pit and striped faces; clamp tapering of shared structure.
4. structure_mesh: replace staircase wall end with a smooth sloped cap; remove the s4 crease at road end and the sliver wedges at the top tier.
5. Grading rules: wider benches with slope between tiers (at least 4-5 m); remove thin fill-side ledges.
6. Export tool / viewer: give s5/s6 cameras close to the toe and bank so ditch and 1:4 runout can be judged; render ditch with a colour or cross-section.

**Terracing: REVERT as the default for steep ground**, keep the diagonal ends, top-follow and shared s7 wall. Keep terraces only behind a 2-3 tier cap with rock-cut fallback. Net: ziggurat is worse for realism than a single tall wall; the single wall is also unrealistic, so both are placeholders.

## 6. Confidence

Medium. The ziggurat verdict is high-confidence (no reference shows anything similar). Specific heights and berm widths are by eye and not photo-supported. The s5/s6 and the ditch/embankment-end judgements are low-confidence because the views do not show the feature.
