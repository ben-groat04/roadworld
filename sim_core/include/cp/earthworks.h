#pragma once
// Roads own their earthwork geometry (docs/planning-log.md, "Elevation and terrain"):
// cutting/embankment slopes and retaining walls are meshes generated from per-station
// cross-sections of the road. The terrain heightfield is never deformed; it is hidden
// under the road corridor (corridor_footprint) and the meshes dip under natural ground
// past the daylight line so the seam is never visible.
#include <cstdint>
#include <vector>

#include "cp/heightfield.h"
#include "cp/road_graph.h"
#include "cp/vec.h"

namespace cp {

struct EarthworkParams {
    double batter = 2.0;            // horizontal : vertical of cutting/embankment slopes
    double wall_start = 6.0;        // slope height above which a retaining wall replaces it
    double wall_cap = 12.0;         // height above which the road needs a bridge/tunnel
    double wall_thickness = 0.4;    // m
    double formation_drop = 0.15;   // m, formation level below the road surface
};

enum class Structure { Ground, Bridge, Tunnel };

struct SideSection {
    enum Kind { None, Slope, Wall } kind = None;
    bool cut = false;   // true: natural ground above formation (cutting); false: embankment
    double edge = 0;    // lateral offset of the road's outer edge (always >= 0, outward)
    double z0 = 0;      // formation z at the edge
    double d1 = 0;      // lateral offset of the daylight point (Wall: outer face, edge + thickness)
    double z1 = 0;      // z where the earthwork meets natural ground
    double batter = 2.0;  // h:v used, kept so meshing can extend the slope past daylight
};

struct CrossSection {
    double s = 0;           // centreline arc length
    Vec3 centre;            // road centreline point (road surface z)
    double heading = 0;     // rad CCW from +x; left = heading + 90 degrees
    Structure structure = Structure::Ground;
    SideSection left, right;
};

// Stations every ~station_step along the edge (spacing is adjusted so both ends are
// included). Daylight is found by marching out at 1:batter then bisecting to 1 cm. A side
// whose slope would exceed wall_cap makes the station a Bridge (fill) or Tunnel (cut; Bridge
// wins if the two sides disagree); its sections are then kind None. Structure flags are
// smoothed: gaps < 20 m between equal flags are filled, then runs < 10 m are dropped.
std::vector<CrossSection> compute_cross_sections(const Edge& e, double road_half_width,
                                                 const Heightfield& terrain,
                                                 const EarthworkParams& params,
                                                 double station_step);

// Indexed triangle mesh. Winding is counter-clockwise when viewed from above (z up), i.e.
// the front face is the one facing up/outward. Normals are smooth (area weighted).
struct MeshData {
    std::vector<Vec3> positions;
    std::vector<Vec3> normals;
    std::vector<std::uint32_t> indices;
};

// One strip per side. Each station contributes kRows vertices per side, running from the
// road edge to the daylight point and then `overlap` metres further so the mesh ends under
// natural ground. Fill: continues at the same slope (or straight down for a wall). Cut:
// the slope would rise out of the ground, so it turns back down at the same gradient.
// Quads between stations are skipped where either station is a Bridge/Tunnel or has no
// earthwork, and zero-area triangles are never emitted. Vertex layout:
// ((side * stations + i) * kRows + row), side 0 = left, 1 = right.
inline constexpr int kEarthworkRows = 5;
MeshData build_slope_mesh(const std::vector<CrossSection>& sections, double overlap = 1.0);

// Plan-view polygons (one per run of consecutive Ground stations with >= 2 stations) to
// cut out of the terrain. Vertices: left side forward, then right side backward (clockwise
// seen from above); the polygon is implicitly closed (first point is not repeated). Each
// side sits at max(edge, d1 - inset); a Wall uses its outer face (d1) with no inset.
std::vector<std::vector<Vec2>> corridor_footprint(const std::vector<CrossSection>& sections,
                                                  double inset = 1.0);

}  // namespace cp
