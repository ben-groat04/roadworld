# Engine-agnostic headless sim core. No Unreal headers, no copyleft deps.
add_library(sim_core
  src/command.cpp
  src/clothoid.cpp
  src/clothoid_chain.cpp
  src/vertical_profile.cpp
  src/road_graph.cpp
  src/road_commands.cpp
  src/region_pack.cpp
  src/lanes.cpp
  src/edge_sampling.cpp
  src/heightfield.cpp
  src/earthworks.cpp
  src/grading.cpp
  src/junction.cpp
  src/junction_connectors.cpp
  src/junction_geometry.cpp
  src/junction_merge.cpp
  src/junction_roundabout.cpp
  src/layout_io.cpp
  src/markings.cpp
  src/triangulate.cpp
  src/render_mesh.cpp
  src/street_lights.cpp
  src/structure_mesh.cpp
  src/test_layouts.cpp
  src/vegetation.cpp
  src/world_cache.cpp
  src/ground_pick.cpp
  src/road_index.cpp
  src/stroke.cpp
  src/road_tool.cpp
)
add_subdirectory(${PROJECT_SOURCE_DIR}/third_party/clipper2 ${CMAKE_BINARY_DIR}/clipper2)
target_link_libraries(sim_core PUBLIC clipper2)
target_include_directories(sim_core PUBLIC include)
# Vendored MIT header; SYSTEM keeps its warnings out of our -Wconversion build.
target_include_directories(sim_core SYSTEM PRIVATE ${PROJECT_SOURCE_DIR}/third_party)
if(MSVC)
  target_compile_options(sim_core PRIVATE /W4 /permissive-
    # Third-party headers (Clipper2 C4702, nlohmann) are SYSTEM includes; see root CMakeLists.
    /external:W0)
  # C4702 from Clipper2 templates is a back-end warning that /external:W0 cannot suppress; limit the
  # silencing to the sources that instantiate Clipper2 (not the rest of sim_core).
  set_source_files_properties(src/junction_geometry.cpp src/junction_merge.cpp src/junction_roundabout.cpp PROPERTIES COMPILE_OPTIONS /wd4702)
else()
  # Exceptions stay off so the library can be linked into Unreal modules (CI-enforced:
  # any throw/try/catch in sim_core fails to compile). Callers still see them if enabled.
  target_compile_options(sim_core PRIVATE -Wall -Wextra -Wpedantic -Wconversion -fno-exceptions)
endif()

# Developer tools (export_scene, fixtures); not part of the library.
add_subdirectory(tools)
