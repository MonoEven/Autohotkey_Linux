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
#include "core_clipboard_linux.h" // LinuxClipboardGetText/SetText (paste path)
#include "core_uinput_linux.h" // uinput injection lane (check0820)
#include "core_hotkey_linux.h" // LinuxSendInputTrack/Clear (SendInput self-suppression)
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>
#include <X11/extensions/XTest.h>
#include <X11/XKBlib.h>
#include <cwctype>
#include <cwchar>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <unistd.h>
#include <strings.h> // strcasecmp (AHK_WAYLAND_PASTE switch).

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

// uinput relative-motion anchor (kept by LinuxFakeMotion for the lane).
static int sUinputLastX = 0;
static int sUinputLastY = 0;

static void LinuxFakeKey(Display *d, vk_type aVK, bool aDown)
{
	if (!d && LinuxWaylandKeyEvent((unsigned)aVK, aDown))
		return; // Wayland virtual keyboard.
	if (!d && LinuxUinputKeyEvent((unsigned)aVK, aDown))
		return; // uinput fallback (GNOME/KWin lack the virtual-keyboard
			// protocol; check0820 direction-B).
	if (!d)
		return; // No X display and no injection lane: no-op.
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
			if (LinuxWaylandWheelEvent(aButton, aDown))
				return;
			if (LinuxUinputWheelEvent(aButton, aDown))
				return;
			return; // Unsupported button on Wayland: no-op (documented).
		}
		if (LinuxWaylandButtonEvent(aButton, aDown))
			return;
		if (LinuxUinputButtonEvent(aButton, aDown))
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
		LinuxUinputMotionEvent(aX - sUinputLastX, aY - sUinputLastY);
		sUinputLastX = aX;
		sUinputLastY = aY;
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
	case 0x9C: return 6; // WheelLeft.
	case 0x9D: return 7; // WheelRight.
	case 0x9E: return 5; // WheelDown.
	case 0x9F: return 4; // WheelUp.
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

// ---------------------------------------------------------------------------
// Send-mode threading + key-delay pacing (check_detail0821 §2-B / R2 S1+S2)
// ---------------------------------------------------------------------------
// The low-level tap functions (LinuxSendVk / LinuxSendChar / ...) read the
// mode the current Send/SendEvent/SendInput/SendPlay call runs under:
//   LSE_EVENT: per-key XTestFakeKeyEvent paced by SetKeyDelay (inter-key) and
//              PressDuration (down-up).  An explicit delay value is honored;
//              -1 (default = "system speed") keeps the previous fast behavior
//              because X11 has no OS-paced journal to defer to.
//   LSE_INPUT: batch semantics -- never sleep (SetKeyDelay does not affect
//              SendInput, docs) and every key is marked self-injected so the
//              script's own grab/capture engine drops the returned copy
//              (Windows unloads the hook during SendInput; the X events come
//              back asynchronously, so an in-flight flag is not enough -- the
//              marks are consumed when the events actually arrive).
//   LSE_PLAY:  LSE_EVENT + the SetKeyDelay ,, Play variants.  X11 has no
//              journal, so the injection depth is the same as Event; this is
//              a documented platform adaptation (parity tier "adapted").
//   LSE_TEXT:  SendText -- raw literal delivery, no pacing, no suppression
//              (keeps the historical fast behavior).
enum { LSE_EVENT = 0, LSE_INPUT, LSE_PLAY, LSE_TEXT };
static int s_send_mode = LSE_EVENT;
// True only for an explicit SendInput() call: it is the only path that
// implements the "unload the hook during SendInput" self-suppression
// (check_detail0821 §2-B).  `Send` resolves by SendMode (default "Input")
// but keeps the historical XTEST trigger semantics -- its events still
// activate the script's own grabs (as the doc-check suite and many macro
// scripts rely on).  Documented deviation: SendMode("Input") + `Send`
// does not self-suppress on Linux; use SendInput() for that semantic.
static bool s_send_explicit_input = false;

