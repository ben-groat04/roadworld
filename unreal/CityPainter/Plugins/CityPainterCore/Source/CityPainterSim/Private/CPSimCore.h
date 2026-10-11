#pragma once

// The one include point of the sim_core headers the facade needs, wrapped in the same warning relaxation the legacy
// build file uses (sim_core headers pull <filesystem> and templates the module is built with exceptions off for).
// Only files of the Sim module include this.

#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4244 4267 4100 4189 4456 4457 4458 4459 4668 4996 5038 4127 4702 4530 4577)
#endif
#include "cp/command.h"
#include "cp/ground_pick.h"
#include "cp/heightfield.h"
#include "cp/markings.h"
#include "cp/region_pack.h"
#include "cp/render_mesh.h"
#include "cp/road_commands.h"
#include "cp/road_graph.h"
#include "cp/road_tool.h"
#include "cp/test_world.h"
#include "cp/vegetation.h"
#include "cp/world_cache.h"
#if defined(_MSC_VER)
#pragma warning(pop)
#endif
