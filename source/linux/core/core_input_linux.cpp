// Linux X11 input module (round 7): Send family, Mouse*, KeyWait, BlockInput,
// Set*LockState, Install*Hook, Click.
//
// Semantics follow docs-v2 and the upstream implementations in
// keyboard_mouse.cpp / lib/input.cpp / script2.cpp:
//   - All sending is done through the XTEST extension; SendMode/SendEvent/
//     SendInput/SendPlay all deliver the same events on Linux (there is no
//     separate journal/input-stream mechanism), which is documented in the
//     CHECK_REPORT.
//   - Key syntax follows the v2 Send documentation: literal text, modifiers
//     ^ + ! #, {KeyName} with optional "down"/"up" suffix and repeat count,
//     {Text}, {Blind}, {Click ...}, {vkXX}/{scXXX}, mouse buttons and wheels.
//   - GetKeyState()/GetAsyncKeyState() (the Win32 compat shims used by the
//     real GetKeyState/KeyWait implementations) query the X server: physical
//     state via XQueryKeymap, toggle state via Xkb lock modifiers.
#include "../../stdafx.h"
#include "../../script.h"
#include "../../globaldata.h"
#include "../../script_func_impl.h"
#include "core_win_linux.h" // LinuxX11ActiveWindow
#include "core_wayland_linux.h"
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>
#include <X11/extensions/XTest.h>
#include <X11/XKBlib.h>
#include <cwctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <unistd.h>

void ScriptSleep(int aDelay);

// Key-name lookup from core_platform_stubs.cpp (LinuxKeyByName).
bool LinuxLookupKey(LPCTSTR aName, vk_type &aVK, sc_type &aSC);

// Convert script coordinates (per CoordMode Mouse) to screen coordinates.
static void LinuxMouseCoords(Display *d, int aX, int aY, int &aOutX, int &aOutY);

static int LinuxInputXErrorHandler(Display *, XErrorEvent *)
{
	return 0; // Ignore protocol errors (grabs can fail, windows can vanish).
}

static Display *LinuxInputDisplay()
{
	static Display *sDpy = nullptr;
	if (!sDpy)
	{
		sDpy = XOpenDisplay(nullptr);
		if (sDpy)
			XSetErrorHandler(LinuxInputXErrorHandler);
	}
	return sDpy;
}

// ---------------------------------------------------------------------------
// vk -> X keysym -> keycode
// ---------------------------------------------------------------------------

static KeySym LinuxVkToKeysym(vk_type aVK)
{
	switch (aVK)
	{
	case 0x08: return XK_BackSpace;
	case 0x09: return XK_Tab;
	case 0x0D: return XK_Return;
	case 0x10: return XK_Shift_L;
	case 0x11: return XK_Control_L;
	case 0x12: return XK_Alt_L;
	case 0x13: return XK_Pause;
	case 0x14: return XK_Caps_Lock;
	case 0x1B: return XK_Escape;
	case 0x20: return XK_space;
	case 0x21: return XK_Prior;
	case 0x22: return XK_Next;
	case 0x23: return XK_End;
	case 0x24: return XK_Home;
	case 0x25: return XK_Left;
	case 0x26: return XK_Up;
	case 0x27: return XK_Right;
	case 0x28: return XK_Down;
	case 0x2C: return XK_Print;
	case 0x2D: return XK_Insert;
	case 0x2E: return XK_Delete;
	case 0x5B: return XK_Super_L;
	case 0x5C: return XK_Super_R;
	case 0x5D: return XK_Menu;
	case 0x60: return XK_KP_0;
	case 0x61: return XK_KP_1;
	case 0x62: return XK_KP_2;
	case 0x63: return XK_KP_3;
	case 0x64: return XK_KP_4;
	case 0x65: return XK_KP_5;
	case 0x66: return XK_KP_6;
	case 0x67: return XK_KP_7;
	case 0x68: return XK_KP_8;
	case 0x69: return XK_KP_9;
	case 0x6A: return XK_KP_Multiply;
	case 0x6B: return XK_KP_Add;
	case 0x6D: return XK_KP_Subtract;
	case 0x6E: return XK_KP_Decimal;
	case 0x6F: return XK_KP_Divide;
	case 0x90: return XK_Num_Lock;
	case 0x91: return XK_Scroll_Lock;
	case 0xA0: return XK_Shift_L;
	case 0xA1: return XK_Shift_R;
	case 0xA2: return XK_Control_L;
	case 0xA3: return XK_Control_R;
	case 0xA4: return XK_Alt_L;
	case 0xA5: return XK_Alt_R;
	case 0xBA: return (KeySym)0x3b; // ';'
	case 0xBB: return (KeySym)0x3d; // '='
	case 0xBC: return (KeySym)0x2c; // ','
	case 0xBD: return (KeySym)0x2d; // '-'
	case 0xBE: return (KeySym)0x2e; // '.'
	case 0xBF: return (KeySym)0x2f; // '/'
	case 0xC0: return (KeySym)0x60; // '`'
	case 0xDB: return (KeySym)0x5b; // '['
	case 0xDC: return (KeySym)0x5c; // '\\'
	case 0xDD: return (KeySym)0x5d; // ']'
	case 0xDE: return (KeySym)0x27; // '\''
	default:
		if (aVK >= 0x41 && aVK <= 0x5A)
			return (KeySym)aVK; // A-Z (keysym == ASCII).
		if (aVK >= 0x30 && aVK <= 0x39)
			return (KeySym)aVK; // 0-9.
		if (aVK >= 0x70 && aVK <= 0x87)
			return XK_F1 + (aVK - 0x70); // F1-F24.
	}
	return NoSymbol;
}

static KeyCode LinuxKeycodeForVk(Display *d, vk_type aVK)
{
	if (!d)
		return 0; // XKeysymToKeycode requires a live display.
	KeySym ks = LinuxVkToKeysym(aVK);
	if (!ks)
		return 0;
	KeyCode kc = XKeysymToKeycode(d, ks);
	return kc;
}

