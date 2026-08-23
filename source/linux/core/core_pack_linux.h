// Packed-script support (check_detail0821 §5-M6 / R4): --pack + self-extract.
#pragma once
#include "../../stdafx.h"
#include <vector>

// True when this process started from a packed binary (A_IsCompiled = 1).
extern bool g_LinuxPacked;

// Check whether the given file's tail carries the pack footer.
bool LinuxPackFooter(const char *aPath, size_t &aScriptLen, size_t &aScriptOff);

// True when the running executable is packed.
bool LinuxIsPacked();

// ahk_core --pack outfile script.ahk.
bool LinuxPackExecutable(const char *aOut, const char *aScript);

// Extract the packed script into a caller buffer (NUL-terminated); 0 on error.
size_t LinuxExtractPackedScript(char *aBuf, size_t aBufCap);