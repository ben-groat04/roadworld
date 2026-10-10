# Engine-agnostic headless sim core. No Unreal headers, no copyleft deps.
add_library(sim_core
  src/command.cpp
)
target_include_directories(sim_core PUBLIC include)
if(MSVC)
  target_compile_options(sim_core PRIVATE /W4 /permissive-)
else()
  target_compile_options(sim_core PRIVATE -Wall -Wextra -Wpedantic -Wconversion)
endif()