// Is the key currently down (XQueryKeymap)?  Generic modifiers (vk 0x10/0x11/
// 0x12) check both left and right variants.
static bool LinuxKeyIsDown(Display *d, vk_type aVK)
{
	char keys[32] = {0};
	XQueryKeymap(d, keys);
	auto bit = [&](KeyCode kc) -> bool
	{
		if (!kc)
			return false;
		return (keys[kc / 8] >> (kc % 8)) & 1;
	};
	KeyCode kc = LinuxKeycodeForVk(d, aVK);
	if (bit(kc))
		return true;
	switch (aVK)
	{
	case 0x10: return bit(LinuxKeycodeForVk(d, 0xA0)) || bit(LinuxKeycodeForVk(d, 0xA1));
	case 0x11: return bit(LinuxKeycodeForVk(d, 0xA2)) || bit(LinuxKeycodeForVk(d, 0xA3));
	case 0x12: return bit(LinuxKeycodeForVk(d, 0xA4)) || bit(LinuxKeycodeForVk(d, 0xA5));
	}
	return false;
}

// Toggle state of a lock key via Xkb.
static bool LinuxLockToggled(Display *d, vk_type aVK)
{
	XkbStateRec state;
	if (!XkbGetState(d, XkbUseCoreKbd, &state))
	{
		switch (aVK)
		{
		case 0x14: return (state.mods & LockMask) != 0;    // CapsLock.
		case 0x90: return (state.mods & Mod2Mask) != 0;    // NumLock.
		case 0x91: return (state.mods & Mod3Mask) != 0;    // ScrollLock.
		}
	}
	return false;
}

static unsigned int LinuxLockModifierMask(vk_type aVK)
{
	switch (aVK)
	{
	case 0x14: return LockMask;
	case 0x90: return Mod2Mask;
	case 0x91: return Mod3Mask;
	}
	return 0;
}

// ---------------------------------------------------------------------------
// Win32 compat shims used by ScriptGetKeyState() / GetKeyState() / KeyWait.
// ---------------------------------------------------------------------------

SHORT GetKeyState(int aVK)
{
	Display *d = LinuxInputDisplay();
	if (!d || !aVK)
		return 0;
	SHORT r = 0;
	if (LinuxKeyIsDown(d, (vk_type)aVK))
		r |= 0x8000;
	if (LinuxLockModifierMask((vk_type)aVK) && LinuxLockToggled(d, (vk_type)aVK))
		r |= 0x01;
	return r;
}

SHORT GetAsyncKeyState(int aVK)
{
	Display *d = LinuxInputDisplay();
	if (!d || !aVK)
		return 0;
	return LinuxKeyIsDown(d, (vk_type)aVK) ? (SHORT)0x8000 : 0;
}

// ---------------------------------------------------------------------------
// XTEST helpers (Wayland virtual keyboard/pointer used when no X display)
// ---------------------------------------------------------------------------

static void LinuxFakeKey(Display *d, vk_type aVK, bool aDown)
{
	if (!d && LinuxWaylandKeyEvent((unsigned)aVK, aDown))
		return; // Wayland virtual keyboard.
	if (!d)
		return; // No X display and Wayland injection unavailable: no-op.
	KeyCode kc = LinuxKeycodeForVk(d, aVK);
	if (!kc)
		return;
	XTestFakeKeyEvent(d, kc, aDown ? True : False, CurrentTime);
	XFlush(d);
}

static void LinuxFakeButton(Display *d, unsigned int aButton, bool aDown)
{
	if (!d)
	{
		if (aButton >= 4 && aButton <= 7)
		{
			LinuxWaylandWheelEvent(aButton, aDown);
			return;
		}
		if (LinuxWaylandButtonEvent(aButton, aDown))
			return;
		return; // Unsupported button on Wayland: no-op (documented).
	}
	XTestFakeButtonEvent(d, aButton, aDown ? True : False, CurrentTime);
	XFlush(d);
}

static void LinuxFakeMotion(Display *d, int aX, int aY)
{
	if (!d)
	{
		LinuxWaylandMotionTo(aX, aY);
		return;
	}
	XTestFakeMotionEvent(d, DefaultScreen(d), aX, aY, CurrentTime);
	XFlush(d);
}

// Mouse button number for a vk (1=left, 2=middle, 3=right, 4=x1, 5=x2) —
// X11 uses 1=left, 2=middle, 3=right, 4/5=wheel, 6/7=wheel-h, 8/9=x1/x2.
static unsigned int LinuxMouseButtonForVk(vk_type aVK)
{
	switch (aVK)
	{
	case 0x01: return 1; // LButton.
	case 0x02: return 3; // RButton.
	case 0x04: return 2; // MButton.
	case 0x05: return 8; // XButton1.
	case 0x06: return 9; // XButton2.
	}
	return 0;
}

// Docs use "Left"/"Right"/"Middle"/"XButton1"/"XButton2"/"WheelUp" etc. as
// button names (the key-name table only knows "LButton"/"RButton"/...).
static bool LinuxButtonFromName(const wchar_t *aName, unsigned int &aBtn)
{
	if (!aName || !*aName || !_tcsicmp(aName, L"Left") || !_tcsicmp(aName, L"L"))
	{ aBtn = 1; return true; }
	if (!_tcsicmp(aName, L"Right") || !_tcsicmp(aName, L"R"))
	{ aBtn = 3; return true; }
	if (!_tcsicmp(aName, L"Middle") || !_tcsicmp(aName, L"M"))
	{ aBtn = 2; return true; }
	if (!_tcsicmp(aName, L"XButton1") || !_tcsicmp(aName, L"X1"))
	{ aBtn = 8; return true; }
	if (!_tcsicmp(aName, L"XButton2") || !_tcsicmp(aName, L"X2"))
	{ aBtn = 9; return true; }
	if (!_tcsicmp(aName, L"WheelUp"))
	{ aBtn = 4; return true; }
	if (!_tcsicmp(aName, L"WheelDown"))
	{ aBtn = 5; return true; }
	if (!_tcsicmp(aName, L"WheelLeft"))
	{ aBtn = 6; return true; }
	if (!_tcsicmp(aName, L"WheelRight"))
	{ aBtn = 7; return true; }
	return false;
}

// ---------------------------------------------------------------------------
// Send engine
// ---------------------------------------------------------------------------

// Characters that require the Shift key on a US-style layout.
static bool LinuxCharNeedsShift(wchar_t c)
{
	if (c >= L'A' && c <= L'Z')
		return true;
	return wcschr(L"~!@#$%^&*()_+{}|:\"<>?", c) != nullptr;
}

