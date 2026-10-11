#pragma once
// The words and numbers of the prototype's screen furniture, produced once here so every host prints the same
// (JS ui.js renderHud / renderProfile / TUNE, shell.html help). A host draws these lines in its own widgets; it
// adds no rule of its own to them. Tests pin each string against the prototype screenshots
// (docs/progress/2026-10-11-prototype-ui/NN_*.png), see tests/test_tool_hud.cpp.
//
// Text is UTF-8: the middle dot " \xC2\xB7 ", the degree sign, the infinity sign, the check mark, the cross and
// the warning sign are the prototype's own glyphs.
#include <cstdint>
#include <string>
#include <vector>

#include "cp/edit_tool.h"
#include "cp/road_heat.h"
#include "cp/road_tool.h"

namespace cp {

enum class HudTone { Normal, Error, Warning, Muted };

// A stretch of one line in one style: the header's "DRAW" is bold, "snap off" and "single carriageway" stand out.
struct HudRun {
    std::string text;
    bool bold = false;
    HudTone tone = HudTone::Normal;
};

struct HudLine {
    std::vector<HudRun> runs;
    HudTone tone = HudTone::Normal;  // the whole line's tone (Error lines are red, Warning lines amber)
    std::string text() const;        // the runs joined
};

// What the host holds that the previews do not: the colour mode (C), Shift, PgUp / PgDn offset.
struct HudContext {
    ColourMode colour = ColourMode::Plain;
    bool snap_off = false;       // Shift held
    double height_offset = 0;    // DrawSession::height_offset()
};

// Draw mode. Line 1: "DRAW . <type> [. one-way | . single carriageway] . colour: <mode> [. snap off]"; then, with a
// pick: "cursor z 87.3 m [(+3 m)] . snap: <kind>"; with a fitted stroke: "span ... bearing ... R at cursor ... min R ..."
// and "grade ... max ... G2 ... total ..."; then the errors ("X text") and advisories ("! text").
std::vector<HudLine> draw_hud(const DrawPreview& pv, const HudContext& ctx);

// Edit mode. Line 1 as above with "EDIT" (the selected road's type when one is selected, else `tool_type_name`);
// with a selection "selected road #N . L m . drag handles, PgUp/PgDn height, Del delete"; errors and advisories of
// a drag in progress.
std::vector<HudLine> edit_hud(const EditPreview& pv, const HudContext& ctx, const std::string& tool_type_name);

// One red / amber line for something a host holds outside a preview: the reason of a refused finish, click or edit
// ("X text" / "! text"), so it prints exactly like the lines the previews produce.
HudLine error_hud_line(const std::string& text);
HudLine warning_hud_line(const std::string& text);

// The word the HUD prints for a snap kind: free, angle, guide, end, road (and node for a junction node).
const char* snap_kind_word(SnapKind k);

// ---- profile panel text and axes ----
// "Profile: draw a road, or select one in edit mode (Tab). P hides this panel."
const char* profile_empty_text();
// "Profile . 224 m . vs original ground . blue = fill, orange = cut, red = over 12 m wall cap (bridge/tunnel) . yellow = vertical curves"
std::string profile_title(const ProfileSeries& p);

// How one stretch between two stations is painted: dz = mean (road - ground) over it.
enum class ProfileBand { Cut, Fill, OverCap };
ProfileBand profile_band(double mean_depth, double wall_cap);

struct ProfileAxes {
    double x_step = 0;            // m: first of 10, 20, 50, 100, 200, 500, 1000 giving fewer than 10 labels
    std::vector<double> x_ticks;  // 0, step, 2 step ... <= length (labels are plain numbers)
    double z_lo = 0, z_hi = 0;    // m, padded by max(5 m, 15% of the range) either side of the road and ground
    double z_step = 0;            // m: first of 1, 2, 5, 10, 20, 50 giving fewer than 8 grid lines
    std::vector<double> z_ticks;  // grid lines and their "N m" labels, strictly inside (z_lo, z_hi)
};
ProfileAxes profile_axes(const ProfileSeries& p);

// "60 m"-style label of a grid line and the plain number of an x tick.
std::string profile_z_label(double z);
std::string profile_x_label(double s);

// ---- colours and sizes of the overlay primitives (the prototype's own look, one table for every host) ----
// 0xRRGGBB, sizes in screen pixels, opacities 0..1. These are interface colours, not region data; the heat-map road
// colours are region data (RoadToolRules::heat).
struct RingStyle {
    std::uint32_t colour = 0xffffff;
    double radius_px = 11;
};
struct UiTheme {
    RingStyle ring_end{0x3fb950, 11}, ring_node{0x3fb950, 11}, ring_road{0x3fb950, 11}, ring_guide{0x7ee0ff, 11}, ring_angle{0xffffff, 11},
        ring_free{0xffffff, 7};
    std::uint32_t ghost = 0xdde6f0, ghost_error = 0xe5484d;  // the ghost ribbon, and while the stroke has an error
    double ghost_opacity = 0.55;
    std::uint32_t guide_line = 0x7ee0ff;                      // extension and parallel-offset guides (dashed, opaque)
    std::uint32_t guide_faint = 0xffffff;                     // angle / length grid line (dashed, half opaque)
    double guide_faint_opacity = 0.5;
    double guide_dash = 4, guide_gap = 3;                     // m
    std::uint32_t point_dot = 0xffd866;                       // placed points
    double point_dot_px = 4;
    std::uint32_t edit_hover_line = 0xffe9a8, edit_selected_line = 0xffd866;
    std::uint32_t handle = 0xffffff, handle_hover = 0xffd866;
    double handle_px = 7, handle_hover_px = 10;
    // lane connector colours of the inspect view (turn kinds)
    std::uint32_t turn_left = 0x4aa3ff, turn_right = 0xff9f43, turn_straight = 0x3fb950, turn_uturn = 0xc678dd, turn_merge = 0xe5484d,
                  turn_diverge = 0x39d0d0, turn_other = 0xffd866, turn_disabled = 0x8a8f98;
    // HUD colours
    std::uint32_t text = 0xe6edf3, muted = 0x8b949e, accent = 0xffd866, error = 0xff7b72, warning = 0xf5a623;
};
const UiTheme& ui_theme();
// The ring the cursor shows for a snap kind (JS buildOverlay): green end / node / road, cyan guide, white angle and free.
RingStyle ring_style(SnapKind k);
// The ghost's colour for a stroke (red when the preview carries an error; advisories do not tint it).
std::uint32_t ghost_colour(const DrawPreview& pv);

// ---- help overlay ----
struct HelpRow {
    std::string keys;
    std::string meaning;
};
// The 14 rows of the prototype's help table, in order. `road_type_names` are the names of the types keys 1 2 3 4
// select, in key order (the prototype listed "Residential", "B road", "Dual carriageway", "Motorway"): the row is
// built from them so the help never names a type the region does not have.
std::vector<HelpRow> help_rows(const std::vector<std::string>& road_type_names);
const char* help_title();       // "Road Draw 3D prototype" (a host may rename it)
// The closing paragraph (the grading wording refers to the tuning panel's "wall start" / "wall cap").
const char* help_paragraph();

// ---- tuning panel text ----
// "Snap angle \xC2\xB0 15"-style row text is label + value; see tool_tuning.h tuning_value_text.

// ---- inspect bar ----
// "or drop a file here . J = connectors" before a scene is loaded.
const char* inspect_idle_text();
// "interchange.json: 19 roads, 12 junctions, 11 bridge / 0 tunnel samples, 54 connectors (0 disabled) . J connectors on"
std::string inspect_status_text(const std::string& file, int roads, int junctions, int bridge_samples, int tunnel_samples, int connectors,
                                int disabled, bool connectors_on);

}  // namespace cp
