// Linux X11 screen module (round 9): MonitorGet/GetCount/GetName/GetPrimary/
// GetWorkArea + PixelGetColor/PixelSearch.
//
// Semantics follow docs-v2 and upstream lib/env.cpp / lib/pixel.cpp:
//   - Monitors are XRandR 1.2 outputs (with their names), falling back to
//     Xinerama screens and then to the single default screen.  The work
//     area equals the full geometry (X11 has no taskbar concept).
//   - MonitorGet/GetWorkArea return the monitor number; an out-of-range N
//     throws ValueError (upstream FR_E_ARG), no monitors throw OSError.
//   - PixelGetColor returns a hexadecimal numeric string "0xRRGGBB"
//     (docs: Type: String), OSError on failure.
//   - PixelSearch returns 1/0; on failure to find, the output variables are
//     made blank (docs); search order starts at (X1,Y1) toward (X2,Y2) row
//     by row; Variation is a per-channel tolerance 0-255; OSError on failure.
//   - Both pixel functions honour CoordMode Pixel (CLIENT = relative to the
//     active window's client area, SCREEN = screen coordinates).
#include "../../stdafx.h"
#include "../../script.h"
#include "../../globaldata.h"
#include "../../script_func_impl.h"
#include "core_screen_linux.h"
#include "core_wayland_linux.h"
#include "core_win_linux.h"
#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <X11/extensions/Xinerama.h>
#include <X11/extensions/Xrandr.h>
#include <cstdlib>
#include <cstring>
#include <cstdio>
#include <string>
#include <vector>
#include <algorithm>

// ---------------------------------------------------------------------------
// Monitor enumeration (XRandR outputs > Xinerama screens > single screen)
// ---------------------------------------------------------------------------

struct LinuxMonitorInfo
{
	int x, y, w, h;
	std::wstring name;
};

static std::wstring LinuxScreenWide(const char *aUtf8)
{
	std::wstring w;
	if (!aUtf8)
		return w;
	for (const char *p = aUtf8; *p; ++p)
		w += (unsigned char)*p < 0x80 ? (wchar_t)(unsigned char)*p : L'?';
	return w;
}

static int LinuxMonitorList(Display *d, std::vector<LinuxMonitorInfo> &aOut)
{
	aOut.clear();
	Window root = DefaultRootWindow(d);

	// XRandR 1.2 outputs (with names).
	int ev = 0, err = 0;
	if (XRRQueryExtension(d, &ev, &err))
	{
		XRRScreenResources *res = XRRGetScreenResources(d, root);
		if (res)
		{
			for (int i = 0; i < (int)res->noutput; ++i)
			{
				XRROutputInfo *oi = XRRGetOutputInfo(d, res, res->outputs[i]);
				if (!oi)
					continue;
				if (oi->crtc)
				{
					XRRCrtcInfo *ci = XRRGetCrtcInfo(d, res, oi->crtc);
					if (ci && ci->width > 0 && ci->height > 0)
					{
						LinuxMonitorInfo m;
						m.x = ci->x;
						m.y = ci->y;
						m.w = ci->width;
						m.h = ci->height;
						m.name = LinuxScreenWide(oi->name);
						aOut.push_back(m);
					}
					if (ci)
						XRRFreeCrtcInfo(ci); // Required; leaks otherwise (LSan).
				}
				XRRFreeOutputInfo(oi);
			}
			XRRFreeScreenResources(res);
		}
	}

	// Xinerama fallback (no names).
	if (aOut.empty())
	{
		int xev = 0, xerr = 0;
		if (XineramaQueryExtension(d, &xev, &xerr))
		{
			int n = 0;
			XineramaScreenInfo *si = XineramaQueryScreens(d, &n);
			for (int i = 0; i < n && si; ++i)
			{
				LinuxMonitorInfo m;
				m.x = si[i].x_org;
				m.y = si[i].y_org;
				m.w = si[i].width;
				m.h = si[i].height;
				aOut.push_back(m);
			}
			if (si)
				XFree(si);
		}
	}

	// Last resort: the single default screen.
	if (aOut.empty())
	{
		LinuxMonitorInfo m;
		m.x = 0;
		m.y = 0;
		m.w = DisplayWidth(d, DefaultScreen(d));
		m.h = DisplayHeight(d, DefaultScreen(d));
		aOut.push_back(m);
	}
	for (size_t i = 0; i < aOut.size(); ++i)
		if (aOut[i].name.empty())
			aOut[i].name = L"Monitor" + std::to_wstring(i + 1);
	return (int)aOut.size();
}

