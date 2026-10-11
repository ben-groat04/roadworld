# Structures round 5: cold-read review (shape and engineering plausibility only)

## 1. Files opened

All 31 r5 PNGs (s4 x5, s4b x5, s5 x6, s6 x6, s7 x7 incl. end_west). r4: s4 aerial, s4 street, s7 end (also read r4 REVIEW.md).
References opened (8): street/04_cutting-netted-rock-with-wall-end, street/05_gabion-wall-bank-hazard-sign, street/10_cutting-gneiss-rock-face-toe-ditch, street/11_cutting-chalk-terraced-overhead, street/13_embankment-grass-slope-gorse-ireland, aerial/26_a9_gelly_overbridge_earthworks_scotland, aerial/29_gasteiner_strasse_tunnel_portals_rock_cutting_austria, aerial/31_a66_gabion_retaining_wall_uk. Eight relevant photos were available and opened (more exist in the folder; not all are earthworks). The reference INDEX was not used as evidence.

## 2. Scores (plausible earthworks shape, /10)

| Scenario | r4 | r5 | Note |
|---|---|---|---|
| s4b realistic side hill | n/a (new) | **6** | best result; plausible except wall ends |
| s4 stress 0.85 | 4 | **4** | ziggurat gone, but the rock cut is not readable and the stress case is mostly an unrelieved slope |
| s5 cutting | 5 | **6** | smooth, plausible; toe ditch invisible |
| s6 embankment | 2 | **4** | bank now readable in 3 views |
| s7 adjacent levels | 5 | **4** | end views still broken; r4 s7 end looks almost identical |
| Overall | ~4 | **5** | |

## 3. Faults per scenario

**s4b.** s4b_street, far end of the left wall (x 720-830, y 430-465): wall tapers as a staircase of small steps, then a short separate wall stub at the far end (x 770-830). s4b_aerial, left wall start (x 390-460, y 585-600): top steps down in 3-4 blocky notches, ending abruptly with a vertical end face rather than burying into the bank. Same aerial, upper wall (x 650-720, y 490-525): a ramp-shaped end with a visible stair. s4b_aerial: the fill-side wall is a thin low ledge about 0.5-1 m tall running the whole length (lower right diagonal); reads like a kerb/low retaining edge, plausible but never ends convincingly. s4b_end: the fill-side low wall ends in a pointed sawtooth wedge (x 790-870, y 500-540) and floats on the slope there. s4b_alongA (x 1035-1075, y 485-505): isolated small wall block sitting on the right verge, detached from anything. Cut-side wall of 3 m plus slope above matches how a real road on a 1:2.5 hill is built (street/05 gabion wall under a grass bank); wall height is right. Wall tops follow ground.

**s4.** The ziggurat is gone: there are no stacked walls anywhere (s4_street, alongA, alongB are one smooth steep slope). But: s4_aerial shows three dark parallel bands up the hillside (x 0-1200, y 230-560) that read as shadow-terrace stripes: these are the catch berms, visible only as shading, so nobody can tell it is a rock cut. s4_end (x 700-900, y 320-500): the uphill berm bands converge into a crumpled fan with a dark crease at the road end (x 800-880, y 470): a pit-like pinch. Fill side shows no wall and no marked "needs structure" feature; a 40 degree fill slope with a road on it looks like a bare cliff. The face looks like an unbroken 1:0.5-ish grass wall (s4_alongA/B): no benches visible from the road, so the rule is unverifiable by eye.

**s5.** s5_street/along/end: smooth sections, soft shoulders, good. s5_toe and toe_section: just flat grass; ditch not visible at any distance (0.27 m cannot be seen; a grass-only renderer cannot show it). s5_aerial: faint pale blob on the left of the road (x 150-700, y 330-640) is a lighting artefact on the slope, harmless but confusing. Grade-out at top of cut (s5_street, x 480-520 and 1080-1100, y 390): a slightly rounded crest, fine.

