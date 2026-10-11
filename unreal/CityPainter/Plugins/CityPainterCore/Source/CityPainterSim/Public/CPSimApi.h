#pragma once

// Export macro for the Sim module's public API. UBT defines CITYPAINTERSIM_API (as DLLEXPORT / DLLIMPORT, which come
// from HAL/Platform.h); it is empty when a header is compiled outside Unreal (the standalone check in unreal/tools/README.md).
#if defined(CITYPAINTERSIM_API)
#include "HAL/Platform.h"
#else
#define CITYPAINTERSIM_API
#endif
