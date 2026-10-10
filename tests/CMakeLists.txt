add_executable(sim_core_tests
  main.cpp
  test_ids.cpp
  test_command.cpp
  test_clothoid.cpp
  test_clothoid_chain.cpp
)
target_link_libraries(sim_core_tests PRIVATE sim_core)
target_include_directories(sim_core_tests PRIVATE ${PROJECT_SOURCE_DIR}/third_party)
add_test(NAME sim_core_tests COMMAND sim_core_tests)