**s6.** s6_street, s6_along: road is on the top of a long bank, runs off to a flat horizon; bank edge reads. s6_runout_close (x 0-1280 sweep): smooth run-out into flat ground, no step or crease; good. s6_aerial: bank is nearly invisible from above (only a faint yellow shade band, x 330-700, y 400-560 in s6_end). The bank is shallow (about 1:4), which is plausible versus street/13 (about 1:2-1:3) but is too gentle to confirm the steeper fill branch. Subtle faint vertical stripes in the grass at the run-out (s6_runout_close, x 750-900, y 430-520): texture-tile seam, minor.

**s7.** s7_street: single 5.5 m wall, continuous, flat top, with no steps, fine. s7_alongA (x 130-230, y 50-110): wall top has an abrupt 1 m stair jump, then straight; the cap steps up with a vertical riser (a "tooth"), not a taper. s7_alongB (x 380-420, y 535-555): the low fill-side wall has a bulged, uneven lower edge with a rough patch (poor base edge). s7_between: both walls clean and parallel, good; wall ends at the far tip vanish into a corner crease (x 1000-1120, y 380-440). **s7_end and s7_end_west (the builder's stated unfixed fault):** (a) a dark pit/gouge beside the end of the upper road (end: x 890-940, y 440-520); (b) striped near-vertical grass faces along the slope (end: x 930-1370, y 500-740, plus upper x 480-800, y 150-190): looks like a stack of vertical louvres; (c) both wall ends finish in a 4-5 step stair (end: x 600-700 and 700-790); (d) a stray pitched ramp/wedge shelf between the roads' ends (x 750-910, y 430-480). Compared to r4's s7 end, the image is almost unchanged: not fixed.

## 4. Good and should be kept

- No stacked terraces at all; wall count per side max 1 here. The ziggurat is gone (confirmed on s4 street, alongA/B, aerial).
- s4b: one 3 m wall plus slope on the cut side and a low edge wall on the fill side; heights consistent, tops follow terrain.
- Smooth slopes in s5 and s6 with seamless grade-out to natural ground (compare street/13 and street/11 for slope shape).
- s7 shared wall (street, between, aerial).
- Rule shape (slope, then one wall, then rock cut or needs_structure) is supported by references: street/10 and aerial/29 show cut faces in rock with no walls; street/04 shows a netted rock face plus one wall end; aerial/31 shows low gabion walls.

## 5. Ranked top 5 fixes

1. s7 east/west ends: remove the dark pit and the striped near-vertical faces (apply the slope-from-wall-top taper and smooth the end blend; check the cut face is not emitted as vertical quads with grass uv).
2. Wall ends everywhere (s4b street/aerial/end, s7 end): replace staircase tops with a single linear ramp cap over 8-12 m and bury the end in the slope; remove detached wall stubs (s4b street far end, s4b alongA right-verge block).
3. Make the rock cut readable: paint class-2 texels grey-brown and show berm edges, then re-render s4 (currently berms are only dark stripes, and the end shows a crease pinch at s4_end).
4. s4 fill side: flagged "needs structure" should show something (a marker colour or a slope in a different tone); a bare 40 degree grass cliff is not accepted.
5. Add a camera that makes the toe ditch and the s6 bank angle checkable (low, close side view with the ditch coloured), and a steeper embankment case (about 1:2) to test the fill branch.

## 6. Confidence and what could not be judged

Medium. The ziggurat verdict is high confidence. Wall heights and berm widths are judged by eye only; I cannot check the numeric rule, and only 8 reference photos were opened (none is a benched rock cut, so the benches judgement uses no photographic support). The s5 ditch and the s6 bank gradient could not be judged from the images. I did not open the fine detail of the s4b alongB or s6 along images beyond the overall silhouette, and a flat grass renderer hides faces, so slope steepness is judged from silhouettes.
