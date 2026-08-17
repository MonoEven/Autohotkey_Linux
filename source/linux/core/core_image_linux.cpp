// Linux X11 image module (round 18): LoadPicture, IL_Create/IL_Add/
// IL_Destroy and ImageSearch.
//
// Windows semantics per docs-v2 and the upstream implementations
// (script2.cpp for IL_*, lib/pixel.cpp for ImageSearch, lib/Gui.* for
// LoadPicture):
//   - LoadPicture loads BMP (24/32-bit BI_RGB) and PPM (P6/P3) files --
//     the formats the port can decode natively -- applies the Wn/Hn
//     resize options (nearest-neighbour; -1 keeps the aspect ratio, 0 uses
//     the original dimension), accepts the Icon/GDI+ options (icon
//     resources and GDI+ do not exist on Linux; the image is loaded as a
//     bitmap, documented) and returns an opaque handle into the port's
//     image store (0 on any error, per the docs).
//   - IL_Create/IL_Destroy/IL_Add maintain image lists in the same store
//     (handles are stable ids; IL_Add returns the 1-based index of the
//     added image, or 0 on failure; IL_Destroy returns 1/0; IL_Add with a
//     zero handle raises a ValueError like upstream).  The lists exist so
//     that scripts written for the ListView/TreeView icon options keep
//     working, but nothing can display them (no Gui on the port).
//   - ImageSearch grabs the search region (XGetImage, or wlr-screencopy
//     on XWayland where the root has no backing store) and scans for the
//     image top-left position (exact match, or per-channel variation with
//     *N; *IconN/*wN/*hN/*Trans<color> options are parsed like upstream,
//     icons are treated as bitmaps).  Returns 1 with the coordinates in
//     the output variables (blank if not found -> 0); ValueError on bad
//     options or an unloadable image; OSError if the screen grab fails.

#include "../../stdafx.h"
#include "../../script.h"
#include "../../globaldata.h"
#include "../../script_func_impl.h"
#include "core_image_linux.h"
#include "core_screen_linux.h"
#include "core_win_linux.h"
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <cstdlib>
#include <cstring>
#include <cstdio>
#include <cwchar>
#include <fstream>
#include <string>
#include <vector>
#include <map>

// ---------------------------------------------------------------------------
// Image store
// ---------------------------------------------------------------------------

struct LinuxImage
{
	int width = 0;
	int height = 0;
	std::vector<DWORD> pixels; // 0xRRGGBB, row-major.
};

struct LinuxImageList
{
	int image_size = 16;      // Large icons = 32, small = 16 (SM_CXICON/SM_CXSMICON).
	std::vector<int> images;  // Image handles in the list.
};

static std::vector<LinuxImage> &LinuxImages()
{
	static std::vector<LinuxImage> sImages;
	return sImages;
}

static std::map<UINT_PTR, LinuxImageList> &LinuxImageLists()
{
	static std::map<UINT_PTR, LinuxImageList> sLists;
	return sLists;
}

static UINT_PTR LinuxNextListId()
{
	static UINT_PTR sNext = 1;
	return sNext++;
}

// ---------------------------------------------------------------------------
// Image file loading (BMP 24/32-bit BI_RGB, PPM P6/P3) + nearest-neighbour
// resize.  Returns false when the file cannot be read or decoded.
// ---------------------------------------------------------------------------

static bool LinuxReadFileBytes(const wchar_t *aPath, std::vector<unsigned char> &aOut)
{
	aOut.clear();
	// Relative paths resolve against A_WorkingDir (docs).  The std::ifstream
	// here is byte-oriented, so the wide path is narrowed first.
	char narrow[4096];
	if (!aPath || wcstombs(narrow, aPath, sizeof(narrow)) == (size_t)-1)
		return false;
	std::ifstream f(narrow, std::ios::binary);
	if (!f)
	{
		std::wstring full = std::wstring(g_WorkingDir.GetString()) + L"/" + aPath;
		if (wcstombs(narrow, full.c_str(), sizeof(narrow)) == (size_t)-1)
			return false;
		f.open(narrow, std::ios::binary);
	}
	if (!f)
		return false;
	f.seekg(0, std::ios::end);
	std::streamoff len = f.tellg();
	if (len <= 0)
		return false;
	f.seekg(0, std::ios::beg);
	aOut.resize((size_t)len);
	f.read((char *)aOut.data(), len);
	return !f.bad();
}