// Resolve the monitor number (1-based; 0/omitted = primary) and fill aInfo.
// Returns false and sets the error on aResultToken when invalid.
static bool LinuxMonitorResolve(ResultToken &aResultToken, Display *d
	, const std::vector<LinuxMonitorInfo> &aList, int aN, LinuxMonitorInfo &aInfo)
{
	if (aN < 1)
		aN = 1; // Primary.
	if (aN > (int)aList.size())
	{
		aResultToken.Error(_T("The specified monitor does not exist."), _T(""), ErrorPrototype::Value);
		return false;
	}
	aInfo = aList[aN - 1];
	return true;
}

// ---------------------------------------------------------------------------
// MonitorGet* BIFs
// ---------------------------------------------------------------------------

BIF_DECL(BIF_Linux_MonitorGetCount)
{
	Display *d = LinuxX11Display();
	if (!d)
	{
		aResultToken.Error(_T("No X display is available."), _T(""), ErrorPrototype::OS);
		return;
	}
	std::vector<LinuxMonitorInfo> list;
	aResultToken.SetValue((__int64)LinuxMonitorList(d, list));
}

BIF_DECL(BIF_Linux_MonitorGetPrimary)
{
	Display *d = LinuxX11Display();
	if (!d)
	{
		aResultToken.Error(_T("No X display is available."), _T(""), ErrorPrototype::OS);
		return;
	}
	// The first monitor is the primary one (single-monitor: always 1).
	aResultToken.SetValue((__int64)1);
}

static void LinuxMonitorGetImpl(ResultToken &aResultToken, ExprTokenType *aParam[], int aParamCount, bool aWorkArea)
{
	Display *d = LinuxX11Display();
	if (!d)
	{
		aResultToken.Error(_T("No X display is available."), _T(""), ErrorPrototype::OS);
		return;
	}
	int n = aParamCount > 0 && !ParamIndexIsOmitted(0) ? (int)TokenToInt64(*aParam[0]) : 1;
	std::vector<LinuxMonitorInfo> list;
	if (LinuxMonitorList(d, list) == 0)
	{
		aResultToken.Error(_T("No monitors are available."), _T(""), ErrorPrototype::OS);
		return;
	}
	LinuxMonitorInfo info;
	if (!LinuxMonitorResolve(aResultToken, d, list, n, info))
		return;
	// Bounding coordinates (right/bottom are exclusive, like Windows).
	int left = info.x, top = info.y, right = info.x + info.w, bottom = info.y + info.h;
	Var *out;
	if (aParamCount > 1 && (out = TokenToOutputVar(*aParam[1]))) out->Assign((__int64)left);
	if (aParamCount > 2 && (out = TokenToOutputVar(*aParam[2]))) out->Assign((__int64)top);
	if (aParamCount > 3 && (out = TokenToOutputVar(*aParam[3]))) out->Assign((__int64)right);
	if (aParamCount > 4 && (out = TokenToOutputVar(*aParam[4]))) out->Assign((__int64)bottom);
	aResultToken.SetValue((__int64)(n < 1 ? 1 : n)); // Docs: the monitor number.
}

BIF_DECL(BIF_Linux_MonitorGet)        { LinuxMonitorGetImpl(aResultToken, aParam, aParamCount, false); }
BIF_DECL(BIF_Linux_MonitorGetWorkArea) { LinuxMonitorGetImpl(aResultToken, aParam, aParamCount, true); }

BIF_DECL(BIF_Linux_MonitorGetName)
{
	Display *d = LinuxX11Display();
	if (!d)
	{
		aResultToken.Error(_T("No X display is available."), _T(""), ErrorPrototype::OS);
		return;
	}
	int n = aParamCount > 0 && !ParamIndexIsOmitted(0) ? (int)TokenToInt64(*aParam[0]) : 1;
	std::vector<LinuxMonitorInfo> list;
	if (LinuxMonitorList(d, list) == 0)
	{
		aResultToken.Error(_T("No monitors are available."), _T(""), ErrorPrototype::OS);
		return;
	}
	LinuxMonitorInfo info;
	if (!LinuxMonitorResolve(aResultToken, d, list, n, info))
		return;
	LinuxWinSetPersistentEx(aResultToken, info.name);
}

// ---------------------------------------------------------------------------
// Pixel helpers
// ---------------------------------------------------------------------------

