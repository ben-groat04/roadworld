# Reference notes (round 7). Opened with Read: street/38-49 (12 new, fetched this round) and street/04, 05, 10, 13 (16 photos; docs/reference/, private, not copied here)

New photos (Wikimedia Commons / Geograph, CC BY-SA 2.0, source URLs in docs/reference/street/ATTRIBUTION.md, one line each in street/INDEX.md): 38 A27 chalk cutting with a bench track, 39 A3 Butser chalk cutting, 40 M5 wooded cutting, 41 A629 rock cutting, 42 / 43 / 44 / 45 embankments meeting fields or fenced verges, 46 / 47 gabion walls, 48 brick wall with a vertical end, 49 fresh earthworks in chalk fields (weak: no road in frame, only the soft landform).
Gaps still open: a clear two-roads-at-different-levels-side-by-side photo (none found; searches returned nothing usable), and a clean aerial of a wall end.
All numbers are by eye, scale-free (ratio to the slope's own length or to a lane width, about 3.5 m). Resolution of the Geograph files is low (640 px for 41, 44, 45), so ratios have an error of about +-5 percentage points.

## Crest of a cutting (38, 39, 40, 04)
- 38 (A27 chalk, seen from a bridge): the face is one straight steep plane (about 1:1.2 to 1:1.5); the round-over into the field / trees at the top is about 10-15 % of the face length. 39 (Butser): same, crest rolled over about 10 %, shrubs hide the line. 04: crest hidden by planting, face near vertical with a sharp top.
- Measurement used for the code: the crest rounding of steep cuts is short, about a tenth to a sixth of the slope; of a gentle 1:2 soil cut a quarter (photo 13 on an embankment crest, quarter).
- Chosen: raw round-over is capped at a third of the slope (round_daylight 2 m horizontal, unchanged); smoothing adds at most about 1 m (8 passes, sigma 0.9 m). Measured on the 1:2 cutting golden (6.9 m high, 14 m slope): crest rounding 4 m raw -> 4.5-5 m smoothed = about a third; the same cut was 6 m (43 %) with the earlier 16 passes, which is more than any photo shows, hence 8.

## Toe of a cutting and verge (38, 39, 40, 10, 41, 04, 12)
- Verge: a flat mown or hard strip about half a lane (1-2 m) in 38, 39, 40, 13; wider (about one lane, 3 m) in 10 and 43. The code's verge of 1.5 m is right.
- Toe: in soil and chalk cuts (38, 39, 40) the face meets the verge with a small concave curve, about 5 % of the face length, and NO ditch is visible. A ditch exists only at rock faces: 10 (gneiss) shows a narrow grassed channel about 1 m wide, shallow (about 0.3 m) 1-2 m in front of the rock. So the 0.4 m x 1.5 m toe ditch in the region data matches the rock case; it is kept as a designed feature (smoothing leaves it alone) and could be switched off per cut type later.

## Rock cuts and benches (10, 41, 38, 04)
- 10: one continuous near-vertical face (about 1:0.25), irregular, NO benches, over a height of about two to three lane widths.
- 41 (A629 sandstone, face about 5-6 lane widths high): irregular natural ledges every 3-5 m of height plus a flat apron in front; not machined benches.
- 38: exactly one diagonal bench track on one of the two faces of a 10-12 m chalk cut; the other face has none. So benches are rare and appear only on faces above about 8-10 m; the existing rock berm every 6 m (ROCK_BENCH_H) is more benches than photos 10 and 41 show. Not changed this round (no data for spacing beyond "one or none per 10 m face"); noted as a defaults question for the owner.

## Embankments, toe against a field and crest (13, 42, 43, 44, 45)
- Slope: straight, 1:2.5 to 1:3 (13, 42, 43). Verge: strip about half to one lane wide at the carriageway, then a post-and-rail fence at the toe line in 13, 42, 43, 45.
- Toe: concave run-out, 15-30 % of the bank length (42, 44, 45 where the bank melts into the field; 43 where it meets the fence). No ditch in any of them (a drainage ditch at the field boundary appears only as a faint dark line in 44).
- Crest: broad round-over, about a quarter of the bank (13, 43); in 42 the crest is hidden by the barrier.
- Chosen: rounding at both ends bounded by a third of the slope (raw), smoothing adds about 1 m of the run-out. Measured on the 1:2 embankment golden: toe 4 m raw -> 5 m smoothed (about 35 % of a 12 m slope).

## Wall tops and ends (04, 05, 46, 47, 48)
- 47 (A66 gabions): the top of the wall DOES step down, in one-course steps (about a quarter to a half of a panel) with long flat treads, and a flat strip about 1-2 m wide behind the top before the bank / fence. So a stepped top is correct; a smooth ramp would be wrong for gabions.
- Wall end: 46 stops with a vertical butt end against rubble masonry and the bank; 04 and 05 stop vertically against the rock / bank; 48 (brick) ends in a vertical pier-like face with a ramped coping. None shows a tapered ramp cap, none shows an end standing proud of the bank. Ends are blunt and buried.
- Code: the 4 m taper with 0.25 m steps (<= 0.5 m) already reads like 47; the remaining visible fault is not the wall but the bank around its end (see s7 / s4b renders and STATUS.md). The wall code was NOT changed this round.

## Not covered by any photo
Two roads at different levels side by side (adjacent_levels): no reference. The behaviour is judged only against the invariants and the plain physical rule (a bank has to wrap round the end of a wall).