// The unshifted base character for a shifted character (US layout).
static wchar_t LinuxCharBase(wchar_t c)
{
	if (c >= L'A' && c <= L'Z')
		return (wchar_t)(c - L'A' + L'a');
	switch (c)
	{
	case L'~': return L'`';
	case L'!': return L'1';
	case L'@': return L'2';
	case L'#': return L'3';
	case L'$': return L'4';
	case L'%': return L'5';
	case L'^': return L'6';
	case L'&': return L'7';
	case L'*': return L'8';
	case L'(': return L'9';
	case L')': return L'0';
	case L'_': return L'-';
	case L'+': return L'=';
	case L'{': return L'[';
	case L'}': return L']';
	case L'|': return L'\\';
	case L':': return L';';
	case L'"': return L'\'';
	case L'<': return L',';
	case L'>': return L'.';
	case L'?': return L'/';
	}
	return c;
}

struct LinuxHeldMods
{
	bool ctrl, shift, alt, win;
	LinuxHeldMods() : ctrl(false), shift(false), alt(false), win(false) {}
	void Set(wchar_t c, bool on)
	{
		switch (c)
		{
		case L'^': ctrl = on; break;
		case L'+': shift = on; break;
		case L'!': alt = on; break;
		case L'#': win = on; break;
		}
	}
	bool Any() const { return ctrl || shift || alt || win; }
};

static void LinuxSetMod(Display *d, LinuxHeldMods &aHeld, wchar_t aMod, bool aOn)
{
	switch (aMod)
	{
	case L'^': if (aHeld.ctrl != aOn) { LinuxFakeKey(d, aOn ? 0x11 : 0x11, aOn); aHeld.ctrl = aOn; } break;
	case L'+': if (aHeld.shift != aOn) { LinuxFakeKey(d, aOn ? 0x10 : 0x10, aOn); aHeld.shift = aOn; } break;
	case L'!': if (aHeld.alt != aOn) { LinuxFakeKey(d, aOn ? 0x12 : 0x12, aOn); aHeld.alt = aOn; } break;
	case L'#': if (aHeld.win != aOn) { LinuxFakeKey(d, aOn ? 0x5B : 0x5B, aOn); aHeld.win = aOn; } break;
	}
}

static void LinuxReleaseAllMods(Display *d, LinuxHeldMods &aHeld)
{
	if (aHeld.ctrl) { LinuxFakeKey(d, 0x11, false); aHeld.ctrl = false; }
	if (aHeld.shift) { LinuxFakeKey(d, 0x10, false); aHeld.shift = false; }
	if (aHeld.alt) { LinuxFakeKey(d, 0x12, false); aHeld.alt = false; }
	if (aHeld.win) { LinuxFakeKey(d, 0x5B, false); aHeld.win = false; }
}

// Send one key press+release; count times (for "{Enter 3}").
static void LinuxSendVk(Display *d, vk_type aVK, int aCount)
{
	for (int i = 0; i < aCount; ++i)
	{
		if (unsigned int btn = LinuxMouseButtonForVk(aVK))
			LinuxFakeButton(d, btn, true), LinuxFakeButton(d, btn, false);
		else if (aVK == 0x1000) // WheelUp (synthetic vk used by the brace parser).
			LinuxFakeButton(d, 4, true), LinuxFakeButton(d, 4, false);
		else if (aVK == 0x1001) // WheelDown.
			LinuxFakeButton(d, 5, true), LinuxFakeButton(d, 5, false);
		else if (aVK == 0x1002) // WheelLeft.
			LinuxFakeButton(d, 6, true), LinuxFakeButton(d, 6, false);
		else if (aVK == 0x1003) // WheelRight.
			LinuxFakeButton(d, 7, true), LinuxFakeButton(d, 7, false);
		else
		{
			LinuxFakeKey(d, aVK, true);
			LinuxFakeKey(d, aVK, false);
		}
	}
}

// ASCII char -> Win32 vk for the unshifted base character (US layout).
// aChar is the lower-case base (LinuxCharBase); letters become their
// upper-case vk (0x41-0x5A) so they don't collide with the numpad vks
// (0x60-0x69); digits map 1:1.
static vk_type LinuxCharVk(wchar_t c)
{
	if (c >= L'a' && c <= L'z')
		return (vk_type)(c - L'a' + L'A');
	switch (c)
	{
	case L'`': return 0xC0;
	case L'-': return 0xBD;
	case L'=': return 0xBB;
	case L'[': return 0xDB;
	case L']': return 0xDD;
	case L'\\': return 0xDC;
	case L';': return 0xBA;
	case L'\'': return 0xDE;
	case L',': return 0xBC;
	case L'.': return 0xBE;
	case L'/': return 0xBF;
	}
	return (vk_type)(unsigned)c;
}

// Send a literal character.
static void LinuxSendChar(Display *d, wchar_t aChar, LinuxHeldMods &aHeld)
{
	if (aChar == L'\n' || aChar == L'\r')
	{
		LinuxSendVk(d, 0x0D, 1); // Enter.
		return;
	}
	if (aChar == L'\t')
	{
		LinuxSendVk(d, 0x09, 1);
		return;
	}
	KeySym ks = (KeySym)(unsigned int)aChar;
	if (ks > 0x7E)
		return; // Non-ASCII: no reliable keysym on a US layout.
	// Shifted characters are not directly mapped; use the base keycode.
	wchar_t base = LinuxCharBase(aChar);
	bool need_shift = LinuxCharNeedsShift(aChar);
	bool added_shift = false;
	if (need_shift && !aHeld.shift)
	{
		LinuxFakeKey(d, 0x10, true);
		aHeld.shift = true;
		added_shift = true;
	}
	if (!d)
	{
		// Wayland virtual keyboard (no X display).
		vk_type vk = LinuxCharVk(base);
		LinuxFakeKey(nullptr, vk, true);
		LinuxFakeKey(nullptr, vk, false);
	}
	else
	{
		KeyCode kc = XKeysymToKeycode(d, (KeySym)(unsigned int)base);
		if (kc)
		{
			XTestFakeKeyEvent(d, kc, True, CurrentTime);
			XFlush(d);
			XTestFakeKeyEvent(d, kc, False, CurrentTime);
			XFlush(d);
		}
	}
	if (added_shift)
	{
		LinuxFakeKey(d, 0x10, false);
		aHeld.shift = false;
	}
}

