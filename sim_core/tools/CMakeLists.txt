# Developer tooling on top of sim_core (not part of the sim_core library, no Unreal wrappers).
add_library(scene_tools STATIC scene_fixture.cpp scene_roundabout.cpp scene_json.cpp)
target_link_libraries(scene_tools PUBLIC sim_core)
target_include_directories(scene_tools PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
add_executable(export_scene export_scene.cpp)
target_link_libraries(export_scene PRIVATE scene_tools)
target_compile_definitions(export_scene PRIVATE CP_DATA_DIR="${PROJECT_SOURCE_DIR}/data")

# Regenerate the prototype fixture:  cmake --build build --target export_fixtures
set(CP_FIXTURE_DIR ${PROJECT_SOURCE_DIR}/prototypes/road-draw/fixtures)
add_custom_target(export_fixtures
  COMMAND ${CMAKE_COMMAND} -E make_directory ${CP_FIXTURE_DIR}
  COMMAND export_scene --out ${CP_FIXTURE_DIR}/interchange.json interchange
  VERBATIM)

# Writes render meshes as .obj per surface class for inspection:  export_mesh --out <dir> interchange
add_executable(export_mesh export_mesh.cpp)
target_link_libraries(export_mesh PRIVATE scene_tools)
target_compile_definitions(export_mesh PRIVATE CP_DATA_DIR="${PROJECT_SOURCE_DIR}/data")