static bool LinuxLoadBMP(const std::vector<unsigned char> &aData, LinuxImage &aOut)
{
	if (aData.size() < 54 || aData[0] != 'B' || aData[1] != 'M')
		return false;
	auto u32 = [&](size_t o) -> unsigned { 
		return (unsigned)aData[o] | ((unsigned)aData[o + 1] << 8)
			| ((unsigned)aData[o + 2] << 16) | ((unsigned)aData[o + 3] << 24);
	};
	auto i32 = [&](size_t o) -> int { return (int)u32(o); };
	unsigned data_off = u32(10);
	unsigned bi_size = u32(14);
	if (bi_size < 40 || data_off > aData.size())
		return false;
	int width = i32(18);
	int height = i32(22);
	unsigned planes = (unsigned)aData[26] | ((unsigned)aData[27] << 8);
	unsigned bpp = (unsigned)aData[28] | ((unsigned)aData[29] << 8);
	unsigned compression = u32(30);
	if (planes != 1 || (bpp != 24 && bpp != 32) || compression != 0)
		return false;
	if (width <= 0 || height == 0)
		return false;
	bool top_down = height < 0;
	unsigned h = (unsigned)(height < 0 ? -height : height);
	unsigned row_bytes = (unsigned)width * bpp / 8;
	unsigned padded = (row_bytes + 3) & ~3u;
	if ((unsigned long long)data_off + (unsigned long long)padded * h > aData.size())
		return false;
	aOut.width = width;
	aOut.height = (int)h;
	aOut.pixels.resize((size_t)width * h);
	for (unsigned y = 0; y < h; ++y)
	{
		unsigned src_row = top_down ? y : h - 1 - y;
		const unsigned char *row = aData.data() + data_off + (size_t)src_row * padded;
		for (int x = 0; x < width; ++x)
		{
			const unsigned char *p = row + (size_t)x * bpp / 8;
			DWORD rgb = ((DWORD)p[2] << 16) | ((DWORD)p[1] << 8) | p[0];
			aOut.pixels[(size_t)y * width + x] = rgb;
		}
	}
	return true;
}

// PPM: P6 (binary) or P3 (ASCII) with optional '#' comments.
static bool LinuxLoadPPM(const std::vector<unsigned char> &aData, LinuxImage &aOut)
{
	size_t pos = 0;
	auto skip_ws = [&]() {
		for (;;)
		{
			while (pos < aData.size() && (aData[pos] == ' ' || aData[pos] == '\t'
				|| aData[pos] == '\r' || aData[pos] == '\n'))
				++pos;
			if (pos < aData.size() && aData[pos] == '#')
			{
				while (pos < aData.size() && aData[pos] != '\n')
					++pos;
				continue;
			}
			break;
		}
	};
	auto next_int = [&](int &aVal) -> bool {
		skip_ws();
		if (pos >= aData.size() || aData[pos] < '0' || aData[pos] > '9')
			return false;
		int v = 0;
		while (pos < aData.size() && aData[pos] >= '0' && aData[pos] <= '9')
			v = v * 10 + (aData[pos++] - '0');
		aVal = v;
		return true;
	};
	if (aData.size() < 2 || aData[0] != 'P')
		return false;
	bool binary = aData[1] == '6';
	if (aData[1] != '6' && aData[1] != '3')
		return false;
	pos = 2;
	int width = 0, height = 0, maxval = 0;
	if (!next_int(width) || !next_int(height) || !next_int(maxval) || width <= 0
		|| height <= 0 || maxval < 1 || maxval > 65535)
		return false;
	if (binary)
	{
		// Exactly one whitespace character follows the maxval.
		if (pos >= aData.size())
			return false;
		++pos;
		if ((size_t)width * height * 3 > aData.size() - pos)
			return false;
		aOut.width = width;
		aOut.height = height;
		aOut.pixels.resize((size_t)width * height);
		for (size_t i = 0; i < (size_t)width * height; ++i)
		{
			const unsigned char *p = aData.data() + pos + i * 3;
			aOut.pixels[i] = ((DWORD)p[0] << 16) | ((DWORD)p[1] << 8) | p[2];
		}
		return true;
	}
	// P3: ASCII RGB triplets.
	aOut.width = width;
	aOut.height = height;
	aOut.pixels.resize((size_t)width * height);
	for (size_t i = 0; i < (size_t)width * height; ++i)
	{
		int r = 0, g = 0, b = 0;
		if (!next_int(r) || !next_int(g) || !next_int(b))
			return false;
		auto scale = [&](int v) -> DWORD {
			return maxval == 255 ? (DWORD)v : (DWORD)(v * 255 / maxval);
		};
		aOut.pixels[i] = (scale(r) << 16) | (scale(g) << 8) | scale(b);
	}
	return true;
}