// Parse a {..} token.  aBlind and aRest are updated for {Blind}/{Text}.
static void LinuxSendBrace(Display *d, const std::wstring &aToken, LinuxHeldMods &aHeld
	, bool &aBlind, bool &aTextMode)
{
	// Split into words.
	std::vector<std::wstring> words;
	size_t pos = 0;
	while (pos <= aToken.size())
	{
		while (pos < aToken.size() && (aToken[pos] == L' ' || aToken[pos] == L'\t'))
			++pos;
		if (pos >= aToken.size())
			break;
		size_t e = aToken.find_first_of(L" \t", pos);
		if (e == std::wstring::npos)
		{
			words.push_back(aToken.substr(pos));
			break;
		}
		words.push_back(aToken.substr(pos, e - pos));
		pos = e + 1;
	}
	if (words.empty())
		return;
	const std::wstring &w0 = words[0];
	if (w0 == L"Text" || w0 == L"text" || w0 == L"TEXT")
	{
		aTextMode = true; // The remainder of the string is sent literally.
		return;
	}
	if (w0 == L"Blind" || w0 == L"blind" || w0 == L"BLIND")
	{
		aBlind = true;
		return;
	}
	if (w0 == L"Click" || w0 == L"click" || w0 == L"CLICK")
	{
		// {Click [x y] [Button] [Down|Up]} — move and click at the current
		// pointer position.
		int x = -1, y = -1, count = 1;
		std::wstring button = L"Left";
		bool down = false, up = false, has_coords = false;
		size_t i = 1;
		while (i < words.size())
		{
			const std::wstring &w = words[i];
			if (iswdigit(w[0]) || w[0] == L'-')
			{
				int v = (int)wcstol(w.c_str(), nullptr, 10);
				if (!has_coords) { x = v; has_coords = true; }
				else if (y < 0) y = v;
			}
			else if (w == L"Down") down = true;
			else if (w == L"Up") up = true;
			else button = w;
			++i;
		}
		if (x >= 0 || y >= 0)
		{
			// {Click} coordinates are screen-relative (CoordMode ToolTip? the
			// docs say Click coordinates follow CoordMode Mouse; keep simple
			// and use the same conversion as MouseMove).
			int cx, cy;
			LinuxMouseCoords(d, x < 0 ? 0 : x, y < 0 ? 0 : y, cx, cy);
			LinuxFakeMotion(d, cx, cy);
		}
		unsigned int btn = 0;
		if (!LinuxButtonFromName(button.c_str(), btn))
		{
			vk_type bvk;
			sc_type bsc;
			if (LinuxLookupKey(button.c_str(), bvk, bsc))
				btn = LinuxMouseButtonForVk(bvk);
		}
		if (btn)
		{
			if (down) LinuxFakeButton(d, btn, true);
			else if (up) LinuxFakeButton(d, btn, false);
			else for (int n = 0; n < count; ++n) { LinuxFakeButton(d, btn, true); LinuxFakeButton(d, btn, false); }
		}
		return;
	}
	// Key name + optional "down"/"up" + optional repeat count.
	std::wstring name = w0;
	bool down = false, up = false;
	int count = 1;
	if (words.size() > 1)
	{
		if (words[1] == L"down" || words[1] == L"Down") down = true;
		else if (words[1] == L"up" || words[1] == L"Up") up = true;
		else count = (int)wcstol(words[1].c_str(), nullptr, 10);
	}
	if (words.size() > 2)
		count = (int)wcstol(words[2].c_str(), nullptr, 10);
	vk_type vk;
	sc_type sc;
	if (!LinuxLookupKey(name.c_str(), vk, sc))
		return;
	// Modifier keys inside braces update the held state.
	if (vk == 0x10 || vk == 0x11 || vk == 0x12 || vk == 0x5B || vk == 0x5C)
	{
		bool on = !up; // "{Ctrl}" = press+release; "{Ctrl down}" holds.
		if (down) on = true;
		if (!down && !up)
		{
			LinuxFakeKey(d, vk, true);
			LinuxFakeKey(d, vk, false);
			return;
		}
		// Hold/release: update held state so SendText etc. can see it.
		LinuxFakeKey(d, vk, on);
		(void)on;
		// Track in aHeld so modifier prefixes don't double-handle.
		if (vk == 0x11) aHeld.ctrl = down;
		else if (vk == 0x10) aHeld.shift = down;
		else if (vk == 0x12) aHeld.alt = down;
		else if (vk == 0x5B || vk == 0x5C) aHeld.win = down;
		return;
	}
	if (down || up)
	{
		for (int n = 0; n < count; ++n)
			LinuxFakeKey(d, vk, down);
		return;
	}
	LinuxSendVk(d, vk, count);
}

// Convert script coordinates (per CoordMode Mouse) to screen coordinates.
static void LinuxMouseCoords(Display *d, int aX, int aY, int &aOutX, int &aOutY);

// The main Send engine.
static void LinuxSendKeys(Display *d, const wchar_t *aKeys)
{
	LinuxHeldMods held;
	bool blind = false;
	const wchar_t *p = aKeys;
	while (*p)
	{
		if (held.Any() && !blind)
		{
			// Release prefix modifiers between key combinations (Send
			// semantics: "^a^b" holds Ctrl only during each key).
			LinuxReleaseAllMods(d, held);
		}
		if (*p == L'{')
		{
			const wchar_t *end = wcschr(p + 1, L'}');
			if (!end)
				break;
			std::wstring token(p + 1, end - p - 1);
			bool text_mode = false;
			LinuxSendBrace(d, token, held, blind, text_mode);
			p = end + 1;
			if (text_mode)
			{
				// The rest of the string is sent literally.
				for (; *p; ++p)
					LinuxSendChar(d, *p, held);
				break;
			}
			continue;
		}
		if (*p == L'^' || *p == L'+' || *p == L'!' || *p == L'#')
		{
			// Collect consecutive modifier prefixes, then apply to the next key.
			std::vector<wchar_t> mods;
			while (*p == L'^' || *p == L'+' || *p == L'!' || *p == L'#')
				mods.push_back(*p++);
			for (auto m : mods)
				LinuxSetMod(d, held, m, true);
			// Next token: a single char or a {..} group.
			if (*p == L'{')
			{
				const wchar_t *end = wcschr(p + 1, L'}');
				if (!end)
					break;
				std::wstring token(p + 1, end - p - 1);
				bool text_mode = false;
				LinuxSendBrace(d, token, held, blind, text_mode);
				p = end + 1;
				if (text_mode)
				{
					for (; *p; ++p)
						LinuxSendChar(d, *p, held);
					break;
				}
			}
			else if (*p)
			{
				LinuxSendChar(d, *p, held);
				++p;
			}
			for (auto m : mods)
				LinuxSetMod(d, held, m, false);
			continue;
		}
		LinuxSendChar(d, *p, held);
		++p;
	}
	LinuxReleaseAllMods(d, held);
}

