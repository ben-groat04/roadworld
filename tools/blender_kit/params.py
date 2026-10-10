"""All tunable dimensions (metres) for the kit. Override any key with --params file.json (deep merge).

Convention for EVERY piece (Blender == FBX local frame):
  +X = along the road/run, +Z = up, +Y = toward the carriageway/traffic side, origin at ground (or bed) level.
"""
PARAMS = {
    "seed": 20260101,
    "uv_metres_per_unit": 1.0,          # UV 1.0 == 1 m in every direction (box/cylindrical projection in metres)
    "kerb": {
        "length": 0.915, "joint": 0.005,
        "hb_w": 0.125, "hb_h": 0.255, "hb_batter_h": 0.150, "hb_batter": 0.025,   # HB2 half-battered
        "bn_w": 0.150, "bn_h": 0.255, "bn_r": 0.075,                               # bullnosed
        "drop_h": 0.155, "dropper_h": 0.140,                                        # transition low end / flush dropper
        "edge_w": 0.050, "edge_h": 0.150,                                           # edging kerb 50 x 150
        "bevel": 0.004, "bevel_segs": 2, "radii": [3.0, 10.0], "nose_radius": 0.30,
        "variant_chip_mm": 7, "variant_jitter_mm": 1.5,
        "buried": 0.130,                                                            # bed depth under road surface
    },
    "cover": {
        "gully_outer": 0.450, "gully_inner": 0.360, "gully_depth": 0.100, "gully_bar": 0.016, "gully_gap": 0.016,
        "mh_frame_d": 0.750, "mh_cover_d": 0.600, "mh_depth": 0.100, "rib_h": 0.005, "rib_w": 0.014, "rib_pitch": 0.035,
        "rect_frame": [0.750, 0.600], "rect_cover": [0.600, 0.450],
        "inlet_len": 0.915, "inlet_h": 0.310, "inlet_w": 0.150, "inlet_open_w": 0.450, "inlet_open_z": [0.120, 0.230],
    },
    "light": {
        "heights": [6, 8, 10, 12], "outreaches": [0.6, 1.0, 1.5, 1.8],
        "base_d": {"6": 0.140, "8": 0.168, "10": 0.178, "12": 0.193},
        "top_d": {"6": 0.076, "8": 0.089, "10": 0.089, "12": 0.102},
        "door_h": 0.55, "door_w": 0.105, "door_z": 0.45, "flange_d": 0.26, "flange_h": 0.025, "sides": 16,
        "arm_d": 0.060, "arm_rise_deg": 8.0, "arm_overlap": 0.45,
        "lantern": [0.70, 0.30, 0.11],
        "pm_plate": [0.30, 0.30, 0.02],
        # data/regions/uk/road_types.json street_lights.style -> [mounting_height, outreach, twin]
        "styles": {
            "residential_column_6m": [6, 0.6, False], "column_8m": [8, 1.0, False],
            "column_10m": [10, 1.5, False], "column_12m_twin": [12, 1.8, True],
        },
    },
    "bollard": {
        "steel_d": 0.114, "steel_h": 1.0, "keepleft": [0.32, 0.18, 0.75], "timber": [0.15, 0.15, 1.0],
    },
    "barrier": {
        "ped_len": 2.0, "ped_h": 1.1, "ped_post_d": 0.076, "ped_rail_d": 0.048,
        "wbeam_len": 4.0, "wbeam_post_spacing": 2.0, "wbeam_top": 0.75, "wbeam_t": 0.004,
        "step_len": 3.0, "step_h": 0.80,
        "parapet_bay": 2.0, "parapet_h": 1.0, "cope_len": 1.0, "cope_w": 0.40, "cope_t": 0.12,
    },
    "masonry": {"retain_cope": [0.50, 0.40, 0.15], "head_cope": [1.00, 0.45, 0.20], "tactile": 0.400},
}
