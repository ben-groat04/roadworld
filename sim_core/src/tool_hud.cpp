// No exceptions: sim_core builds with -fno-exceptions.
// Port of renderHud / renderProfile (text and axes) / the help table of prototypes/road-draw/src (ui.js, shell.html, inspect.js).
#include "cp/tool_hud.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>

namespace cp {

namespace {

// UTF-8 glyphs of the prototype (kept as byte escapes: source encodings differ between compilers).
constexpr const char* kDot = " \xC2\xB7 ";           // " . " middle dot
constexpr const char* kDeg = "\xC2\xB0";              // degree sign
constexpr const char* kInfinity = "\xE2\x88\x9E";     // infinity
constexpr const char* kCheck = "\xE2\x9C\x93";        // check mark
constexpr const char* kCross = "\xE2\x9C\x95";        // multiplication X (error)
constexpr const char* kWarn = "\xE2\x9A\xA0";         // warning sign

std::string f1(double v) {
    char b[48];
    std::snprintf(b, sizeof b, "%.1f", v);
    return b;
}
std::string f0(double v) {
    char b[48];
    std::snprintf(b, sizeof b, "%.0f", v);
    return b;
}
// JS `${n}`: the number as it prints, no trailing zeros.
std::string plain(double v) {
    char b[48];
    std::snprintf(b, sizeof b, "%.4f", v);
    std::string s = b;
    while (!s.empty() && s.back() == '0') s.pop_back();
    if (!s.empty() && s.back() == '.') s.pop_back();
    if (s == "-0") s = "0";
    return s;
}
std::string radius_text(double r) { return std::isinf(r) ? kInfinity : f0(r); }

HudRun run(std::string t, bool bold = false, HudTone tone = HudTone::Normal) { return HudRun{std::move(t), bold, tone}; }

HudLine plain_line(std::string t, HudTone tone = HudTone::Normal) {
    HudLine l;
    l.tone = tone;
    l.runs.push_back(run(std::move(t), false, tone));
    return l;
}

HudLine header_line(bool edit, const std::string& type_name, bool one_way, bool dual, const HudContext& ctx) {
    HudLine l;
    l.runs.push_back(run(edit ? "EDIT" : "DRAW", true));
    l.runs.push_back(run(std::string(kDot) + type_name));
    if (one_way) {
        if (dual) {
            l.runs.push_back(run(kDot));
            l.runs.push_back(run("single carriageway", true));
        } else {
            l.runs.push_back(run(std::string(kDot) + "one-way"));
        }
    }
    l.runs.push_back(run(std::string(kDot) + "colour: " + colour_mode_name(ctx.colour)));
    if (ctx.snap_off) {
        l.runs.push_back(run(kDot));
        l.runs.push_back(run("snap off", false, HudTone::Warning));
    }
    return l;
}

void add_issues(std::vector<HudLine>& out, const std::vector<Issue>& errors, const std::vector<Issue>& advisories) {
    for (const Issue& e : errors) out.push_back(plain_line(std::string(kCross) + " " + e.text, HudTone::Error));
    for (const Issue& w : advisories) out.push_back(plain_line(std::string(kWarn) + " " + w.text, HudTone::Warning));
}

}  // namespace

std::string HudLine::text() const {
    std::string s;
    for (const HudRun& r : runs) s += r.text;
    return s;
}

const char* snap_kind_word(SnapKind k) {
    switch (k) {
        case SnapKind::Free: return "free";
        case SnapKind::Angle: return "angle";
        case SnapKind::Guide: return "guide";
        case SnapKind::End: return "end";
        case SnapKind::Node: return "node";
        case SnapKind::Road: return "road";
    }
    return "free";
}

std::vector<HudLine> draw_hud(const DrawPreview& pv, const HudContext& ctx) {
    std::vector<HudLine> out;
    out.push_back(header_line(false, pv.road_type_name, pv.one_way, pv.dual_type, ctx));
    if (pv.has_pick) {
        std::string t = "cursor z " + f1(pv.cursor.z) + " m";
        if (std::abs(ctx.height_offset) > 1e-9) t += std::string(" (") + (ctx.height_offset > 0 ? "+" : "") + plain(ctx.height_offset) + " m)";
        t += std::string(kDot) + "snap: " + snap_kind_word(pv.snap);
        out.push_back(plain_line(t));
    }
    if (!pv.ribbons.empty()) {  // a fitted stroke exists (a segment under the minimum span has none: no readouts)
        const Readouts& r = pv.readouts;
        out.push_back(plain_line("span " + f1(r.span_length) + " m" + kDot + "bearing " + f0(r.heading_deg) + kDeg + kDot + "R at cursor " +
                                 radius_text(r.end_radius) + " m" + kDot + "min R " + radius_text(r.min_radius) + " m"));
        out.push_back(plain_line("grade " + f1(r.grade_pct) + "%" + kDot + "max " + f1(r.max_grade_pct) + "%" + kDot +
                                 (r.g2 ? std::string("G2 ") + kCheck : std::string("G1")) + kDot + "total " + f0(r.length) + " m"));
    }
    add_issues(out, pv.errors, pv.advisories);
    return out;
}

std::vector<HudLine> edit_hud(const EditPreview& pv, const HudContext& ctx, const std::string& tool_type_name) {
    std::vector<HudLine> out;
    const EditSelection& s = pv.selection;
    if (s.valid) out.push_back(header_line(true, s.road_type_name.empty() ? tool_type_name : s.road_type_name, s.one_way, s.dual_type, ctx));
    else out.push_back(header_line(true, tool_type_name, false, false, ctx));
    // A drag in progress (or a refused one): the prototype printed the red error of a refused edit; advisories likewise.
    if (pv.dragging) add_issues(out, pv.errors, pv.advisories);
    if (s.valid)
        out.push_back(plain_line("selected road #" + std::to_string(s.number) + kDot + f0(s.length) + " m" + kDot +
                                 "drag handles, PgUp/PgDn height, Del delete"));
    return out;
}

const UiTheme& ui_theme() {
    static const UiTheme t;
    return t;
}

RingStyle ring_style(SnapKind k) {
    const UiTheme& t = ui_theme();
    switch (k) {
        case SnapKind::End: return t.ring_end;
        case SnapKind::Node: return t.ring_node;
        case SnapKind::Road: return t.ring_road;
        case SnapKind::Guide: return t.ring_guide;
        case SnapKind::Angle: return t.ring_angle;
        case SnapKind::Free: return t.ring_free;
    }
    return t.ring_free;
}

std::uint32_t ghost_colour(const DrawPreview& pv) { return pv.errors.empty() ? ui_theme().ghost : ui_theme().ghost_error; }

const char* profile_empty_text() { return "Profile: draw a road, or select one in edit mode (Tab). P hides this panel."; }

std::string profile_title(const ProfileSeries& p) {
    return "Profile" + std::string(kDot) + f0(p.length) + " m" + kDot + "vs original ground" + kDot + "blue = fill, orange = cut, red = over " +
           plain(p.wall_cap) + " m wall cap (bridge/tunnel)" + kDot + "yellow = vertical curves";
}

ProfileBand profile_band(double mean_depth, double wall_cap) {
    if (std::abs(mean_depth) > wall_cap) return ProfileBand::OverCap;
    return mean_depth > 0 ? ProfileBand::Fill : ProfileBand::Cut;
}

ProfileAxes profile_axes(const ProfileSeries& p) {
    ProfileAxes a;
    if (!p.valid) return a;
    double lo = p.z_min, hi = p.z_max;
    const double pad = std::max(5.0, (hi - lo) * 0.15);
    lo -= pad;
    hi += pad;
    a.z_lo = lo;
    a.z_hi = hi;
    a.z_step = 50;
    for (const double v : {1.0, 2.0, 5.0, 10.0, 20.0, 50.0})
        if ((hi - lo) / v < 8) {
            a.z_step = v;
            break;
        }
    for (double z = std::ceil(lo / a.z_step) * a.z_step; z < hi; z += a.z_step) a.z_ticks.push_back(z);
    a.x_step = 1000;
    for (const double v : {10.0, 20.0, 50.0, 100.0, 200.0, 500.0, 1000.0})
        if (p.length / v < 10) {
            a.x_step = v;
            break;
        }
    for (double s = 0; s <= p.length; s += a.x_step) a.x_ticks.push_back(s);
    return a;
}

std::string profile_z_label(double z) { return plain(z) + " m"; }
std::string profile_x_label(double s) { return plain(s); }

std::vector<HelpRow> help_rows(const std::vector<std::string>& names) {
    std::string types;
    for (std::size_t i = 0; i < names.size(); ++i) types += (i ? kDot : "") + names[i];
    std::vector<HelpRow> r;
    r.push_back({"Left click", "Add a point (the clothoid refits through all points). Clicking onto an existing road connects and finishes. Click onto a bridge deck to start from it."});
    r.push_back({"Double-click / Enter / right-click (no drag)", "Finish road"});
    r.push_back({"Esc / Backspace", "Cancel road / remove last point"});
    r.push_back({"Hold Shift", "Snapping off (angle, length, guidelines, roads)"});
    r.push_back({"PgUp / PgDn  or  ] / [", "Raise / lower the next point 1 m (Shift: 5 m). In edit mode: the hovered handle."});
    r.push_back({"1 2 3 4", types + " (dual types: one stroke places two one-way carriageways)"});
    r.push_back({"O", "Toggle one-way (single-carriageway types)"});
    r.push_back({"C", "Road colour: plain / curvature / gradient"});
    r.push_back({"Tab", "Toggle draw / edit. Edit: click a road, drag its handles (terrain regrades on release), Del deletes it."});
    r.push_back({"Ctrl+Z / Ctrl+Y", "Undo / redo"});
    r.push_back({"Right drag / middle drag / wheel", "Orbit / pan / zoom"});
    r.push_back({std::string("W A S D or arrows") + kDot + "Q E", std::string("Pan along the ground") + kDot + "rotate"});
    r.push_back({"P", "Toggle the profile panel"});
    r.push_back({"H", "Toggle this help"});
    return r;
}

const char* help_title() { return "Road Draw 3D prototype"; }

const char* help_paragraph() {
    return "The terrain is graded to follow each road: cuttings and embankments at the batter slope, retaining walls where cut/fill exceeds "
           "\"wall start\", bridges (fill) and tunnels (cut) beyond \"wall cap\". Curvature and gradient are advisory (amber). Red means the road "
           "would break graphically and the click is refused. Profile: blue fill, orange cut, red = over the wall cap, measured against the "
           "original ground. Click to close.";
}

const char* inspect_idle_text() { return "or drop a file here \xC2\xB7 J = connectors"; }

std::string inspect_status_text(const std::string& file, int roads, int junctions, int bridge_samples, int tunnel_samples, int connectors,
                                int disabled, bool connectors_on) {
    return file + ": " + std::to_string(roads) + " roads, " + std::to_string(junctions) + " junctions, " + std::to_string(bridge_samples) +
           " bridge / " + std::to_string(tunnel_samples) + " tunnel samples, " + std::to_string(connectors) + " connectors (" +
           std::to_string(disabled) + " disabled)" + kDot + "J connectors " + (connectors_on ? "on" : "off");
}

}  // namespace cp