// ---------------------------------------------------------------------------
// Coordinate conversion (CoordMode Mouse: Screen = absolute, otherwise
// relative to the active window's top-left corner).
// ---------------------------------------------------------------------------

static void LinuxMouseCoords(Display *d, int aX, int aY, int &aOutX, int &aOutY)
{
	unsigned mode = (g && ((g->CoordMode >> COORD_MODE_MOUSE) & COORD_MODE_MASK) == COORD_MODE_SCREEN)
		? COORD_MODE_SCREEN : COORD_MODE_CLIENT;
	if (mode == COORD_MODE_SCREEN)
	{
		aOutX = aX;
		aOutY = aY;
		return;
	}
	// Client/Window: relative to the active window's top-left corner.
	Window active = LinuxX11ActiveWindow();
	int wx = 0, wy = 0;
	if (active)
	{
		Window child;
		XTranslateCoordinates(d, active, DefaultRootWindow(d), 0, 0, &wx, &wy, &child);
	}
	aOutX = aX + wx;
	aOutY = aY + wy;
}

// ---------------------------------------------------------------------------
// Send / SendEvent / SendInput / SendPlay / SendText
// ---------------------------------------------------------------------------

static void LinuxSendWrapper(ResultToken &aResultToken, ExprTokenType *aParam[], int aParamCount, bool aRaw)
{
	Display *d = LinuxInputDisplay();
	if (!d && !LinuxWaylandActive())
	{
		aResultToken.Error(_T("No X display or Wayland display is available."), _T(""), ErrorPrototype::OS);
		return;
	}
	TCHAR keys_buf[65536];
	LPTSTR keys = aParamCount > 0 ? TokenToString(*aParam[0], keys_buf, nullptr) : nullptr;
	if (!keys)
		keys = keys_buf;
	if (aRaw)
	{
		LinuxHeldMods held;
		for (const wchar_t *p = keys; *p; ++p)
			LinuxSendChar(d, *p, held);
		LinuxReleaseAllMods(d, held);
	}
	else
		LinuxSendKeys(d, keys);
}

BIF_DECL(BIF_Linux_Send)      { LinuxSendWrapper(aResultToken, aParam, aParamCount, false); }
BIF_DECL(BIF_Linux_SendEvent) { LinuxSendWrapper(aResultToken, aParam, aParamCount, false); }
BIF_DECL(BIF_Linux_SendInput) { LinuxSendWrapper(aResultToken, aParam, aParamCount, false); }
BIF_DECL(BIF_Linux_SendPlay)  { LinuxSendWrapper(aResultToken, aParam, aParamCount, false); }
BIF_DECL(BIF_Linux_SendText)  { LinuxSendWrapper(aResultToken, aParam, aParamCount, true); }

// ---------------------------------------------------------------------------
// Accessors for the control module (core_ctrl_linux.cpp): ControlClick and
// ControlSend reuse the XTEST send engine.
// ---------------------------------------------------------------------------

void LinuxFakeButtonEvent(Display *d, unsigned int aButton, bool aDown)
{
	LinuxFakeButton(d, aButton, aDown);
}

void LinuxFakeMotionEvent(Display *d, int aX, int aY)
{
	LinuxFakeMotion(d, aX, aY);
}

void LinuxSendKeysString(Display *d, const wchar_t *aKeys)
{
	LinuxSendKeys(d, aKeys);
}

void LinuxSendCharsString(Display *d, const wchar_t *aKeys)
{
	LinuxHeldMods held;
	for (const wchar_t *p = aKeys; *p; ++p)
		LinuxSendChar(d, *p, held);
	LinuxReleaseAllMods(d, held);
}

bool LinuxButtonFromNameEx(const wchar_t *aName, unsigned int &aBtn)
{
	return LinuxButtonFromName(aName, aBtn);
}

KeyCode LinuxKeycodeForVkEx(Display *d, vk_type aVK)
{
	return LinuxKeycodeForVk(d, aVK);
}

// ---------------------------------------------------------------------------
// MouseMove / MouseClick / MouseClickDrag / MouseGetPos
// ---------------------------------------------------------------------------

// Query the pointer position (root coordinates).
static void LinuxQueryPointer(Display *d, int &aX, int &aY)
{
	int wx, wy;
	unsigned int mask;
	Window root_ret, child_ret;
	XQueryPointer(d, DefaultRootWindow(d), &root_ret, &child_ret, &aX, &aY, &wx, &wy, &mask);
}

BIF_DECL(BIF_Linux_MouseMove)
{
	Display *d = LinuxInputDisplay();
	if (!d && !LinuxWaylandActive())
	{
		aResultToken.Error(_T("No X display or Wayland display is available."), _T(""), ErrorPrototype::OS);
		return;
	}
	int x = (int)TokenToInt64(*aParam[0]);
	int y = (int)TokenToInt64(*aParam[1]);
	// Relative mode ("R"): relative to the current position.
	if (aParamCount > 3 && !ParamIndexIsOmitted(3))
	{
		TCHAR r_buf[16];
		LPTSTR rel = TokenToString(*aParam[3], r_buf, nullptr);
		if (rel && !_tcsicmp(rel, _T("R")))
		{
			if (d)
			{
				int cx, cy;
				LinuxQueryPointer(d, cx, cy);
				x += cx;
				y += cy;
			}
			else
			{
				// Wayland: relative motion by the given amounts.
				LinuxWaylandMotionEvent(x, y);
				return;
			}
		}
	}
	if (d)
	{
		int sx, sy;
		LinuxMouseCoords(d, x, y, sx, sy);
		LinuxFakeMotion(d, sx, sy);
	}
	else
		LinuxFakeMotion(nullptr, x, y); // Absolute intent via tracked position.
}

