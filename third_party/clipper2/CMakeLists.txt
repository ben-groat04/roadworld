# Vendored Clipper2 1.5.4 (Boost Software License 1.0, see LICENSE.txt), C++ sources only.
# Upstream: https://github.com/AngusJohnson/Clipper2 (tag Clipper2_1.5.4). Unmodified.
# Builds with -fno-exceptions: Clipper2's DoError() compiles to a no-op when exceptions are
# off, so failures yield empty/partial paths instead of throwing. Callers must treat an empty
# result as "failed". Int64 paths only (no USINGZ); use ClipperD / PathsD for doubles.
add_library(clipper2 STATIC
  src/clipper.engine.cpp
  src/clipper.offset.cpp
  src/clipper.rectclip.cpp
)
target_include_directories(clipper2 SYSTEM PUBLIC include)
target_compile_features(clipper2 PUBLIC cxx_std_17)
if(NOT MSVC)
  target_compile_options(clipper2 PRIVATE -fno-exceptions)
endif()
