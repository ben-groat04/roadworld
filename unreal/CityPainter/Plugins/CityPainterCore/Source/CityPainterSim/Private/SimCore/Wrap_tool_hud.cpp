// Compiles sim_core/src/tool_hud.cpp inside this module. See CityPainterSim.Build.cs.
// Uses nlohmann/json, which compiles throw-free when exceptions are disabled (module has them off).
#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4244 4267 4100 4189 4456 4457 4458 4459 4668 4996 5038 4127 4702 4530 4577)
#endif
#include "../../../../../../../../sim_core/src/tool_hud.cpp"
#if defined(_MSC_VER)
#pragma warning(pop)
#endif
