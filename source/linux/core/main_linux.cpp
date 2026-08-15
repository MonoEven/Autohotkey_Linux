// Temporary Linux entry point for the AutoHotkey core port.
// This will later be replaced by a real argument parser and script runner.

#include "../../stdafx.h"
#include "../../ahkversion.h"
#include <cstdio>

int main()
{
	std::printf("AutoHotkey Linux port scaffold (version %s)\n", T_AHK_VERSION ? "2.0" : "2.0");
	return 0;
}
