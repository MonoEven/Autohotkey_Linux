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

// Grab the (aLeft,aTop)-(aLeft+aW-1,aTop+aH-1) screen region as 0xRRGGBB
// row-major into aScreen.  XGetImage cannot read sway's XWayland root (no
// backing store, BadMatch), so on failure this falls back to capturing
// through the compositor's wlr-screencopy protocol.  Returns false when
// no backend can provide the pixels.  Shared by PixelGetColor/PixelSearch
// and ImageSearch.
bool LinuxScreenGrabRegion(Display *d, int aLeft, int aTop, int aW, int aH
	, std::vector<DWORD> &aScreen);
