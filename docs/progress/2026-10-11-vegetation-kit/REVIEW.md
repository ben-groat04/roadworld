# Cold-read review: vegetation kit, 11 Oct 2026

## 1. Files opened
Kit images (all 16): scene_a closeup / elevated / eye; scene_b elevated / eye; scene_c elevated / eye; sheet_canopy_blocks, grass_LOD0, grass_LODs, hedges_LOD0, hedges_LOD1_LOD2, shrubs_LOD0, shrubs_LODs, trees_LOD0, trees_LODs.

References (11 opened): street/35_rural-lane-hedges-sun-cloud, street/36_rural-b-road-low-sun-winter, street/37_rural-single-track-fog-wet, street/31_dual-central-reservation-grass-barrier, street/32_dual-street-lights-verge-kerbed-channel, street/13_embankment-grass-slope-gorse-ireland, street/28_junction-roundabout-splitter-island-elevated, street/27_junction-mini-roundabout-village, aerial/21_sixhills_lane_hedged_fields, aerial/19_a23_handcross_lane_green_countryside, aerial/18_a737_dual_carriageway_overcast. Scene a: 35, 36, 37, 21. Scene b: 31, 32, 13, 18, 19. Scene c: 28, 27, 32.

## 2. Scores (would pass in a mid-distance photoreal roadside shot)
| Category | /10 |
|---|---|
| Hedges | 4 |
| Grass | 5 |
| Shrubs | 4 |
| Broadleaf trees | 5 |
| Conifers | 2 |
| LODs | 3 |
| Overall | 4 |

## 3. Faults and fixes

**Hedges**
- Reads as a green box with dark plates. Dark opaque core shows as flat black-green slabs between cards and at hedge ends (scene_a_closeup, scene_a_elevated end caps show a dark arched "gravestone" face). Real hedge in street/35 and street/36 is uniformly leafy to the base with no visible core. Fix: tint the core to the mean leaf colour (about 45,60,30) and lighten the E00/E01 end faces; cover the end faces with cards at the same density as the sides.
- Cards too big and too uniform: leaf plates about 0.4-0.8 m read as ivy or maple leaves (scene_a_closeup, sheet_hedges_LOD0). Street/35 and street/36 show fine twiggy texture with bare shoots and no large leaves. Fix: card size 0.15-0.30 m, roughly 3x the count, some twig/stem cards; hold tris near 5-8k for 4 m.
- Top line is too straight and flat-capped (sheet_hedges_LOD0 trimmed V00-V03, scene_a_elevated left hedge). Street/36 and aerial/21 show gently undulating tops with small shoots sticking out. Fix: add +-0.1-0.2 m low-frequency height wobble over 3-6 m, plus a few shoots 0.2-0.4 m above the top.

**Grass**
- Mown strip is uniform bright green with dark gaps (scene_a_eye, scene_c_eye). Real verges (street/35, street/32) are denser, softer and have colour patches; street/32 also has a pale kerb-edge weed fringe. Fix: raise density to hide gaps, use 3 or more tint groups (+-15% value, one yellowish), and vary clump height 0.06-0.15 m.
- Edge strip reads as a pasted tile with a lighter outline and cabbage-like weed rosettes along the asphalt (scene_a_closeup, scene_c_eye). Street/32 and street/35 show a soft grit/soil line with sparse encroachment. Fix: drop the visible light-green base patch, use sparse blade clumps over a brown grit decal, rosettes 0.05-0.1 m not 0.2-0.3 m.
- Rough/tussock patches are laid out in a visible grid with a bare ground between them (scene_b_elevated). Street/37 and street/13 show continuous long grass with random dry tufts. Fix: jittered Poisson placement at 2-3x density; keep tussock height 0.5-0.9 m.

**Shrubs**
- Leaf cards oversized and the shrub reads as a floating cabbage with a thin base (sheet_shrubs_LOD0 top row). Gorse looks like palm fronds with spots (sheet_shrubs_LOD0 bottom row); street/13 gorse is upright, spiky, dense olive with sparse yellow dots. Fix: gorse spray cards 0.15-0.25 m, upright growth, dark core removed (black holes visible), flowers 5% of cards.
- LOD2 is a faceted icosahedron, clearly a rock (sheet_shrubs_LODs). Fix: noisy lumpy blob with 40+ tris, textured leaf colour.