// ICO (Windows icon): ICONDIR + N ICONDIRENTRY; each payload is a DIB
// (BITMAPINFOHEADER + XOR bitmap + AND mask) or a PNG (skipped - not
// decodable here).  32/24-bit and indexed (8/4/1-bit) DIBs are supported.
// Transparency is not representable in the port's RGB-only image model, so
// transparent pixels (AND-mask bit set, or 32-bit alpha < 128) are marked
// with the magenta sentinel 0xFFFF00FF (documented), usable with ImageSearch
// *Trans / PixelGetColor.
static bool LinuxLoadICO(const std::vector<unsigned char> &aData, LinuxImage &aOut)
{
	auto u16 = [&](size_t o) -> unsigned {
		return (unsigned)aData[o] | ((unsigned)aData[o + 1] << 8);
	};
	auto u32 = [&](size_t o) -> unsigned {
		return (unsigned)aData[o] | ((unsigned)aData[o + 1] << 8)
			| ((unsigned)aData[o + 2] << 16) | ((unsigned)aData[o + 3] << 24);
	};
	if (aData.size() < 6 || aData[0] != 0 || aData[1] != 0
		|| aData[2] != 1 || aData[3] != 0) // reserved=0, type=1 (icon).
		return false;
	unsigned count = u16(4);
	if (!count || aData.size() < 6 + (size_t)count * 16)
		return false;
	// Pick the largest non-PNG entry (traditional .ico holds several sizes).
	int best = -1;
	unsigned best_area = 0, best_w = 0, best_h = 0;
	for (unsigned i = 0; i < count; ++i)
	{
		size_t eo = 6 + (size_t)i * 16;
		unsigned w = aData[eo] ? (unsigned)aData[eo] : 256u;
		unsigned h = aData[eo + 1] ? (unsigned)aData[eo + 1] : 256u;
		unsigned off = u32(eo + 12);
		if ((size_t)off + 40 > aData.size())
			continue;
		if (aData[off] == 0x89 && aData[off + 1] == 'P' && aData[off + 2] == 'N' && aData[off + 3] == 'G')
			continue; // PNG-compressed entry: cannot decode here.
		unsigned area = w * h;
		if (area > best_area)
		{
			best_area = area; best = (int)i; best_w = w; best_h = h;
		}
	}
	if (best < 0)
		return false;
	size_t eo = 6 + (size_t)best * 16;
	unsigned off = u32(eo + 12);
	if ((size_t)off + 40 > aData.size())
		return false;
	unsigned bi_size = u32(off);
	if (bi_size < 40)
		return false;
	int bi_h = (int)u32(off + 8); // Doubled (XOR + AND mask); sign = row order.
	unsigned planes = u16(off + 12);
	unsigned bpp = u16(off + 14);
	unsigned compression = u32(off + 16);
	if (planes != 1 || compression != 0
		|| (bpp != 32 && bpp != 24 && bpp != 8 && bpp != 4 && bpp != 1))
		return false;
	unsigned clr_used = u32(off + 32);
	bool top_down = bi_h < 0;
	int w = (int)best_w, h = (int)best_h;
	if (w <= 0 || h <= 0)
		return false;

	// Palette (indexed formats) follows the header.
	std::vector<DWORD> palette;
	if (bpp <= 8)
	{
		unsigned n = clr_used ? clr_used : (1u << bpp);
		size_t po = off + bi_size;
		if (po + (size_t)n * 4 > aData.size())
			return false;
		for (unsigned k = 0; k < n; ++k)
			palette.push_back(((DWORD)aData[po + k * 4 + 2] << 16)
				| ((DWORD)aData[po + k * 4 + 1] << 8) | (DWORD)aData[po + k * 4]);
	}

	unsigned row_bytes = ((unsigned)w * bpp + 7) / 8;
	unsigned pad = (row_bytes + 3) & ~3u;
	unsigned and_row_bytes = ((unsigned)w + 7) / 8;
	unsigned and_pad = (and_row_bytes + 3) & ~3u;
	size_t xor_off = off + bi_size + palette.size() * 4;
	size_t and_off = xor_off + (size_t)pad * (unsigned)h;
	if (and_off + (size_t)and_pad * (unsigned)h > aData.size())
		return false;

	aOut.width = w;
	aOut.height = h;
	aOut.pixels.resize((size_t)w * h);
	for (int y = 0; y < h; ++y)
	{
		unsigned src_row = top_down ? (unsigned)y : (unsigned)(h - 1 - y);
		const unsigned char *row = aData.data() + xor_off + (size_t)src_row * pad;
		const unsigned char *androw = aData.data() + and_off + (size_t)src_row * and_pad;
		for (int x = 0; x < w; ++x)
		{
			DWORD c = 0;
			bool transparent = false;
			if (bpp == 32)
			{
				const unsigned char *p = row + (size_t)x * 4;
				c = ((DWORD)p[2] << 16) | ((DWORD)p[1] << 8) | p[0];
				transparent = p[3] < 128;
			}
			else if (bpp == 24)
			{
				const unsigned char *p = row + (size_t)x * 3;
				c = ((DWORD)p[2] << 16) | ((DWORD)p[1] << 8) | p[0];
			}
			else // indexed: bits per pixel packed MSB-first.
			{
				unsigned idx_bits = (unsigned)(x * (int)bpp);
				unsigned idx = (row[idx_bits / 8] >> (8 - bpp - (idx_bits % 8)))
					& ((1u << bpp) - 1);
				c = idx < palette.size() ? palette[idx] : 0;
			}
			if ((androw[x / 8] >> (7 - (x % 8))) & 1)
				transparent = true;
			aOut.pixels[(size_t)y * w + x] = transparent ? 0xFFFF00FFu : c;
		}
	}
	return true;
}