BIF_DECL(BIF_Linux_MouseClick)
{
	Display *d = LinuxInputDisplay();
	if (!d && !LinuxWaylandActive())
	{
		aResultToken.Error(_T("No X display or Wayland display is available."), _T(""), ErrorPrototype::OS);
		return;
	}
	TCHAR btn_buf[32], duo_buf[32], rel_buf[16];
	btn_buf[0] = L'\0'; duo_buf[0] = L'\0'; rel_buf[0] = L'\0';
	LPTSTR button = aParamCount > 0 && !ParamIndexIsOmitted(0) ? TokenToString(*aParam[0], btn_buf, nullptr) : nullptr;
	if (!button || !*button)
		button = const_cast<LPTSTR>(_T("Left")); // Docs: default button is Left.
	int x = -1, y = -1;
	if (aParamCount > 1 && !ParamIndexIsOmitted(1))
		x = (int)TokenToInt64(*aParam[1]);
	if (aParamCount > 2 && !ParamIndexIsOmitted(2))
		y = (int)TokenToInt64(*aParam[2]);
	int count = aParamCount > 3 && !ParamIndexIsOmitted(3) ? (int)TokenToInt64(*aParam[3]) : 1;
	LPTSTR down_up = aParamCount > 5 && !ParamIndexIsOmitted(5) ? TokenToString(*aParam[5], duo_buf, nullptr) : nullptr;
	LPTSTR relative = aParamCount > 6 && !ParamIndexIsOmitted(6) ? TokenToString(*aParam[6], rel_buf, nullptr) : nullptr;
	if (count < 1)
		count = 1;
	if (x >= 0 && y >= 0)
	{
		if (relative && !_tcsicmp(relative, _T("R")))
		{
			if (d)
			{
				int cx, cy;
				LinuxQueryPointer(d, cx, cy);
				x += cx;
				y += cy;
			}
			else
			{
				// Wayland: relative motion by the given amounts.
				LinuxWaylandMotionEvent(x, y);
				x = -1;
			}
		}
		if (x >= 0)
		{
			if (d)
			{
				int sx, sy;
				LinuxMouseCoords(d, x, y, sx, sy);
				LinuxFakeMotion(d, sx, sy);
			}
			else
				LinuxFakeMotion(nullptr, x, y);
		}
	}
	vk_type bvk;
	sc_type bsc;
	unsigned int btn = 0;
	if (!LinuxButtonFromName(button, btn))
	{
		if (LinuxLookupKey(button, bvk, bsc))
			btn = LinuxMouseButtonForVk(bvk);
	}
	if (!btn)
	{
		aResultToken.Error(_T("Invalid button name."), _T(""), ErrorPrototype::Value);
		return;
	}
	bool hold = false, release = false;
	if (down_up)
	{
		if (!_tcsicmp(down_up, _T("D"))) hold = true;
		else if (!_tcsicmp(down_up, _T("U"))) release = true;
	}
	if (!btn)
	{
		aResultToken.Error(_T("Invalid button name."), _T(""), ErrorPrototype::Value);
		return;
	}
	if (hold)
		LinuxFakeButton(d, btn, true);
	else if (release)
		LinuxFakeButton(d, btn, false);
	else
		for (int i = 0; i < count; ++i)
		{
			LinuxFakeButton(d, btn, true);
			LinuxFakeButton(d, btn, false);
		}
}

BIF_DECL(BIF_Linux_MouseClickDrag)
{
	Display *d = LinuxInputDisplay();
	if (!d && !LinuxWaylandActive())
	{
		aResultToken.Error(_T("No X display or Wayland display is available."), _T(""), ErrorPrototype::OS);
		return;
	}
	TCHAR btn_buf[32], rel_buf[16];
	btn_buf[0] = L'\0'; rel_buf[0] = L'\0';
	LPTSTR button = aParamCount > 0 && !ParamIndexIsOmitted(0) ? TokenToString(*aParam[0], btn_buf, nullptr) : nullptr;
	if (!button || !*button)
		button = const_cast<LPTSTR>(_T("Left"));
	int x1 = aParamCount > 1 ? (int)TokenToInt64(*aParam[1]) : 0;
	int y1 = aParamCount > 2 ? (int)TokenToInt64(*aParam[2]) : 0;
	int x2 = (int)TokenToInt64(*aParam[3]);
	int y2 = (int)TokenToInt64(*aParam[4]);
	LPTSTR relative = aParamCount > 6 && !ParamIndexIsOmitted(6) ? TokenToString(*aParam[6], rel_buf, nullptr) : nullptr;
	unsigned int btn = 0;
	if (!LinuxButtonFromName(button, btn))
	{
		vk_type bvk;
		sc_type bsc;
		if (LinuxLookupKey(button, bvk, bsc))
			btn = LinuxMouseButtonForVk(bvk);
	}
	if (!btn)
	{
		aResultToken.Error(_T("Invalid button name."), _T(""), ErrorPrototype::Value);
		return;
	}
	if (relative && !_tcsicmp(relative, _T("R")) && d) // Wayland: cannot query the pointer.
	{
		int cx, cy;
		LinuxQueryPointer(d, cx, cy);
		x1 += cx; y1 += cy;
		x2 += cx; y2 += cy;
	}
	if (d)
	{
		int sx1, sy1, sx2, sy2;
		LinuxMouseCoords(d, x1, y1, sx1, sy1);
		LinuxMouseCoords(d, x2, y2, sx2, sy2);
		LinuxFakeMotion(d, sx1, sy1);
		LinuxFakeButton(d, btn, true);
		LinuxFakeMotion(d, sx2, sy2);
		LinuxFakeButton(d, btn, false);
	}
	else
	{
		LinuxFakeMotion(nullptr, x1, y1);
		LinuxFakeButton(nullptr, btn, true);
		LinuxFakeMotion(nullptr, x2, y2);
		LinuxFakeButton(nullptr, btn, false);
	}
}

