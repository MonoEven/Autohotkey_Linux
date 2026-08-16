// Declarations of the X11 screen-module BIFs (implemented in
// core_screen_linux.cpp) for use by the LMD function table.
#pragma once

#include "../../abi.h"

BIF_DECL(BIF_Linux_MonitorGet);
BIF_DECL(BIF_Linux_MonitorGetCount);
BIF_DECL(BIF_Linux_MonitorGetName);
BIF_DECL(BIF_Linux_MonitorGetPrimary);
BIF_DECL(BIF_Linux_MonitorGetWorkArea);
BIF_DECL(BIF_Linux_PixelGetColor);
BIF_DECL(BIF_Linux_PixelSearch);