static void LinuxSetSendMode(int aMode, bool aExplicitSendInput)
{
	switch (aMode)
	{
	case LSE_INPUT: case LSE_PLAY: case LSE_TEXT: s_send_mode = aMode; break;
	default: s_send_mode = LSE_EVENT; break;
	}
	s_send_explicit_input = aExplicitSendInput;
}

static int LinuxModeKeyDelayMs()
{
	switch (s_send_mode)
	{
	case LSE_INPUT: case LSE_TEXT: return 0;
	case LSE_PLAY: return g->KeyDelayPlay > -1 ? g->KeyDelayPlay : 0;
	default: return g->KeyDelay > -1 ? g->KeyDelay : 0;
	}
}

static int LinuxModePressDurationMs()
{
	switch (s_send_mode)
	{
	case LSE_INPUT: case LSE_TEXT: return 0;
	case LSE_PLAY: return g->PressDurationPlay > -1 ? g->PressDurationPlay : 0;
	default: return g->PressDuration > -1 ? g->PressDuration : 0;
	}
}

// True only in an explicit SendInput batch: the batch's events must not
// re-fire this process's own hotkeys/hotstrings.
static bool LinuxModeSuppressSelf()
{
	return s_send_mode == LSE_INPUT && s_send_explicit_input;
}

// Send one key phase and (in SendInput mode) mark it self-injected so the
// script's own grab does not re-fire it.
static void LinuxTapKey(Display *d, vk_type aVK, KeyCode aTrackKc, bool aDown)
{
	LinuxFakeKey(d, aVK, aDown);
	if (aTrackKc && LinuxModeSuppressSelf())
		LinuxSendInputTrack((unsigned int)aTrackKc, aDown);
}