BIF_DECL(BIF_Linux_MouseGetPos)
{
	Display *d = LinuxInputDisplay();
	if (!d)
	{
		aResultToken.Error(_T("No X display is available."), _T(""), ErrorPrototype::OS);
		return;
	}
	int root_x = 0, root_y = 0, win_x = 0, win_y = 0;
	unsigned int mask = 0;
	Window root_ret, child_ret;
	XQueryPointer(d, DefaultRootWindow(d), &root_ret, &child_ret, &root_x, &root_y, &win_x, &win_y, &mask);
	// Docs: Flag=1 returns screen coordinates; otherwise CoordMode Mouse.
	bool screen = false;
	if (aParamCount > 4 && !ParamIndexIsOmitted(4))
		screen = TokenToInt64(*aParam[4]) != 0;
	unsigned mode = (g && ((g->CoordMode >> COORD_MODE_MOUSE) & COORD_MODE_MASK) == COORD_MODE_SCREEN)
		? COORD_MODE_SCREEN : COORD_MODE_CLIENT;
	int out_x = root_x, out_y = root_y;
	if (!screen && mode != COORD_MODE_SCREEN)
	{
		Window active = LinuxX11ActiveWindow();
		int wx = 0, wy = 0;
		if (active)
		{
			Window child;
			XTranslateCoordinates(d, active, DefaultRootWindow(d), 0, 0, &wx, &wy, &child);
		}
		out_x = root_x - wx;
		out_y = root_y - wy;
	}
	// NOTE: aParam[] only has aParamCount valid entries — never read beyond.
	Var *out;
	if (aParamCount > 0 && (out = TokenToOutputVar(*aParam[0]))) out->Assign((__int64)out_x);
	if (aParamCount > 1 && (out = TokenToOutputVar(*aParam[1]))) out->Assign((__int64)out_y);
	if (aParamCount > 2 && (out = TokenToOutputVar(*aParam[2]))) out->Assign((__int64)(ULONG_PTR)child_ret);
	if (aParamCount > 3 && (out = TokenToOutputVar(*aParam[3]))) out->Assign(_T("")); // No controls on X11.
}

// ---------------------------------------------------------------------------
// KeyWait (docs: waits until the key is up, or down with the "D" option)
// ---------------------------------------------------------------------------

BIF_DECL(BIF_Linux_KeyWait)
{
	Display *d = LinuxInputDisplay();
	if (!d)
	{
		aResultToken.Error(_T("No X display is available."), _T(""), ErrorPrototype::OS);
		return;
	}
	TCHAR key_buf[128], opt_buf[64];
	LPTSTR key = TokenToString(*aParam[0], key_buf, nullptr);
	LPTSTR opts = aParamCount > 1 && !ParamIndexIsOmitted(1) ? TokenToString(*aParam[1], opt_buf, nullptr) : nullptr;
	vk_type vk;
	sc_type sc;
	if (!LinuxLookupKey(key ? key : key_buf, vk, sc))
	{
		aResultToken.Error(_T("Invalid key name."), _T(""), ErrorPrototype::Value);
		return;
	}
	bool wait_down = false;
	if (opts)
	{
		for (const wchar_t *p = opts; *p; ++p)
		{
			if (ctoupper(*p) == L'D')
				wait_down = true;
		}
	}
	for (;;)
	{
		bool down = LinuxKeyIsDown(d, vk);
		if (down == wait_down)
		{
			aResultToken.SetValue((__int64)1);
			return;
		}
		ScriptSleep(20);
	}
}

// ---------------------------------------------------------------------------
// BlockInput (X grabs)
//
// Docs (v2): three independent modes --
//   OnOff: On/1 blocks all user input; Off/0 re-enables it.
//   SendMouse: Send/Mouse/SendAndMouse block the *user's* input while send /
//     mouse functions are in progress; Default turns these two modes off but
//     does NOT change the OnOff blocking state.
//   MouseMove: MouseMove blocks cursor movement; MouseMoveOff re-enables it.
// Windows implements this with input hooks; on Linux the closest equivalent
// is X keyboard/pointer grabs (owner_events=False, async).  While a grab is
// active, hardware input is delivered to this client and swallowed (other
// clients see nothing), and XTEST-simulated input (our Send/Mouse*) is still
// generated -- matching "user input is blocked but AutoHotkey can simulate
// keystrokes and mouse clicks".  Grabs are released automatically when the
// script exits (server-side), matching "Input is automatically re-enabled
// when the script closes".  Unlike Windows, Send/Mouse mode blocking stays
// active until Default/Off rather than only during each send (no hook system).
// ---------------------------------------------------------------------------

static bool sLinuxBlockOnOff = false;     // OnOff mode (On/1 vs Off/0).
static bool sLinuxBlockSend = false;      // SendMouse: Send word.
static bool sLinuxBlockMouse = false;     // SendMouse: Mouse word.
static bool sLinuxBlockMouseMove = false; // MouseMove mode.

static void LinuxBlockInputApply(Display *d)
{
	Window root = DefaultRootWindow(d);
	bool kbd = sLinuxBlockOnOff || sLinuxBlockSend;
	bool ptr = sLinuxBlockOnOff || sLinuxBlockMouse || sLinuxBlockMouseMove;
	if (kbd)
		XGrabKeyboard(d, root, False, GrabModeAsync, GrabModeAsync, CurrentTime);
	else
		XUngrabKeyboard(d, CurrentTime);
	if (ptr)
		XGrabPointer(d, root, False, ButtonPressMask | ButtonReleaseMask | PointerMotionMask
			, GrabModeAsync, GrabModeAsync, None, None, CurrentTime);
	else
		XUngrabPointer(d, CurrentTime);
	XSync(d, False);
}

BIF_DECL(BIF_Linux_BlockInput)
{
	Display *d = LinuxInputDisplay();
	if (!d)
	{
		aResultToken.Error(_T("No X display is available."), _T(""), ErrorPrototype::OS);
		return;
	}
	TCHAR mode_buf[64];
	LPTSTR mode = TokenToString(*aParam[0], mode_buf, nullptr);
	if (!_tcsicmp(mode, _T("On")) || !_tcsicmp(mode, _T("1")))
		sLinuxBlockOnOff = true;
	else if (!_tcsicmp(mode, _T("Off")) || !_tcsicmp(mode, _T("0")))
		sLinuxBlockOnOff = false;
	else if (!_tcsicmp(mode, _T("Send")))
		sLinuxBlockSend = true;
	else if (!_tcsicmp(mode, _T("Mouse")))
		sLinuxBlockMouse = true;
	else if (!_tcsicmp(mode, _T("SendAndMouse")))
		sLinuxBlockSend = sLinuxBlockMouse = true;
	else if (!_tcsicmp(mode, _T("Default")))
		sLinuxBlockSend = sLinuxBlockMouse = false; // Docs: does not touch OnOff.
	else if (!_tcsicmp(mode, _T("MouseMove")))
		sLinuxBlockMouseMove = true;
	else if (!_tcsicmp(mode, _T("MouseMoveOff")))
		sLinuxBlockMouseMove = false;
	else
	{
		aResultToken.Error(_T("Invalid mode."), _T(""), ErrorPrototype::Value);
		return;
	}
	LinuxBlockInputApply(d);
}

