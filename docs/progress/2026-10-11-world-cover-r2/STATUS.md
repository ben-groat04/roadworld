# Status (checkpoint)
- Item 1 fields (warped Voronoi, per-block scale/aspect/angle, terrain axis hint): done. Cell-size caveat: uk data said 300x180 m (150x110 is only the C++ default), x/y jitter 0.25 gave the measured 250-400 m.
- Item 2 woodland (ribbons, belts, copses, blocks, plantation cells; CoverGround::land): done.
- Item 3 hedges (3-8 m, gateways, decayed stretches, 70 percent share, trees 30 percent): done.
- Item 4 terrain (rolling detail, tilt, rotated valley, spurs): done, spurs weak (foot wanders 13 m).
- Item 5 per-seed variety: done.
- gcc suite 341/341 pass. MSVC clean build was still running; golden heights not yet confirmed on MSVC.
- Next: check MSVC build and suite, strengthen spurs, straighten-less ribbons, then merge.
