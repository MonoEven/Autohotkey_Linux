#pragma once

// Minimal declarations for core files that only need error-reporting hooks.
// Used on Linux to avoid pulling in the full platform-dependent globaldata.h.

#include "../defines.h"

#ifdef UNICODE
#define tmemcpy wmemcpy
#else
#define tmemcpy memcpy
#endif

ResultType MemoryError();