// ---------------------------------------------------------------------------
// InstallKeybdHook / InstallMouseHook (no hook system on Linux; stored flags)
// ---------------------------------------------------------------------------

static bool sLinuxKeybdHook = false, sLinuxMouseHook = false;

static void LinuxInstallHook(ExprTokenType *aParam[], int aParamCount, bool aKeybd)
{
	bool install = true;
	if (aParamCount > 0 && !ParamIndexIsOmitted(0))
		install = TokenToBOOL(*aParam[0]);
	if (aKeybd)
		sLinuxKeybdHook = install;
	else
		sLinuxMouseHook = install;
}

BIF_DECL(BIF_Linux_InstallKeybdHook) { LinuxInstallHook(aParam, aParamCount, true); }
BIF_DECL(BIF_Linux_InstallMouseHook) { LinuxInstallHook(aParam, aParamCount, false); }

// ---------------------------------------------------------------------------
// SetCapsLockState / SetNumLockState / SetScrollLockState (Xkb)
// ---------------------------------------------------------------------------

static void LinuxSetLockState(ResultToken &aResultToken, ExprTokenType *aParam[], int aParamCount, vk_type aVK)
{
	Display *d = LinuxInputDisplay();
	if (!d)
	{
		aResultToken.Error(_T("No X display is available."), _T(""), ErrorPrototype::OS);
		return;
	}
	unsigned int mask = LinuxLockModifierMask(aVK);
	if (!mask)
		return;
	bool on;
	TCHAR state_buf[32];
	LPTSTR state = aParamCount > 0 && !ParamIndexIsOmitted(0) ? TokenToString(*aParam[0], state_buf, nullptr) : nullptr;
	if (!state || !*state || !_tcsicmp(state, _T("Toggle")) || !_tcscmp(state, _T("-1")))
	{
		// Toggle (-1 / "Toggle" / omitted): flip the current state.
		XkbStateRec st;
		XkbGetState(d, XkbUseCoreKbd, &st);
		on = (st.mods & mask) == 0;
	}
	else if (!_tcsicmp(state, _T("On")) || !_tcscmp(state, _T("1")) || !_tcsicmp(state, _T("AlwaysOn")))
		on = true;
	else if (!_tcsicmp(state, _T("Off")) || !_tcscmp(state, _T("0")) || !_tcsicmp(state, _T("AlwaysOff")))
		on = false;
	else
	{
		aResultToken.Error(_T("Invalid state."), _T(""), ErrorPrototype::Value);
		return;
	}
	XkbLockModifiers(d, XkbUseCoreKbd, mask, on ? mask : 0);
	XSync(d, False);
}

BIF_DECL(BIF_Linux_SetCapsLockState)  { LinuxSetLockState(aResultToken, aParam, aParamCount, 0x14); }
BIF_DECL(BIF_Linux_SetNumLockState)   { LinuxSetLockState(aResultToken, aParam, aParamCount, 0x90); }
BIF_DECL(BIF_Linux_SetScrollLockState){ LinuxSetLockState(aResultToken, aParam, aParamCount, 0x91); }

// ---------------------------------------------------------------------------
// Click (g_BIF entry; registered as BIF_Click)
// ---------------------------------------------------------------------------

BIF_DECL(BIF_Click)
{
	Display *d = LinuxInputDisplay();
	if (!d)
	{
		aResultToken.Error(_T("No X display is available."), _T(""), ErrorPrototype::OS);
		return;
	}
	TCHAR keys_buf[4096];
	keys_buf[0] = L'\0';
	LPTSTR keys = aParamCount > 0 && !ParamIndexIsOmitted(0) ? TokenToString(*aParam[0], keys_buf, nullptr) : nullptr;
	std::wstring text(keys ? keys : L"");
	int x = -1, y = -1;
	bool has_coords = false;
	std::vector<std::wstring> actions; // "Left", "Right", ..., "Down", "Up", "WheelUp", ...
	size_t pos = 0;
	while (pos <= text.size())
	{
		while (pos < text.size() && (text[pos] == L' ' || text[pos] == L'\t'))
			++pos;
		if (pos >= text.size())
			break;
		size_t e = text.find_first_of(L" \t", pos);
		std::wstring w = e == std::wstring::npos ? text.substr(pos) : text.substr(pos, e - pos);
		pos = e == std::wstring::npos ? text.size() + 1 : e + 1;
		if (iswdigit(w[0]) || (w[0] == L'-' && w.size() > 1))
		{
			int v = (int)wcstol(w.c_str(), nullptr, 10);
			if (!has_coords) { x = v; has_coords = true; }
			else if (y < 0) y = v;
		}
		else
			actions.push_back(w);
	}
	// Docs: coordinates move the mouse first; then the actions run (a bare
	// click with no actions = Left-click at the current position).
	if (has_coords)
	{
		int sx, sy;
		LinuxMouseCoords(d, x < 0 ? 0 : x, y < 0 ? 0 : y, sx, sy);
		LinuxFakeMotion(d, sx, sy);
	}
	if (actions.empty())
		actions.push_back(L"Left");
	for (auto &a : actions)
	{
		if (a == L"Down")
		{
			LinuxFakeButton(d, 1, true); // Left down (docs: Down without a button = Left).
			continue;
		}
		if (a == L"Up")
		{
			LinuxFakeButton(d, 1, false);
			continue;
		}
		vk_type bvk;
		sc_type bsc;
		unsigned int btn = 0;
		if (!LinuxButtonFromName(a.c_str(), btn))
		{
			if (LinuxLookupKey(a.c_str(), bvk, bsc))
				btn = LinuxMouseButtonForVk(bvk);
		}
		if (!btn)
		{
			aResultToken.Error(_T("Invalid Click item."), _T(""), ErrorPrototype::Value);
			return;
		}
		LinuxFakeButton(d, btn, true);
		LinuxFakeButton(d, btn, false);
	}
}

