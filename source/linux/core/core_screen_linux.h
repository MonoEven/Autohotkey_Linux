// Declarations of the X11 screen-module BIFs (implemented in
// core_screen_linux.cpp) for use by the LMD function table.
#pragma once

#include "../../abi.h"
#include <X11/Xlib.h>

BIF_DECL(BIF_Linux_MonitorGet);
BIF_DECL(BIF_Linux_MonitorGetCount);
BIF_DECL(BIF_Linux_MonitorGetName);
BIF_DECL(BIF_Linux_MonitorGetPrimary);
BIF_DECL(BIF_Linux_MonitorGetWorkArea);
BIF_DECL(BIF_Linux_PixelGetColor);
BIF_DECL(BIF_Linux_PixelSearch);

// Convert an X11 pixel value to 0xRRGGBB (TrueColor fast path, colormap
// lookup otherwise); shared with ImageSearch (core_image_linux.cpp).
DWORD LinuxPixelToRGB(Display *d, unsigned long aPixel);
