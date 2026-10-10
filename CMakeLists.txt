cmake_minimum_required(VERSION 3.20)
project(CityPainter LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(CP_BUILD_TESTS "Build sim_core tests" ON)

add_subdirectory(sim_core)

if(CP_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()