// Send one key press+release; count times (for "{Enter 3}").
static void LinuxSendVk(Display *d, vk_type aVK, int aCount)
{
	int press_ms = LinuxModePressDurationMs();
	int gap_ms = LinuxModeKeyDelayMs();
	// Keycode used only for the SendInput self-suppression mark (X11 path).
	KeyCode track_kc = 0;
	if (LinuxModeSuppressSelf() && d && !LinuxMouseButtonForVk(aVK)
		&& aVK != 0x1000 && aVK != 0x1001 && aVK != 0x1002 && aVK != 0x1003)
		track_kc = LinuxKeycodeForVk(d, aVK);
	for (int i = 0; i < aCount; ++i)
	{
		bool is_key = true;
		if (unsigned int btn = LinuxMouseButtonForVk(aVK))
			LinuxFakeButton(d, btn, true), is_key = false;
		else if (aVK == 0x1000) // WheelUp (synthetic vk used by the brace parser).
			LinuxFakeButton(d, 4, true), is_key = false;
		else if (aVK == 0x1001) // WheelDown.
			LinuxFakeButton(d, 5, true), is_key = false;
		else if (aVK == 0x1002) // WheelLeft.
			LinuxFakeButton(d, 6, true), is_key = false;
		else if (aVK == 0x1003) // WheelRight.
			LinuxFakeButton(d, 7, true), is_key = false;
		else
			LinuxTapKey(d, aVK, track_kc, true);
		if (press_ms > 0)
			usleep((useconds_t)press_ms * 1000);
		if (is_key)
			LinuxTapKey(d, aVK, track_kc, false);
		else if (unsigned int btn = LinuxMouseButtonForVk(aVK))
			LinuxFakeButton(d, btn, false);
		else if (aVK == 0x1000)
			LinuxFakeButton(d, 4, false);
		else if (aVK == 0x1001)
			LinuxFakeButton(d, 5, false);
		else if (aVK == 0x1002)
			LinuxFakeButton(d, 6, false);
		else if (aVK == 0x1003)
			LinuxFakeButton(d, 7, false);
		if (gap_ms > 0 && i + 1 < aCount)
			usleep((useconds_t)gap_ms * 1000);
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

// ---------------------------------------------------------------------------
// Unicode characters (non-ASCII) in SendText/Send
// ---------------------------------------------------------------------------
//
// X11/XWayland: any Unicode character is delivered as a key event whose
// keycode maps to the character's keysym.  Latin-1 characters (U+0000-U+00FF)
// use their direct keysym value (e.g. U+00E9 == XK_eacute); other code points
// use the X11 Unicode keysym range (0x01000000 | code point), which modern
// toolkits (GTK/Qt) accept.  When the layout already binds the keysym to a
// keycode (e.g. é on a French layout) that keycode is reused; otherwise a
// spare keycode (currently bound to no keysym) is temporarily remapped, the
// key events are sent, and the mapping is reverted.  The X server delivers
// MappingNotify before the resulting KeyPress/KeyRelease on every client
// connection (FIFO per connection), so a client that refreshes its cached
// map on MappingNotify resolves the temporary mapping correctly.  This is the
// same approach xdotool uses for `type` of non-ASCII text.
//
// Pure Wayland: virtual-keyboard key events carry only keycodes (no Unicode
// keysyms), so a run of text containing non-ASCII characters is delivered via
// the controlled clipboard-paste path (set clipboard -> Ctrl+V -> restore).
// On compositors without the virtual-keyboard protocol (e.g. GNOME) there is
// no injection path at all: the send fails with a clear error instead of
// silently dropping characters.

// X11 keysym for a Unicode code point (exported; the hotstring capture
// engine uses it for Unicode replacements).
KeySym LinuxCharToKeySym(wchar_t aChar)
{
	unsigned int cp = (unsigned int)aChar;
	if (cp < 0x100)
		return (KeySym)cp; // Latin-1: keysym value == code point.
	return (KeySym)(0x01000000u | cp); // Unicode keysym range.
}

// A keycode whose level-0 keysym is aKeysym (so it can be sent unshifted).
static KeyCode LinuxFindKeycodeForKeySym(Display *d, KeySym aKeysym)
{
	if (!d)
		return 0;
	int min_kc = 0, max_kc = 0;
	XDisplayKeycodes(d, &min_kc, &max_kc);
	int ks_per_kc = 0;
	KeySym *map = XGetKeyboardMapping(d, (KeyCode)min_kc, max_kc - min_kc + 1, &ks_per_kc);
	if (!map)
		return 0;
	KeyCode found = 0;
	for (int kc = min_kc; kc <= max_kc && !found; ++kc)
	{
		KeySym *row = map + (size_t)(kc - min_kc) * ks_per_kc;
		if (ks_per_kc == 1 && row[0] == aKeysym) // Unmodified key.
			found = (KeyCode)kc;
		else if (ks_per_kc > 1 && row[0] == aKeysym) // Level 0 match.
			found = (KeyCode)kc;
	}
	XFree(map);
	return found;
}

// A keycode currently bound to no keysym at all (so it can be borrowed for a
// transient Unicode mapping without disturbing real keys).  Skips keycodes
// that participate in the XKB modifier map (e.g. NumLock bindings).
static KeyCode LinuxSpareKeycode(Display *d)
{
	if (!d)
		return 0;
	int min_kc = 0, max_kc = 0;
	XDisplayKeycodes(d, &min_kc, &max_kc);
	int ks_per_kc = 0;
	KeySym *map = XGetKeyboardMapping(d, (KeyCode)min_kc, max_kc - min_kc + 1, &ks_per_kc);
	if (!map)
		return 0;
	// Modifier-map mask computed via Xkb (a spare must not be a modifier).
	XkbDescPtr xkb = XkbGetMap(d, XkbKeySymsMask | XkbModifierMapMask, XkbUseCoreKbd);
	KeyCode spare = 0;
	for (int kc = max_kc; kc >= min_kc && !spare; --kc)
	{
		KeySym *row = map + (size_t)(kc - min_kc) * ks_per_kc;
		bool empty = true;
		for (int l = 0; l < ks_per_kc; ++l)
			if (row[l] != NoSymbol) { empty = false; break; }
		if (!empty)
			continue;
		if (xkb && kc < 256 && xkb->map && xkb->map->modmap && xkb->map->modmap[kc])
			continue; // Used as a modifier key.
		spare = (KeyCode)kc;
	}
	if (xkb)
		XkbFreeClientMap(xkb, 0, TRUE);
	XFree(map);
	return spare;
}

// Borrow-and-borrow bookkeeping for Unicode keycode transmission.
//
// The key events and the temporary mapping change are both sent while the
// mapping is installed, but the *consumers* resolve the keycode at their own
// pace: an X client that processes events late (e.g. our own input-capture
// engine, which drains grabbed events from its dedicated hotkey connection
// on the main loop) may translate the keycode AFTER the mapping has been
// reverted and see NoSymbol.  This process owns the borrows, so it keeps a
// process-local LOG of every transient borrow: the capture engine resolves
// borrowed keycodes against this log (in FIFO order -- the order it
// consumes the key events matches the order the borrows were made), then
// falls back to the server maps for real keyboards.
//
// The mapping change itself is server-wide, so a *second* AHK process on the
// same X server must never remap the same spare keycode while the first one
// is in a borrow window.  All borrows are therefore serialized through an X
// selection (AHK_UNICODE_BORROW): taking ownership asserts the lease, and
// ownership is automatically reclaimed by the server when a client dies
// (crash / kill -9), so no stale lease can block the second process.
static std::vector<std::pair<KeyCode, KeySym>> sBorrowLog;
static DWORD sLastBorrowMs = 0; // See LinuxBorrowRecent() below.
#define GS_BORROW_LOG_MAX 128

static Display *sLeaseDpy = nullptr;
static Window sLeaseWin = 0;
static Atom sLeaseSel = 0;
static int sLeaseDepth = 0;   // Reentrant borrows within one process.

// Acquire the cross-process borrow lease.  Waits (bounded) when another
// process is mid-borrow; returns false when the lease cannot be taken
// within the timeout (the caller then reports the char as undeliverable).
static bool LinuxBorrowLeaseAcquire(Display *d, int aTimeoutMs)
{
	if (!d)
		return true; // No X display: no server-wide mapping to protect.
	if (sLeaseDepth > 0)
	{
		// Already held by this process.  A nested borrow can come from a
		// DIFFERENT connection to the same server (the capture engine runs
		// on the hotkey display while a Send is in progress); ownership is
		// server-side, so the lease stays with our window either way.
		++sLeaseDepth;
		return true;
	}
	if (sLeaseWin == 0)
	{
		sLeaseWin = XCreateSimpleWindow(d, DefaultRootWindow(d), -100, -100, 1, 1, 0, 0, 0);
		sLeaseSel = XInternAtom(d, "AHK_UNICODE_BORROW_LEASE", False);
	}
	int waited = 0;
	for (;;)
	{
		XSync(d, False);
		if (XGetSelectionOwner(d, sLeaseSel) == None)
		{
			XSetSelectionOwner(d, sLeaseSel, sLeaseWin, CurrentTime);
			XSync(d, False);
			if (XGetSelectionOwner(d, sLeaseSel) == sLeaseWin)
			{
				sLeaseDpy = d;
				sLeaseDepth = 1;
				return true;
			}
			// Lost the race (another client took ownership in between);
			// retry unless the timeout has expired.
		}
		if (waited >= aTimeoutMs)
			return false;
		int step = aTimeoutMs - waited;
		if (step > 5)
			step = 5;
		usleep((unsigned)step * 1000);
		waited += step;
	}
}

static void LinuxBorrowLeaseRelease(Display *d)
{
	if (!sLeaseDepth)
		return;
	if (--sLeaseDepth > 0)
		return; // Nested borrow still inside; keep the lease.
	XSetSelectionOwner(d, sLeaseSel, None, CurrentTime); // Release the lease.
	XSync(d, False);
	sLeaseDpy = nullptr;
}

// Borrow-or-find a keycode for aKeysym.  Returns 0 when neither an existing
// mapping nor a spare keycode is available.  When a spare is remapped,
// *aRemapped is set to true and the caller must call LinuxUnicodeRestore()
// after the key events have been sent (the mapping change is server-wide, so
// the borrow window must be as short as possible).
KeyCode LinuxUnicodeKeycode(Display *d, KeySym aKeysym, bool &aRemapped)
{
	aRemapped = false;
	if (KeyCode kc = LinuxFindKeycodeForKeySym(d, aKeysym))
		return kc;
	KeyCode spare = LinuxSpareKeycode(d);
	if (!spare)
		return 0;
	if (!aKeysym)
		return 0;
	// Server-wide remap: take the cross-process lease first so no second
	// AHK can remap the same spare keycode inside this borrow window
	// (check0820 P1).  The window is short but a busy other process may still
	// hold the lease; then this char is reported undeliverable rather than
	// clobbering the other process's mapping.
	if (!LinuxBorrowLeaseAcquire(d, 150))
		return 0;
	XChangeKeyboardMapping(d, spare, 1, &aKeysym, 1);
	XSync(d, False); // Server must process the remap before the key events.
	// Log the borrow (FIFO; the capture engine consumes it in event order).
	if (sBorrowLog.size() >= GS_BORROW_LOG_MAX)
		sBorrowLog.erase(sBorrowLog.begin());
	sBorrowLog.push_back(std::make_pair(spare, aKeysym));
	sLastBorrowMs = GetTickCount();
	aRemapped = true;
	return spare;
}

// Revert a borrowed keycode after LinuxUnicodeKeycode() remapped it.  The
// process-local borrow log is deliberately KEPT: the capture engine may
// still need to resolve the (already reverted) keycode, and the log entry is
// consumed (FIFO) by LinuxConsumeBorrowedKeySym() when the event is
// processed.
void LinuxUnicodeRestore(Display *d, KeyCode aKeycode)
{
	if (!d || !aKeycode)
		return;
	KeySym none = NoSymbol;
	XChangeKeyboardMapping(d, aKeycode, 1, &none, 1);
	XSync(d, False);
	// Release the cross-process lease (check0820 P1): the borrow window is
	// over, so the next AHK process may remap its own Unicode keysym now.
	LinuxBorrowLeaseRelease(d);
}

// Consume the OLDEST borrow-log entry for aKeycode and return its keysym
// (NoSymbol when the keycode was never borrowed).  The capture engine calls
// this when it processes a key event whose keycode was transiently remapped:
// the borrow order equals the event order, so consuming FIFO maps each
// event to the keysym that was installed when it was generated.
KeySym LinuxConsumeBorrowedKeySym(KeyCode aKeycode)
{
	for (size_t i = 0; i < sBorrowLog.size(); ++i)
		if (sBorrowLog[i].first == aKeycode)
		{
			KeySym ks = sBorrowLog[i].second;
			sBorrowLog.erase(sBorrowLog.begin() + i);
			return ks;
		}
	return NoSymbol;
}

// True while a Unicode borrow was made recently (within the keep-window).
// The hotkey backend uses this to skip the full grab rebuild that a
// MappingNotify triggers: borrows intentionally broadcast MappingNotify but
// only retarget a spare keycode (modifier slots and grab targets are
// unaffected), and rebuilding ~2000 capture grabs per borrow floods the X
// connection (check0819 round-34).
bool LinuxBorrowRecent()
{
	DWORD now = GetTickCount();
	return now >= sLastBorrowMs && now - sLastBorrowMs < 500;
}

// The last non-ASCII character that could not be delivered (for the error).
static wchar_t sLastUnsendable = 0;

static void LinuxSendChar(Display *d, wchar_t aChar, LinuxHeldMods &aHeld); // fwd

// Send one non-ASCII character through the X11 path (per-character keysym
// transmission).  Returns true when the char was delivered.
static bool LinuxSendCharUnicode(Display *d, wchar_t aChar)
{
	KeySym ks = LinuxCharToKeySym(aChar);
	bool remapped = false;
	KeyCode kc = LinuxUnicodeKeycode(d, ks, remapped);
	if (!kc)
	{
		sLastUnsendable = aChar;
		return false;
	}
	XTestFakeKeyEvent(d, kc, True, CurrentTime);
	if (LinuxModeSuppressSelf())
		LinuxSendInputTrack((unsigned int)kc, true);
	XFlush(d);
	int press_ms = LinuxModePressDurationMs();
	int gap_ms = LinuxModeKeyDelayMs();
	if (press_ms > 0)
		usleep((useconds_t)press_ms * 1000);
	XTestFakeKeyEvent(d, kc, False, CurrentTime);
	if (LinuxModeSuppressSelf())
		LinuxSendInputTrack((unsigned int)kc, false);
	XFlush(d);
	if (gap_ms > 0)
		usleep((useconds_t)gap_ms * 1000);
	if (remapped)
	{
		// Race (round-34, observed in the doc-check): a client that
		// refreshes its keymap on MappingNotify (XRefreshKeyboardMapping
		// -> XGetKeyboardMapping) can see the REVERTED mapping if the
		// revert wins the race, resolving the keycode to NoSymbol and
		// dropping the character (both xkeycap and the rename capture
		// engine hit this).  Give clients a short window to process the
		// key events before the borrow is returned; the mapping change is
		// server-wide, so the window is kept as small as practical.
		XSync(d, False);
		usleep(30000);
		LinuxUnicodeRestore(d, kc);
	}
	return true;
}

// Paste a literal run via the clipboard (pure Wayland fallback for text
// containing non-ASCII characters).  Returns true on success.
static bool LinuxSendRunPaste(const wchar_t *aStart, const wchar_t *aEnd)
{
	// check0820 P1 (hardened): the fallback is a compatibility lane, so it
	// can be disabled entirely and the owner can be warned about the brief
	// clipboard handover (password managers, sensitive input).
	static bool sPasteWarned = false;
	if (const char *v = getenv("AHK_WAYLAND_PASTE"))
	{
		if (!strcasecmp(v, "0") || !strcasecmp(v, "off")
			|| !strcasecmp(v, "false") || !strcasecmp(v, "no"))
		{
			sLastUnsendable = aStart < aEnd ? *aStart : L'?';
			return false;
		}
	}
	if (!sPasteWarned)
	{
		fprintf(stderr,
			"AHK warning: SendText on a native-Wayland session without a "
			"virtual-keyboard protocol uses the clipboard as a paste channel "
			"(text is placed in the system clipboard for a moment and then "
			"restored). Set AHK_WAYLAND_PASTE=0 to disable this fallback.\n");
		sPasteWarned = true;
	}

	std::wstring saved;
	bool had = LinuxClipboardGetText(saved);
	std::wstring run(aStart, aEnd);
	if (!LinuxClipboardPasteSet(run, saved))
		return false;
	// Ctrl+V via the virtual keyboard (wlroots compositors deliver these to
	// the focused surface).  The focused app reads the clipboard when it
	// processes the paste key; wait (bounded) until the app actually asks
	// for our offer before restoring, then restore the previous clipboard --
	// an originally-empty clipboard comes back empty (check0820 P1).
	LinuxFakeKey(nullptr, 0x11, true);  // Control_L.
	LinuxFakeKey(nullptr, 0x56, true);  // V.
	LinuxFakeKey(nullptr, 0x56, false);
	LinuxFakeKey(nullptr, 0x11, false);
	// Wait (bounded) until the target app actually pulls our offer; the
	// deadline must stay small enough that headless docs (sway never asks)
	// still restore in time (check0820 P1).  Slow apps/Electron/remote can
	// raise it via AHK_WAYLAND_PASTE_TIMEOUT_MS.
	int wait_ms = 800;
	if (const char *t = getenv("AHK_WAYLAND_PASTE_TIMEOUT_MS")) {
		int v = atoi(t);
		if (v > 0 && v <= 10000)
			wait_ms = v;
	}
	LinuxClipboardPasteWaitConsumed(wait_ms);
	LinuxClipboardPasteRestore(had);
	return true;
}

// Send a run of literal text ([aStart, aEnd)) to the focused window.  ASCII
// (or any text when an X display is present, or while modifiers are held) is
// sent per-character; a non-ASCII run on a pure-Wayland session uses the
// clipboard-paste path.  Returns false when a character cannot be delivered
// (sLastUnsendable names the offending character).
static bool LinuxSendLiteralRun(Display *d, const wchar_t *aStart, const wchar_t *aEnd
	, LinuxHeldMods &aHeld)
{
	sLastUnsendable = 0;
	bool has_non_ascii = false;
	for (const wchar_t *q = aStart; q < aEnd; ++q)
		if (*q > 0x7E) { has_non_ascii = true; break; }
	if (!has_non_ascii || d || aHeld.Any())
	{
		for (const wchar_t *q = aStart; q < aEnd; ++q)
		{
			LinuxSendChar(d, *q, aHeld);
			if (sLastUnsendable)
				return false; // A Unicode char could not be delivered.
		}
		return true;
	}
	// Pure Wayland with non-ASCII text and no modifiers held.  The paste
	// fallback needs a key-injection lane for its Ctrl+V pair: the virtual
	// keyboard, or the uinput lane (GNOME/KWin lack the protocol).
	if (LinuxWaylandActive()
		&& (LinuxWaylandCanInjectKeys() || LinuxUinputInjectionAvailable()))
		return LinuxSendRunPaste(aStart, aEnd);
	for (const wchar_t *q = aStart; q < aEnd; ++q)
		if (*q > 0x7E) { sLastUnsendable = *q; break; }
	return false;
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
	{
		// Non-ASCII: Unicode keysym transmission on X11; on a pure-Wayland
		// session the run-level clipboard-paste fallback handles it (a bare
		// call with no run context reports failure instead of dropping).
		if (d)
			LinuxSendCharUnicode(d, aChar);
		else
			sLastUnsendable = aChar;
		return;
	}
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
		int press_ms = LinuxModePressDurationMs();
		int gap_ms = LinuxModeKeyDelayMs();
		if (press_ms > 0)
			usleep((useconds_t)press_ms * 1000);
		LinuxFakeKey(nullptr, vk, false);
		if (gap_ms > 0)
			usleep((useconds_t)gap_ms * 1000);
	}
	else
	{
		KeyCode kc = XKeysymToKeycode(d, (KeySym)(unsigned int)base);
		if (kc)
		{
			XTestFakeKeyEvent(d, kc, True, CurrentTime);
			if (LinuxModeSuppressSelf())
				LinuxSendInputTrack((unsigned int)kc, true);
			XFlush(d);
			int press_ms = LinuxModePressDurationMs();
			int gap_ms = LinuxModeKeyDelayMs();
			if (press_ms > 0)
				usleep((useconds_t)press_ms * 1000);
			XTestFakeKeyEvent(d, kc, False, CurrentTime);
			if (LinuxModeSuppressSelf())
				LinuxSendInputTrack((unsigned int)kc, false);
			XFlush(d);
			if (gap_ms > 0)
				usleep((useconds_t)gap_ms * 1000);
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

// The main Send engine.  Returns false when a literal run could not be
// delivered (sLastUnsendable names the offending character).
static bool LinuxSendKeys(Display *d, const wchar_t *aKeys)
{
	LinuxHeldMods held;
	bool blind = false;
	bool ok = true;
	const wchar_t *p = aKeys;
	while (*p && ok)
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
				ok = LinuxSendLiteralRun(d, p, p + wcslen(p), held);
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
					ok = LinuxSendLiteralRun(d, p, p + wcslen(p), held);
					break;
				}
			}
			else if (*p)
			{
				LinuxSendChar(d, *p, held);
				if (sLastUnsendable)
					ok = false;
				++p;
			}
			for (auto m : mods)
				LinuxSetMod(d, held, m, false);
			continue;
		}
		// A run of literal text: send it as a unit (this is where a pure-
		// Wayland non-ASCII run switches to the clipboard-paste path).
		const wchar_t *run = p;
		while (*p && *p != L'{' && *p != L'^' && *p != L'+' && *p != L'!' && *p != L'#')
			++p;
		ok = LinuxSendLiteralRun(d, run, p, held);
	}
	LinuxReleaseAllMods(d, held);
	return ok;
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

// aMode is one of LSE_*; aExplicitSendInput marks an explicit SendInput()
// call (the only path with self-suppression).  BIF_Linux_Send resolves the
// current SendMode itself.
static void LinuxSendWrapper(ResultToken &aResultToken, ExprTokenType *aParam[], int aParamCount, bool aRaw, int aMode, bool aExplicitSendInput)
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
	int saved_mode = s_send_mode;
	bool saved_explicit = s_send_explicit_input;
	LinuxSetSendMode(aMode, aExplicitSendInput);
	if (s_send_mode == LSE_INPUT && aExplicitSendInput)
		LinuxSendInputClear(); // Fresh suppression marks for this batch.
	sLastUnsendable = 0;
	bool ok = true;
	if (aRaw)
	{
		LinuxHeldMods held;
		ok = LinuxSendLiteralRun(d, keys, keys + wcslen(keys), held);
		LinuxReleaseAllMods(d, held);
	}
	else
		ok = LinuxSendKeys(d, keys);
	s_send_mode = saved_mode;
	s_send_explicit_input = saved_explicit;
	if (!ok && sLastUnsendable)
	{
		TCHAR buf[256];
		_tcsncpy(buf, _T("Non-ASCII character U+"), _countof(buf));
		TCHAR hex[32];
		sntprintf(hex, _countof(hex), _T("%04X"), (unsigned)sLastUnsendable);
		_tcsncat(buf, hex, _countof(buf));
		_tcsncat(buf, _T(" cannot be sent on this session (no X display and the "
			"compositor provides no virtual keyboard); use X11/XWayland, or "
			"check that the Wayland compositor exposes a virtual keyboard)."),
			_countof(buf));
		aResultToken.Error(buf, _T(""), ErrorPrototype::OS);
	}
}

