add_executable(sim_core_tests
  main.cpp
  test_ids.cpp
  test_command.cpp
  test_clothoid.cpp
  test_clothoid_chain.cpp
  test_vertical_profile.cpp
  test_road_graph.cpp
  test_region_pack.cpp
  test_lanes.cpp
  test_edge_sampling.cpp
  test_heightfield.cpp
  test_earthworks.cpp
  test_grading.cpp
  test_junction_interface.cpp
  test_junction_geometry.cpp
  test_junction_connectors.cpp
  test_junction_connectors_fuzz.cpp
  test_junction_merge.cpp
  test_junction_roundabout.cpp
  test_junction_round3.cpp
  test_layout_io.cpp
  test_markings.cpp
  test_export_scene.cpp
  test_render_mesh.cpp
  test_edit_loop.cpp
  test_street_lights.cpp
  test_structure_mesh.cpp
  test_test_layouts.cpp
)
target_link_libraries(sim_core_tests PRIVATE sim_core scene_tools)
target_include_directories(sim_core_tests SYSTEM PRIVATE ${PROJECT_SOURCE_DIR}/third_party)
add_test(NAME sim_core_tests COMMAND sim_core_tests)
target_compile_definitions(sim_core_tests PRIVATE CP_DATA_DIR="${PROJECT_SOURCE_DIR}/data")
target_compile_definitions(sim_core_tests PRIVATE CP_TEST_DATA_DIR="${PROJECT_SOURCE_DIR}/tests/data")
if(MSVC)
  # doctest C5285 (specializes std::tuple) is third-party: it is a SYSTEM include, so external warnings off.
  target_compile_options(sim_core_tests PRIVATE /external:W0)
endif()