**Broadleaf trees**
- Open type is a thin sparse "lollipop" with few leaves (sheet_trees_LOD0 row 2). Street/32 and aerial/19 show full rounded crowns. Fix: drop or double crown density for M and L; crown width at least 0.7x height; 4-6 visible main limbs.
- Round type is good but crowns read slightly dense and lumpy, trunk too short (scene_a_elevated; real hedgerow trees in aerial/21 and street/35 have trunks about 1/3 of height). Fix: clear trunk 3-5 m on L (16 m), crown depth 8-10 m.
- Scale versus road is plausible (scene_a_eye, scene_c_elevated) and the 28 m belt in scene_b_elevated reads decently against street/31, but the belt is too tidy: all trees one height. Fix: height variation +-30% and some understorey.

**Conifers**
- Stacked tiers like a Christmas tree, very dark, with a spiky top finial (sheet_trees_LOD0 bottom row, sheet_trees_LODs). Aerial/19 and street/37 show soft-edged irregular plantation conifers. Fix: 40% more whorl cards in random rotation, droopier, less regular spacing, colour up to about 45,70,40, drop the finial.
- LOD2 is a three-tier cone (sheet_trees_LODs bottom left). Fix: use a noisy, narrower cone, with a 2-3 m trunk visible.

**LODs**
- LOD2 hedges are a plain green slab with a few cards (sheet_hedges_LOD1_LOD2 right-hand items) and visibly lighter and flatter than LOD0. Fix: bake the LOD0 leaf look into the LOD2 albedo colour, add a gentle noise to the top.
- LOD2 tree lumps (icosahedron balls) read as boulders in scene_a_eye (distant stacked polyhedra at road end) and scene_b_elevated (right-hand block). Fix: replace with fewer, larger, noise-displaced blobs with subdivision, or a crown card cluster; lower contrast to the sky.
- Canopy blocks are smooth trapezoid mounds (sheet_canopy_blocks, scene_b_elevated right). Aerial/18 and aerial/21 show woodland as fine mottled texture, with soft tree-sized bumps. Fix: add 1-3 m noise bumps, a mottled leaf texture, uneven top edge.

## 4. Already good enough to keep
- Round broadleaf L trees in scene_c_eye and scene_a_elevated: trunk plus clumpy crown, plausible against a 3.65 m lane.
- Overall hedge placement logic and gateway module idea (scene_a_elevated).
- Mown grass blade shape up close (scene_c_eye) and tussock/rough clump silhouettes (sheet_grass_LOD0).
- Bracken fronds (sheet_grass_LOD0), matching street/37 bracken.
- Tree belt layout in scene_b_elevated: mixed heights, understorey shrubs, dark interior shade.
- Sapling with stake (sheet_trees_LODs): correct for new planting (cf. street/13 staked saplings).

## 5. Top 5 changes by payoff
1. Hedge core colour matched to leaf colour; hedge cards 0.15-0.30 m with 3x count; end faces covered.
2. Hedge top-line wobble and stray shoots; rework LOD2 hedge colour.
3. Fix LOD2 tree/shrub/canopy blobs (icosahedron or tier shapes to noisy lumps) so far ranges do not read as boulders.
4. Verge grass: denser, tint variation, soft grit edge instead of the pasted green edge strip; jittered placement of tussock.
5. Conifer rework (irregular whorls, lighter colour) and open broadleaf crown density.

## 6. Confidence and limits
Confidence moderate-high on shape, density and scale faults (seen across several images each). I could not judge motion/wind, real Unreal shading, subsurface translucency, colour in the actual game lighting, or true card size (estimated from the 1 m ruler and 1.8 m figure). Winter or bare-state looks (street/36, aerial/21 colours) are not built, so seasons were not scored. Trees in the references are mostly bare (street/36, street/32, street/13), so crown shape was compared by overall silhouette only.
