#pragma once

// Minimal Linux shim for <shlwapi.h>.
// Currently only provides StrCmpLogicalW(), which string.cpp uses for sorting.

#include "../stdafx_linux.h"

inline int StrCmpLogicalW(const wchar_t *a, const wchar_t *b)
{
	// TODO: implement natural/logical comparison.  Plain lexical order is
	// sufficient for basic script compatibility until a real port lands.
	return wcscmp(a ? a : L"", b ? b : L"");
}