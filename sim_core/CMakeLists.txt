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
  src/junction.cpp
  src/junction_connectors.cpp
  src/junction_geometry.cpp
  src/junction_merge.cpp
)
add_subdirectory(${PROJECT_SOURCE_DIR}/third_party/clipper2 ${CMAKE_BINARY_DIR}/clipper2)
target_link_libraries(sim_core PUBLIC clipper2)
target_include_directories(sim_core PUBLIC include)
# Vendored MIT header; SYSTEM keeps its warnings out of our -Wconversion build.
target_include_directories(sim_core SYSTEM PRIVATE ${PROJECT_SOURCE_DIR}/third_party)
if(MSVC)
  target_compile_options(sim_core PRIVATE /W4 /permissive-)
else()
  # Exceptions stay off so the library can be linked into Unreal modules (CI-enforced:
  # any throw/try/catch in sim_core fails to compile). Callers still see them if enabled.
  target_compile_options(sim_core PRIVATE -Wall -Wextra -Wpedantic -Wconversion -fno-exceptions)
endif()