// Resolve the current SendMode to an LSE_* tap mode.  SM_INPUT_FALLBACK_TO_PLAY
// and SM_INPUT both map to LSE_INPUT here (there is no X11 journal to fall
// back to, and SM_PLAY is requested explicitly).
static int LinuxResolveSendMode()
{
	switch (g->SendMode)
	{
	case SM_PLAY: return LSE_PLAY;
	case SM_EVENT: return LSE_EVENT;
	default: return LSE_INPUT;
	}
}

BIF_DECL(BIF_Linux_Send)      { LinuxSendWrapper(aResultToken, aParam, aParamCount, false, LinuxResolveSendMode(), false); }
BIF_DECL(BIF_Linux_SendEvent) { LinuxSendWrapper(aResultToken, aParam, aParamCount, false, LSE_EVENT, false); }
BIF_DECL(BIF_Linux_SendInput) { LinuxSendWrapper(aResultToken, aParam, aParamCount, false, LSE_INPUT, true); }
BIF_DECL(BIF_Linux_SendPlay)  { LinuxSendWrapper(aResultToken, aParam, aParamCount, false, LSE_PLAY, false); }
BIF_DECL(BIF_Linux_SendText)  { LinuxSendWrapper(aResultToken, aParam, aParamCount, true, LSE_TEXT, false); }

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
	LinuxSendLiteralRun(d, aKeys, aKeys + wcslen(aKeys), held);
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

