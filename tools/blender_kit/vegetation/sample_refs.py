"""Sample mean sRGB of patches in reference photos (read only; nothing is copied). Run inside Blender headless."""
import bpy, os, sys, json
REF = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", "..", "docs", "reference", "street"))
# (file, displayed->original scale, label, (x0,y0,x1,y1) in displayed px)
P = [
 ("35_rural-lane-hedges-sun-cloud.jpg", 1.73, "hedge_trimmed_left_lit", (100, 850, 450, 1000)),
 ("35_rural-lane-hedges-sun-cloud.jpg", 1.73, "hedge_overgrown_right_dark", (1400, 850, 1850, 1000)),
 ("35_rural-lane-hedges-sun-cloud.jpg", 1.73, "verge_grass_mown", (100, 1200, 600, 1400)),
 ("36_rural-b-road-low-sun-winter.jpg", 1.89, "hedge_winter_trimmed", (800, 1010, 1800, 1050)),
 ("36_rural-b-road-low-sun-winter.jpg", 1.89, "rough_grass_bank_winter", (30, 1050, 400, 1250)),
 ("36_rural-b-road-low-sun-winter.jpg", 1.89, "verge_strip_hedge_foot", (1000, 1075, 1400, 1110)),
 ("31_dual-central-reservation-grass-barrier.jpg", 1.15, "tree_belt_canopy_dark", (50, 100, 300, 500)),
 ("31_dual-central-reservation-grass-barrier.jpg", 1.15, "tree_belt_canopy_right", (1500, 200, 1800, 500)),
 ("31_dual-central-reservation-grass-barrier.jpg", 1.15, "rough_grass_centre_res", (750, 1100, 1150, 1450)),
 ("28_junction-roundabout-splitter-island-elevated.jpg", 1.15, "island_mown_grass", (600, 750, 900, 950)),
 ("28_junction-roundabout-splitter-island-elevated.jpg", 1.15, "bank_mown_grass_right", (1550, 700, 1950, 800)),
 ("28_junction-roundabout-splitter-island-elevated.jpg", 1.15, "small_tree_foliage", (700, 650, 800, 780)),
]
def lin2srgb(c):
    return 12.92*c if c <= 0.0031308 else 1.055*(c**(1/2.4))-0.055
out = {}
cache = {}
for f, s, lab, (x0,y0,x1,y1) in P:
    if f not in cache:
        img = bpy.data.images.load(os.path.join(REF, f)); img.colorspace_settings.name = 'sRGB'
        cache[f] = (img, list(img.pixels))   # float, stored as display (sRGB) values for byte images
    img, px = cache[f]; W, H = img.size[0], img.size[1]
    X0,X1 = int(x0*s), int(x1*s); Y0,Y1 = int(y0*s), int(y1*s)
    acc=[0,0,0]; n=0
    for y in range(Y0, Y1, 4):
        yy = H-1-y
        for x in range(X0, X1, 4):
            i = (yy*W+x)*4
            for k in range(3): acc[k]+=px[i+k]
            n+=1
    m=[a/n for a in acc]
    out[lab]=[round(v*255) for v in m]
    print(f[:2], lab, out[lab], "linear" if False else "")
json.dump(out, open(os.path.join(os.path.dirname(__file__), "ref_colours.json"), "w"), indent=1)