// Nearest-neighbour resize; aReqW/aReqH of 0 keep the original dimension,
// -1 keeps the aspect ratio based on the other dimension (docs).
static void LinuxImageResize(LinuxImage &aImg, int aReqW, int aReqH)
{
	int w = aReqW, h = aReqH;
	if (w < 0 && h > 0)
		w = aImg.width * h / aImg.height;
	else if (h < 0 && w > 0)
		h = aImg.height * w / aImg.width;
	else if (w < 0 || h < 0)
	{
		w = aImg.width;
		h = aImg.height;
	}
	if (w <= 0)
		w = aImg.width;
	if (h <= 0)
		h = aImg.height;
	if (w == aImg.width && h == aImg.height)
		return;
	std::vector<DWORD> out((size_t)w * h);
	for (int y = 0; y < h; ++y)
	{
		int sy = (int)((unsigned long long)y * aImg.height / h);
		if (sy >= aImg.height)
			sy = aImg.height - 1;
		for (int x = 0; x < w; ++x)
		{
			int sx = (int)((unsigned long long)x * aImg.width / w);
			if (sx >= aImg.width)
				sx = aImg.width - 1;
			out[(size_t)y * w + x] = aImg.pixels[(size_t)sy * aImg.width + sx];
		}
	}
	aImg.width = w;
	aImg.height = h;
	aImg.pixels.swap(out);
}

static bool LinuxImageLoad(const wchar_t *aPath, int aReqW, int aReqH, LinuxImage &aOut, bool *aIsIcon = nullptr)
{
	std::vector<unsigned char> data;
	if (!LinuxReadFileBytes(aPath, data))
		return false;
	if (LinuxLoadICO(data, aOut))      // .ico (DIB entry) - icon type.
	{
		if (aIsIcon)
			*aIsIcon = true;
	}
	else if (LinuxLoadBMP(data, aOut) || LinuxLoadPPM(data, aOut))
	{
		if (aIsIcon)
			*aIsIcon = false;
	}
	else
		return false;
	LinuxImageResize(aOut, aReqW, aReqH);
	return true;
}

// Register a loaded image; returns its handle (0 on failure).
static UINT_PTR LinuxImageAdd(LinuxImage &aImg)
{
	if (aImg.width <= 0 || aImg.height <= 0)
		return 0;
	LinuxImages().push_back(std::move(aImg));
	return (UINT_PTR)LinuxImages().size();
}

// ---------------------------------------------------------------------------
// LoadPicture / IL_Create / IL_Add / IL_Destroy
// ---------------------------------------------------------------------------