// Convert an X11 pixel value to 0xRRGGBB using the default visual.
// Convert an X11 pixel value to 0xRRGGBB (TrueColor fast path, colormap
// lookup otherwise); exported for ImageSearch (core_image_linux.cpp).
DWORD LinuxPixelToRGB(Display *d, unsigned long aPixel)
{
	Visual *vis = DefaultVisual(d, DefaultScreen(d));
	if (vis->c_class == TrueColor || vis->c_class == DirectColor)
	{
		auto shift = [](unsigned long mask) -> int {
			int s = 0;
			while (mask && !(mask & 1))
			{
				mask >>= 1;
				++s;
			}
			return s;
		};
		DWORD r = (DWORD)((aPixel & vis->red_mask) >> shift(vis->red_mask));
		DWORD g = (DWORD)((aPixel & vis->green_mask) >> shift(vis->green_mask));
		DWORD b = (DWORD)((aPixel & vis->blue_mask) >> shift(vis->blue_mask));
		// Scale to 8 bits for depths < 24.
		int depth = DefaultDepth(d, DefaultScreen(d));
		if (depth < 24)
		{
			r = (DWORD)(r * 255 / ((1u << (depth / 3)) - 1));
			g = (DWORD)(g * 255 / ((1u << (depth / 3)) - 1));
			b = (DWORD)(b * 255 / ((1u << (depth / 3)) - 1));
		}
		return (r << 16) | (g << 8) | b;
	}
	// PseudoColor etc.: resolve through the colormap.
	XColor c;
	c.pixel = aPixel;
	c.flags = DoRed | DoGreen | DoBlue;
	XQueryColor(d, DefaultColormap(d, DefaultScreen(d)), &c);
	return ((c.red >> 8) << 16) | ((c.green >> 8) << 8) | (c.blue >> 8);
}

bool LinuxScreenGrabRegion(Display *d, int aLeft, int aTop, int aW, int aH
	, std::vector<DWORD> &aScreen)
{
	aScreen.clear();
	XImage *ximg = XGetImage(d, DefaultRootWindow(d), aLeft, aTop
		, (unsigned)aW, (unsigned)aH, AllPlanes, ZPixmap);
	if (ximg)
	{
		aScreen.resize((size_t)aW * aH);
		for (int y = 0; y < aH; ++y)
			for (int x = 0; x < aW; ++x)
				aScreen[(size_t)y * aW + x] = LinuxPixelToRGB(d, XGetPixel(ximg, x, y));
		XDestroyImage(ximg);
		return true;
	}
	// sway's XWayland root has no backing store, so XGetImage returns
	// BadMatch; capture through the compositor (wlr-screencopy) instead.
	if (LinuxWaylandCaptureScreen(aLeft, aTop, aW, aH, aScreen))
		return true;
	aScreen.clear();
	return false;
}

// Apply CoordMode Pixel: CLIENT mode translates the point from the active
// window's client area to screen coordinates.
static void LinuxPixelCoords(Display *d, int &x, int &y)
{
	if (((g->CoordMode >> COORD_MODE_PIXEL) & COORD_MODE_MASK) == COORD_MODE_CLIENT)
	{
		Window active = LinuxX11ActiveWindow();
		if (active)
		{
			int rx = 0, ry = 0;
			Window child = 0;
			if (XTranslateCoordinates(d, active, DefaultRootWindow(d), 0, 0, &rx, &ry, &child))
			{
				x += rx;
				y += ry;
			}
		}
	}
}

BIF_DECL(BIF_Linux_PixelGetColor)
{
	Display *d = LinuxX11Display();
	if (!d)
	{
		aResultToken.Error(_T("No X display is available."), _T(""), ErrorPrototype::OS);
		return;
	}
	int x = (int)TokenToInt64(*aParam[0]);
	int y = (int)TokenToInt64(*aParam[1]);
	LinuxPixelCoords(d, x, y);
	int sw = DisplayWidth(d, DefaultScreen(d));
	int sh = DisplayHeight(d, DefaultScreen(d));
	if (x < 0 || y < 0 || x >= sw || y >= sh)
	{
		aResultToken.Error(_T("The specified pixel is outside the screen."), _T(""), ErrorPrototype::OS);
		return;
	}
	XImage *img = XGetImage(d, DefaultRootWindow(d), x, y, 1, 1, AllPlanes, ZPixmap);
	if (!img)
	{
		// XWayland root has no backing store; fall back to screencopy.
		std::vector<DWORD> px;
		if (LinuxScreenGrabRegion(d, x, y, 1, 1, px) && px.size() == 1)
		{
			DWORD rgb = px[0];
			TCHAR buf[32];
			swprintf(buf, 32, _T("0x%06X"), (unsigned)rgb);
			LinuxWinSetPersistentEx(aResultToken, buf);
			return;
		}
		aResultToken.Error(_T("The pixel could not be retrieved."), _T(""), ErrorPrototype::OS);
		return;
	}
	unsigned long px = XGetPixel(img, 0, 0);
	XDestroyImage(img);
	DWORD rgb = LinuxPixelToRGB(d, px);
	// Docs: hexadecimal numeric string.  Use persistent memory: the caller
	// reads the returned string after this BIF returns (stack buffer would
	// be use-after-return, caught by the ASan build).
	TCHAR buf[32];
	swprintf(buf, 32, _T("0x%06X"), (unsigned)rgb);
	LinuxWinSetPersistentEx(aResultToken, buf);
}

