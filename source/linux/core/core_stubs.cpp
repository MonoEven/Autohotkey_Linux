// Temporary Linux stubs for core error-reporting functions.
// These will be replaced by real error handling as the interpreter is ported.

#include "../../stdafx.h"
#include "../core_errors.h"
#include "../../SimpleHeap.h"
#include <cstdio>
#include <cstdlib>

ResultType MemoryError()
{
	std::fputs("AutoHotkey Linux: out of memory\n", stderr);
	return FAIL;
}

void SimpleHeap::CriticalFail()
{
	std::fputs("AutoHotkey Linux: fatal out-of-memory error\n", stderr);
	std::exit(1);
}