BIF_DECL(BIF_Linux_LoadPicture)
{
	// (In, String, Filename), (In_Opt, String, Options),
	// (Out_Opt, Int32, ImageType) -> handle.
	TCHAR file_buf[4096];
	LPTSTR file = TokenToString(*aParam[0], file_buf, nullptr);
	int req_w = 0, req_h = 0;
	bool want_icon = false;
	if (aParamCount > 1 && !ParamIndexIsOmitted(1))
	{
		TCHAR opt_buf[512];
		LPTSTR opts = TokenToString(*aParam[1], opt_buf, nullptr);
		for (const wchar_t *p = opts ? opts : L""; *p; )
		{
			while (*p == L' ' || *p == L'\t')
				++p;
			if (!*p)
				break;
			wchar_t c = towupper(*p);
			if (c == L'W' || c == L'H')
			{
				int v = (int)wcstol(p + 1, nullptr, 10);
				if (c == L'W')
					req_w = v;
				else
					req_h = v;
				while (*p && *p != L' ' && *p != L'\t')
					++p;
			}
			else if (!_tcsnicmp(p, L"GDI+", 4))
			{
				// GDI+ is unavailable on Linux; the native loader is used
				// (documented).
				p += 4;
			}
			else if (!_tcsnicmp(p, L"Icon", 4))
			{
				// "Icon n": icon resources are not supported on Linux; the
				// image is loaded as a bitmap but reported as an icon when
				// OutImageType is present (docs: "Any supported image
				// format can be converted to an icon").
				want_icon = true;
				p += 4;
				while (*p && *p != L' ' && *p != L'\t')
					++p;
			}
			else // Unknown option: ignore (docs tolerate extra text).
			{
				while (*p && *p != L' ' && *p != L'\t')
					++p;
			}
		}
	}
	LinuxImage img;
	bool is_icon = false;
	bool ok = LinuxImageLoad(file ? file : file_buf, req_w, req_h, img, &is_icon);
	// Docs: "If there are any errors, the function returns 0."
	UINT_PTR handle = ok ? LinuxImageAdd(img) : 0;
	Var *out = nullptr;
	if (aParamCount > 2 && (out = TokenToOutputVar(*aParam[2])))
		out->Assign((want_icon || is_icon) ? _T("Icon") : _T("Bitmap"));
	aResultToken.SetValue((__int64)handle);
}

BIF_DECL(BIF_Linux_IL_Create)
{
	// (In_Opt, Int32, InitialCount), (In_Opt, Int32, GrowCount),
	// (In_Opt, Bool32, LargeIcons) -> handle or 0 (docs).
	int slot = 0;
	bool large = aParamCount > 2 && !ParamIndexIsOmitted(2) && TokenToBOOL(*aParam[2]);
	LinuxImageList list;
	list.image_size = large ? 32 : 16; // SM_CXICON / SM_CXSMICON.
	UINT_PTR id = LinuxNextListId();
	LinuxImageLists()[id] = std::move(list);
	aResultToken.SetValue((__int64)id);
}

BIF_DECL(BIF_Linux_IL_Destroy)
{
	// (In, UIntPtr, ImageList) -> 1 on success, 0 on failure (docs).
	UINT_PTR id = (UINT_PTR)TokenToInt64(*aParam[0]);
	aResultToken.SetValue((__int64)(LinuxImageLists().erase(id) ? 1 : 0));
}

BIF_DECL(BIF_Linux_IL_Add)
{
	// (In, UIntPtr, ImageList), (In, String, Filename),
	// (In_Opt, Int32, IconNumber), (In_Opt, Bool32, ResizeNonIcon)
	// -> 1-based index, or 0 on failure (docs).
	UINT_PTR id = (UINT_PTR)TokenToInt64(*aParam[0]);
	if (!id)
	{
		// Upstream: FR_E_ARG(0) for a zero image list handle.
		FResultToError(aResultToken, aParam, aParamCount, FR_E_ARG(0), 0);
		return;
	}
	auto it = LinuxImageLists().find(id);
	if (it == LinuxImageLists().end())
	{
		// Upstream: ImageList_Add on an invalid handle fails -> index 0.
		aResultToken.SetValue((__int64)0);
		return;
	}
	TCHAR file_buf[4096];
	LPTSTR file = TokenToString(*aParam[1], file_buf, nullptr);
	int req_w = 0, req_h = 0;
	if (aParamCount > 3 && !ParamIndexIsOmitted(3) && TokenToBOOL(*aParam[3]))
	{
		// ResizeNonIcon true: scale to the image list's size.
		req_w = req_h = it->second.image_size;
	}
	// IconNumber (param 3) selects an icon inside a multi-icon resource
	// file; the port's loader only decodes whole bitmaps, so it is accepted
	// and ignored (documented).
	LinuxImage img;
	if (!LinuxImageLoad(file ? file : file_buf, req_w, req_h, img))
	{
		aResultToken.SetValue((__int64)0);
		return;
	}
	UINT_PTR image_handle = LinuxImageAdd(img);
	if (!image_handle)
	{
		aResultToken.SetValue((__int64)0);
		return;
	}
	it->second.images.push_back((int)image_handle);
	aResultToken.SetValue((__int64)it->second.images.size());
}

