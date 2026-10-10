cmake_minimum_required(VERSION 3.20)
project(CityPainter LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# MSVC: make SYSTEM include dirs (Clipper2, doctest, nlohmann) external so /external:W0 silences their warnings.
# The Visual Studio generators do not map SYSTEM to /external:I by themselves.
if(MSVC)
  set(CMAKE_INCLUDE_SYSTEM_FLAG_CXX "/external:I ")
endif()

option(CP_BUILD_TESTS "Build sim_core tests" ON)

add_subdirectory(sim_core)

if(CP_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()