BIF_DECL(BIF_Linux_PixelSearch)
{
	Display *d = LinuxX11Display();
	if (!d)
	{
		aResultToken.Error(_T("No X display is available."), _T(""), ErrorPrototype::OS);
		return;
	}
	int x1 = (int)TokenToInt64(*aParam[2]);
	int y1 = (int)TokenToInt64(*aParam[3]);
	int x2 = (int)TokenToInt64(*aParam[4]);
	int y2 = (int)TokenToInt64(*aParam[5]);
	unsigned int color = (unsigned int)TokenToInt64(*aParam[6]);
	int variation = aParamCount > 7 && !ParamIndexIsOmitted(7) ? (int)TokenToInt64(*aParam[7]) : 0;
	if (variation < 0)
		variation = 0;
	LinuxPixelCoords(d, x1, y1);
	LinuxPixelCoords(d, x2, y2);
	int left = std::min(x1, x2), right = std::max(x1, x2);
	int top = std::min(y1, y2), bottom = std::max(y1, y2);
	int sw = DisplayWidth(d, DefaultScreen(d));
	int sh = DisplayHeight(d, DefaultScreen(d));
	if (left < 0) left = 0;
	if (top < 0) top = 0;
	if (right >= sw) right = sw - 1;
	if (bottom >= sh) bottom = sh - 1;
	if (right < left || bottom < top)
	{
		aResultToken.SetValue((__int64)0); // Empty region: not found.
		return;
	}
	// Grab the region (XGetImage, falling back to wlr-screencopy when the
	// XWayland root has no backing store).
	std::vector<DWORD> screen;
	if (!LinuxScreenGrabRegion(d, left, top, right - left + 1, bottom - top + 1, screen))
	{
		aResultToken.Error(_T("The region could not be retrieved."), _T(""), ErrorPrototype::OS);
		return;
	}
	// Search from (X1,Y1) toward (X2,Y2), row by row (docs).
	int xstep = x1 <= x2 ? 1 : -1;
	int ystep = y1 <= y2 ? 1 : -1;
	int found_x = 0, found_y = 0;
	bool found = false;
	DWORD r_want = (color >> 16) & 0xFF, g_want = (color >> 8) & 0xFF, b_want = color & 0xFF;
	for (int yy = y1; !found && (ystep > 0 ? yy <= y2 : yy >= y2); yy += ystep)
		for (int xx = x1; !found && (xstep > 0 ? xx <= x2 : xx >= x2); xx += xstep)
		{
			if (xx < left || xx > right || yy < top || yy > bottom)
				continue; // Clipped away.
			unsigned long px = screen[(size_t)(yy - top) * (right - left + 1) + (xx - left)];
			DWORD rgb = px;
			DWORD r = (rgb >> 16) & 0xFF, gr = (rgb >> 8) & 0xFF, b = rgb & 0xFF;
			int dr = (int)r - (int)r_want;
			int dg = (int)gr - (int)g_want;
			int db = (int)b - (int)b_want;
			if (dr < 0) dr = -dr;
			if (dg < 0) dg = -dg;
			if (db < 0) db = -db;
			if (dr <= variation && dg <= variation && db <= variation)
			{
				found = true;
				found_x = xx;
				found_y = yy;
			}
		}
	Var *out;
	if (found)
	{
		if ((out = TokenToOutputVar(*aParam[0]))) out->Assign((__int64)found_x);
		if ((out = TokenToOutputVar(*aParam[1]))) out->Assign((__int64)found_y);
		aResultToken.SetValue((__int64)1);
	}
	else
	{
		// Docs: if no match is found, the variables are made blank.
		if ((out = TokenToOutputVar(*aParam[0]))) out->Assign(_T(""));
		if ((out = TokenToOutputVar(*aParam[1]))) out->Assign(_T(""));
		aResultToken.SetValue((__int64)0);
	}
}