// ---------------------------------------------------------------------------
// ImageSearch
// ---------------------------------------------------------------------------

// CoordMode Pixel: CLIENT mode translates the point from the active
// window's client area to screen coordinates (same logic as the pixel
// module).
static void LinuxImageCoords(Display *d, int &x, int &y)
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

BIF_DECL(BIF_Linux_ImageSearch)
{
	// (Out, Variant, X), (Out, Variant, Y), (In, Int32, X1), (In, Int32,
	// Y1), (In, Int32, X2), (In, Int32, Y2), (In, String, Image).
	Display *d = LinuxX11Display();
	if (!d)
	{
		aResultToken.Error(_T("No X display is available."), _T(""), ErrorPrototype::OS);
		return;
	}
	// Default outputs: blank (docs: "if no match is found, the variables
	// are made blank").
	Var *out;
	if ((out = TokenToOutputVar(*aParam[0])))
		out->Assign(_T(""));
	if ((out = TokenToOutputVar(*aParam[1])))
		out->Assign(_T(""));

	int left = (int)TokenToInt64(*aParam[2]);
	int top = (int)TokenToInt64(*aParam[3]);
	int right = (int)TokenToInt64(*aParam[4]);
	int bottom = (int)TokenToInt64(*aParam[5]);
	int origin_x = left, origin_y = top;
	LinuxImageCoords(d, left, top);
	LinuxImageCoords(d, right, bottom);
	origin_x = left - origin_x;
	origin_y = top - origin_y;

	// Parse the ImageFile options ("*N" variation, "*IconN", "*wN", "*hN",
	// "*Trans<color>"; upstream lib/pixel.cpp option grammar).
	TCHAR image_buf[4096];
	LPTSTR image = TokenToString(*aParam[6], image_buf, nullptr);
	int variation = 0;
	int req_w = 0, req_h = 0;
	DWORD trans_color = 0xFFFFFFFF; // CLR_NONE equivalent.
	bool has_trans = false;
	const wchar_t *cp = omit_leading_whitespace(image ? image : L"");
	while (*cp == L'*')
	{
		++cp;
		wchar_t c = towupper(*cp);
		if (c == L'W')
			req_w = (int)wcstol(cp + 1, nullptr, 10);
		else if (c == L'H')
			req_h = (int)wcstol(cp + 1, nullptr, 10);
		else if (!_tcsnicmp(cp, L"Icon", 4))
		{
			// *IconN: icon groups are treated as plain bitmaps (documented).
			cp += 4;
			while (*cp && *cp != L' ' && *cp != L'\t')
				++cp;
		}
		else if (!_tcsnicmp(cp, L"Trans", 5))
		{
			cp += 5;
			wchar_t color_name[64];
			size_t n = 0;
			while (*cp && *cp != L' ' && *cp != L'\t' && n + 1 < _countof(color_name))
				color_name[n++] = *cp++;
			color_name[n] = L'\0';
			// Accept "0xRRGGBB" / "RRGGBB" hex; named colors are not
			// resolved on Linux (documented).
			wchar_t *endp = nullptr;
			unsigned long v = wcstoul(color_name, &endp, 16);
			if (endp && *endp)
				v = 0;
			trans_color = (DWORD)((v & 0xFF) << 16) | (v & 0xFF00) | ((v >> 16) & 0xFF);
			has_trans = true;
		}
		else // "*N" variation (docs: 0..255).
		{
			variation = (int)wcstol(cp, nullptr, 10);
			if (variation < 0)
				variation = 0;
			if (variation > 255)
				variation = 255;
			while (*cp && *cp != L' ' && *cp != L'\t')
				++cp;
		}
		// One space/tab separates the option from what follows (upstream).
		if (!(cp = StrChrAny(cp, _T(" \t"))))
		{
			// Docs: "A ValueError is thrown if an invalid parameter was
			// detected" (upstream FR_E_ARG(6)).
			FResultToError(aResultToken, aParam, aParamCount, FR_E_ARG(6), 0);
			return;
		}
		image = (LPTSTR)(++cp); // Skip the single delimiter.
		cp = omit_leading_whitespace(cp);
	}

	LinuxImage img;
	if (!LinuxImageLoad(image ? image : L"", req_w, req_h, img))
	{
		// Docs: "A ValueError is thrown if ... the image could not be
		// loaded."
		FResultToError(aResultToken, aParam, aParamCount, FR_E_ARG(6), 0);
		return;
	}
	if (img.width <= 0 || img.height <= 0)
	{
		FResultToError(aResultToken, aParam, aParamCount, FR_E_ARG(6), 0);
		return;
	}

	int sw = right - left + 1;
	int sh = bottom - top + 1;
	if (sw <= 0 || sh <= 0)
	{
		// Empty region: nothing can match (upstream BitBlt would fail; the
		// documented result for a not-found search is 0).
		aResultToken.SetValue((__int64)0);
		return;
	}
	// Grab the region.  XGetImage reads the X server's root window;
	// sway's XWayland root has no backing store (XGetImage returns
	// BadMatch), so fall back to capturing through the compositor
	// (wlr-screencopy) when a Wayland display is reachable.
	std::vector<DWORD> screen;
	if (!LinuxScreenGrabRegion(d, left, top, sw, sh, screen))
	{
		// Docs: "An OSError is thrown if an internal function call fails."
		aResultToken.Error(_T("The screen region could not be captured."), _T(""), ErrorPrototype::OS);
		return;
	}

	const std::vector<DWORD> &img_px = img.pixels;
	int iw = img.width, ih = img.height;
	int iw_count = (size_t)iw * ih;
	bool found = false;
	int found_index = 0;
	if (variation < 1) // Exact match (docs: "*0" is exact).
	{
		for (size_t i = 0; i < screen.size(); ++i)
		{
			if (ih > sh - (int)(i / sw) || iw > sw - (int)(i % sw))
				continue; // The image would extend past the region edge.
			bool ok = true;
			for (int j = 0; j < iw_count; ++j)
			{
				DWORD sp = screen[i + (size_t)(j / iw) * sw + (j % iw)];
				DWORD ip = img_px[j];
				if (sp != ip && !(has_trans && ip == trans_color))
				{
					ok = false;
					break;
				}
			}
			if (ok)
			{
				found = true;
				found_index = (int)i;
				break;
			}
		}
	}
	else // Per-channel variation match (docs: "*N" allowed variation).
	{
		for (size_t i = 0; i < screen.size(); ++i)
		{
			if (ih > sh - (int)(i / sw) || iw > sw - (int)(i % sw))
				continue;
			bool ok = true;
			for (int j = 0; j < iw_count; ++j)
			{
				DWORD sp = screen[i + (size_t)(j / iw) * sw + (j % iw)];
				DWORD ip = img_px[j];
				if (has_trans && ip == trans_color)
					continue;
				int dr = (int)((sp >> 16) & 0xFF) - (int)((ip >> 16) & 0xFF);
				int dg = (int)((sp >> 8) & 0xFF) - (int)((ip >> 8) & 0xFF);
				int db = (int)(sp & 0xFF) - (int)(ip & 0xFF);
				if (dr < 0) dr = -dr;
				if (dg < 0) dg = -dg;
				if (db < 0) db = -db;
				if (dr > variation || dg > variation || db > variation)
				{
					ok = false;
					break;
				}
			}
			if (ok)
			{
				found = true;
				found_index = (int)i;
				break;
			}
		}
	}

	if (found)
	{
		// Coordinates relative to the CoordMode origin (docs).
		int fx = left + found_index % sw - origin_x;
		int fy = top + found_index / sw - origin_y;
		if ((out = TokenToOutputVar(*aParam[0])))
			out->Assign((__int64)fx);
		if ((out = TokenToOutputVar(*aParam[1])))
			out->Assign((__int64)fy);
	}
	aResultToken.SetValue((__int64)(found ? 1 : 0));
}
