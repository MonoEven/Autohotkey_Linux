// Linux Wayland display layer (round 20).
//
// The port runs on X11 when a display is available (which also covers
// XWayland: with both DISPLAY and WAYLAND_DISPLAY set, X11 is preferred,
// i.e. the XWayland fallback).  Without X11 but with a connectable
// WAYLAND_DISPLAY, this layer provides:
//   - input simulation through the zwp_virtual_keyboard_v1 and
//     zwlr_virtual_pointer_manager_v1 protocols (the de-facto standards
//     implemented by wlroots-based compositors such as sway; the pointer
//     motion is relative, since Wayland clients cannot position the
//     pointer absolutely without output enumeration -- documented);
//   - script-owned top-level windows (used by ToolTip) as xdg-shell
//     toplevels;
//   - the main-loop poll/dispatch hook so that xdg configure events are
//     acknowledged while the script runs.
//
// Window enumeration, hotkeys, pixel/monitor access and the rest of the
// X11 surface are not available on Wayland (a fundamental Wayland
// limitation: clients cannot see other clients' windows); the respective
// modules raise clear errors.  Everything else (timers, dialogs with the
// stdin fallback, file/string functions) works unchanged.

#include "../../stdafx.h"
#include "../../script.h"
#include "../../globaldata.h"
#include "../../script_func_impl.h"
#include "core_wayland_linux.h"
#include "core_win_linux.h"
#include "core_clipboard_linux.h"
#include <wayland-client.h>
#include <xdg-shell-protocol.h>
#include <virtual-keyboard-unstable-v1-protocol.h>
#include <wlr-virtual-pointer-unstable-v1-protocol.h>
#include <wlr-screencopy-unstable-v1-protocol.h>
#include <xkbcommon/xkbcommon.h>
#include <linux/input-event-codes.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>
#include <poll.h>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <cstdio>
#include <cwchar>
#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// State
// ---------------------------------------------------------------------------

struct LinuxWaylandWindow
{
	wl_surface *surface;
	xdg_surface *xdg;
	xdg_toplevel *toplevel;
	wl_buffer *buffer = nullptr; // 1x1 shm buffer so the compositor maps us.
	wl_shm_pool *pool = nullptr;
	int shm_fd = -1;
	bool buffer_attached = false;
};

struct LinuxWaylandState
{
	wl_display *display = nullptr;
	wl_registry *registry = nullptr;
	wl_compositor *compositor = nullptr;
	wl_shm *shm = nullptr;
	wl_seat *seat = nullptr;
	xdg_wm_base *wm_base = nullptr;
	zwp_virtual_keyboard_manager_v1 *vkbd_mgr = nullptr;
	zwlr_virtual_pointer_manager_v1 *vptr_mgr = nullptr;
	zwp_virtual_keyboard_v1 *vkbd = nullptr;
	zwlr_virtual_pointer_v1 *vptr = nullptr;
	bool vptr_used = false;
	unsigned int vkbd_mods_depressed = 0; // Tracked for the modifiers request.
	bool active = false;
	bool connect_failed = false;
	std::vector<LinuxWaylandWindow *> windows;
};

static LinuxWaylandState &LinuxWl()
{
	static LinuxWaylandState s;
	return s;
}

// ---------------------------------------------------------------------------
// Registry
// ---------------------------------------------------------------------------

static void LinuxWlRegistryGlobal(void *aData, wl_registry *aReg, uint32_t aName
	, const char *aIface, uint32_t aVersion)
{
	LinuxWaylandState &s = LinuxWl();
	if (!strcmp(aIface, "wl_compositor"))
		s.compositor = (wl_compositor *)wl_registry_bind(aReg, aName, &wl_compositor_interface, 4);
	else if (!strcmp(aIface, "wl_shm"))
		s.shm = (wl_shm *)wl_registry_bind(aReg, aName, &wl_shm_interface, 1);
	else if (!strcmp(aIface, "wl_seat"))
		s.seat = (wl_seat *)wl_registry_bind(aReg, aName, &wl_seat_interface, 1);
	else if (!strcmp(aIface, "xdg_wm_base"))
		s.wm_base = (xdg_wm_base *)wl_registry_bind(aReg, aName, &xdg_wm_base_interface, 1);
	else if (!strcmp(aIface, "zwp_virtual_keyboard_manager_v1"))
		s.vkbd_mgr = (zwp_virtual_keyboard_manager_v1 *)wl_registry_bind(aReg, aName
			, &zwp_virtual_keyboard_manager_v1_interface, 1);
	else if (!strcmp(aIface, "zwlr_virtual_pointer_manager_v1"))
		s.vptr_mgr = (zwlr_virtual_pointer_manager_v1 *)wl_registry_bind(aReg, aName
			, &zwlr_virtual_pointer_manager_v1_interface, 1);
	else
		LinuxClipboardWaylandRegistry(aData, aReg, aName, aIface, aVersion);
}

static void LinuxWlRegistryRemove(void *aData, wl_registry *aReg, uint32_t aName)
{
}

static const wl_registry_listener sRegistryListener = {
	LinuxWlRegistryGlobal,
	LinuxWlRegistryRemove
};

// ---------------------------------------------------------------------------
// Seat event logging (test facility).  With AHK_WL_EVLOG=<file>, key and
// pointer-button events delivered to surfaces of this client (i.e. the
// script-owned xdg windows) are appended as "k:<evdev-keycode>:<down|up>"
// and "b:<button-code>:<down|up>" lines, so automated suites can verify
// that virtual-keyboard/pointer input reaches the compositor and is
// dispatched (the same idea as the AHK_*_AUTOCLOSE_MS dialog hooks).
// ---------------------------------------------------------------------------

static FILE *LinuxWlEvlog()
{
	static FILE *sLog = nullptr;
	if (!sLog)
	{
		const char *path = getenv("AHK_WL_EVLOG");
		if (path && *path)
			sLog = fopen(path, "a");
	}
	return sLog;
}

static void LinuxWlKeyboardKeymap(void *aData, wl_keyboard *aKb, uint32_t aFormat, int32_t aFd, uint32_t aSize)
{
	if (aFd >= 0)
		close(aFd);
}

static void LinuxWlKeyboardKey(void *aData, wl_keyboard *aKb, uint32_t aSerial
	, uint32_t aTime, uint32_t aKey, uint32_t aState)
{
	if (FILE *f = LinuxWlEvlog())
		fprintf(f, "k:%u:%s\n", aKey, aState == WL_KEYBOARD_KEY_STATE_PRESSED ? "down" : "up");
}

static void LinuxWlKeyboardEnter(void *aData, wl_keyboard *aKb, uint32_t aSerial
	, wl_surface *aSurface, wl_array *aKeys)
{
	if (FILE *f = LinuxWlEvlog())
		fprintf(f, "kbd-enter\n");
}

static void LinuxWlKeyboardLeave(void *aData, wl_keyboard *aKb, uint32_t aSerial, wl_surface *aSurface)
{
	if (FILE *f = LinuxWlEvlog())
		fprintf(f, "kbd-leave\n");
}

static void LinuxWlKeyboardModifiers(void *aData, wl_keyboard *aKb, uint32_t aSerial
	, uint32_t aModsDepressed, uint32_t aModsLatched, uint32_t aModsLocked, uint32_t aGroup)
{
}

static void LinuxWlKeyboardRepeatInfo(void *aData, wl_keyboard *aKb, int32_t aRate, int32_t aDelay)
{
}

static const wl_keyboard_listener sKeyboardListener = {
	LinuxWlKeyboardKeymap, // keymap
	LinuxWlKeyboardEnter,
	LinuxWlKeyboardLeave,
	LinuxWlKeyboardModifiers,
	LinuxWlKeyboardKey,
	LinuxWlKeyboardRepeatInfo
};

static void LinuxWlPointerButton(void *aData, wl_pointer *aPtr, uint32_t aSerial
	, uint32_t aTime, uint32_t aButton, uint32_t aState)
{
	if (FILE *f = LinuxWlEvlog())
		fprintf(f, "b:%u:%s\n", aButton, aState == WL_POINTER_BUTTON_STATE_PRESSED ? "down" : "up");
}

static void LinuxWlPointerEnter(void *aData, wl_pointer *aPtr, uint32_t aSerial
	, wl_surface *aSurface, wl_fixed_t aSX, wl_fixed_t aSY)
{
	if (FILE *f = LinuxWlEvlog())
		fprintf(f, "ptr-enter\n");
}

static void LinuxWlPointerLeave(void *aData, wl_pointer *aPtr, uint32_t aSerial, wl_surface *aSurface)
{
}

static void LinuxWlPointerMotion(void *aData, wl_pointer *aPtr, uint32_t aTime, wl_fixed_t aSX, wl_fixed_t aSY)
{
}

static void LinuxWlPointerAxis(void *aData, wl_pointer *aPtr, uint32_t aTime, uint32_t aAxis, wl_fixed_t aValue)
{
}

static void LinuxWlPointerFrame(void *aData, wl_pointer *aPtr)
{
}

static void LinuxWlPointerAxisSource(void *aData, wl_pointer *aPtr, uint32_t aAxisSource)
{
}

static void LinuxWlPointerAxisStop(void *aData, wl_pointer *aPtr, uint32_t aTime, uint32_t aAxis)
{
}

static void LinuxWlPointerAxisDiscrete(void *aData, wl_pointer *aPtr, uint32_t aAxis, int32_t aDiscrete)
{
}

static const wl_pointer_listener sPointerListener = {
	LinuxWlPointerEnter,
	LinuxWlPointerLeave,
	LinuxWlPointerMotion,
	LinuxWlPointerButton,
	LinuxWlPointerAxis,
	LinuxWlPointerFrame,
	LinuxWlPointerAxisSource,
	LinuxWlPointerAxisStop,
	LinuxWlPointerAxisDiscrete,
};

// ---------------------------------------------------------------------------
// xdg_wm_base / ping
// ---------------------------------------------------------------------------

static void LinuxWlWmBasePing(void *aData, xdg_wm_base *aBase, uint32_t aSerial)
{
	xdg_wm_base_pong(aBase, aSerial);
}

static const xdg_wm_base_listener sWmBaseListener = {
	LinuxWlWmBasePing
};

// ---------------------------------------------------------------------------
// Connection
// ---------------------------------------------------------------------------

// True when an X display is currently in use (the preferred backend; also
// covers XWayland).
static bool LinuxWlXInUse()
{
	return LinuxX11Display() != nullptr;
}

bool LinuxWaylandShouldUse()
{
	if (LinuxWlXInUse())
		return false;
	if (LinuxWl().active || LinuxWl().connect_failed)
		return false; // Already decided.
	// Only attempt a connection when a Wayland display is plausibly
	// present (WAYLAND_DISPLAY set, or the default socket exists).
	const char *wl = getenv("WAYLAND_DISPLAY");
	if (wl && *wl)
		return true;
	const char *rt = getenv("XDG_RUNTIME_DIR");
	if (rt && *rt)
	{
		std::string sock = std::string(rt) + "/wayland-0";
		if (access(sock.c_str(), R_OK) == 0)
			return true;
	}
	return false;
}

// Lazily connect (call only after LinuxWaylandShouldUse()).
static bool LinuxWlConnect()
{
	LinuxWaylandState &s = LinuxWl();
	if (s.active || s.connect_failed || s.display)
		return s.active;
	s.display = wl_display_connect(nullptr);
	if (!s.display)
	{
		s.connect_failed = true;
		return false;
	}
	s.registry = wl_display_get_registry(s.display);
	wl_registry_add_listener(s.registry, &sRegistryListener, nullptr);
	wl_display_roundtrip(s.display);
	wl_display_roundtrip(s.display);
	if (!s.wm_base)
	{
		// No xdg-shell: not a usable compositor for this backend.
		wl_display_disconnect(s.display);
		s.display = nullptr;
		s.connect_failed = true;
		return false;
	}
	xdg_wm_base_add_listener(s.wm_base, &sWmBaseListener, nullptr);
	if (s.vkbd_mgr)
		s.vkbd = zwp_virtual_keyboard_manager_v1_create_virtual_keyboard(s.vkbd_mgr, s.seat);
	if (s.vptr_mgr)
		s.vptr = zwlr_virtual_pointer_manager_v1_create_virtual_pointer(s.vptr_mgr, s.seat);
	if (s.seat)
	{
		// Listen for events delivered to this client's surfaces (test
		// facility: AHK_WL_EVLOG); also ensures get_keyboard/get_pointer
		// are exercised so sway enables the seat capabilities.
		wl_keyboard *kb = wl_seat_get_keyboard(s.seat);
		if (kb)
			wl_keyboard_add_listener(kb, &sKeyboardListener, nullptr);
		wl_pointer *ptr = wl_seat_get_pointer(s.seat);
		if (ptr)
			wl_pointer_add_listener(ptr, &sPointerListener, nullptr);
		// System clipboard via the seat's data device.
		LinuxClipboardWaylandSeat(s.seat);
	}
	(void)0;
	// Push a keymap so compositors accept key events (xkbcommon).
	if (s.vkbd)
	{
		xkb_context *ctx = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
		if (ctx)
		{
			xkb_keymap *km = xkb_keymap_new_from_names(ctx, nullptr, XKB_KEYMAP_COMPILE_NO_FLAGS);
			if (km)
			{
				char *str = xkb_keymap_get_as_string(km, XKB_KEYMAP_FORMAT_TEXT_V1);
				if (str)
				{
					size_t len = strlen(str);
					if (s.shm && len > 0)
					{
						char name[64];
						snprintf(name, sizeof(name), "/ahk-keymap-%d", getpid());
						int fd = shm_open(name, O_CREAT | O_RDWR, 0600);
						shm_unlink(name);
						if (fd >= 0 && ftruncate(fd, (off_t)len) == 0)
						{
							void *map = mmap(nullptr, len, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
							if (map != MAP_FAILED)
							{
								memcpy(map, str, len);
								munmap(map, len);
							}
							zwp_virtual_keyboard_v1_keymap(s.vkbd, XKB_KEYMAP_FORMAT_TEXT_V1, fd, (uint32_t)len);
							close(fd);
						}
					}
					free(str);
				}
				xkb_keymap_unref(km);
			}
			xkb_context_unref(ctx);
		}
		zwp_virtual_keyboard_v1_modifiers(s.vkbd, 0, 0, 0, 0);
		wl_display_flush(s.display);
		// Let the compositor finish creating the virtual keyboard device
		// and processing the keymap before any key events are sent
		// (compositors drop keys received too early; documented).
		wl_display_roundtrip(s.display);
	}
	s.active = true;
	return true;
}

bool LinuxWaylandActive()
{
	LinuxWaylandState &s = LinuxWl();
	if (s.active)
		return true;
	if (s.connect_failed)
		return false;
	if (LinuxWlXInUse())
		return false;
	if (!LinuxWaylandShouldUse())
		return false;
	return LinuxWlConnect();
}

bool LinuxWaylandCanInjectKeys()
{
	LinuxWaylandState &s = LinuxWl();
	return s.active && s.vkbd != nullptr;
}

// ---------------------------------------------------------------------------
// vk -> evdev keycode (US-ish layout; the same keys the Send engine emits)
// ---------------------------------------------------------------------------

unsigned int LinuxWaylandKeycodeForVk(unsigned int aVK)
{
	// The evdev keyboard rows are NOT contiguous like the Windows VK range:
	// KEY_0..KEY_9 run 11,2,3,...9,10 and F10 comes before F11 (KEY_F1..F10
	// = 59..68, KEY_F11/F12 = 87/88, KEY_F13..F24 = 183..194).  Use explicit
	// tables instead of arithmetic offsets (check0818 P1).  The LETTERS are
	// also QWERTY-ordered and non-contiguous (KEY_A..KEY_L = 30..38, then
	// KEY_Z=44, KEY_X=45, KEY_C=46, KEY_V=47, KEY_B=48, KEY_N=49, KEY_M=50)
	// -- KEY_A + offset would map 'V' to KEY_COMMA(51), so the paste-path
	// Ctrl+V never matched sway's bindsym (round-34 catch).
	static constexpr unsigned int letters[] = {
		KEY_A, KEY_B, KEY_C, KEY_D, KEY_E, KEY_F, KEY_G, KEY_H, KEY_I, KEY_J,
		KEY_K, KEY_L, KEY_M, KEY_N, KEY_O, KEY_P, KEY_Q, KEY_R, KEY_S, KEY_T,
		KEY_U, KEY_V, KEY_W, KEY_X, KEY_Y, KEY_Z
	};
	static constexpr unsigned int digits[] = {
		KEY_0, KEY_1, KEY_2, KEY_3, KEY_4,
		KEY_5, KEY_6, KEY_7, KEY_8, KEY_9
	};
	static constexpr unsigned int function_keys[] = {
		KEY_F1, KEY_F2, KEY_F3, KEY_F4, KEY_F5, KEY_F6,
		KEY_F7, KEY_F8, KEY_F9, KEY_F10, KEY_F11, KEY_F12,
		KEY_F13, KEY_F14, KEY_F15, KEY_F16, KEY_F17, KEY_F18,
		KEY_F19, KEY_F20, KEY_F21, KEY_F22, KEY_F23, KEY_F24
	};
	if (aVK >= 'A' && aVK <= 'Z')
		return letters[aVK - 'A'];
	if (aVK >= '0' && aVK <= '9')
		return digits[aVK - '0'];
	if (aVK >= 0x70 && aVK <= 0x87) // F1-F24.
		return function_keys[aVK - 0x70];
	switch (aVK)
	{
	case 0x08: return KEY_BACKSPACE;
	case 0x09: return KEY_TAB;
	case 0x0D: return KEY_ENTER;
	case 0x1B: return KEY_ESC;
	case 0x20: return KEY_SPACE;
	case 0x10: return KEY_LEFTSHIFT;
	case 0x11: return KEY_LEFTCTRL;
	case 0x12: return KEY_LEFTALT;
	case 0x5B: return KEY_LEFTMETA;
	case 0xA0: return KEY_LEFTSHIFT;
	case 0xA1: return KEY_RIGHTSHIFT;
	case 0xA2: return KEY_LEFTCTRL;
	case 0xA3: return KEY_RIGHTCTRL;
	case 0xA4: return KEY_LEFTALT;
	case 0xA5: return KEY_RIGHTALT;
	case 0x25: return KEY_LEFT;
	case 0x26: return KEY_UP;
	case 0x27: return KEY_RIGHT;
	case 0x28: return KEY_DOWN;
	case 0x21: return KEY_PAGEUP;
	case 0x22: return KEY_PAGEDOWN;
	case 0x24: return KEY_HOME;
	case 0x23: return KEY_END;
	case 0x2D: return KEY_INSERT;
	case 0x2E: return KEY_DELETE;
	case 0xC0: return KEY_GRAVE;
	case 0xBD: return KEY_MINUS;
	case 0xBB: return KEY_EQUAL;
	case 0xDB: return KEY_LEFTBRACE;
	case 0xDD: return KEY_RIGHTBRACE;
	case 0xDC: return KEY_BACKSLASH;
	case 0xBA: return KEY_SEMICOLON;
	case 0xDE: return KEY_APOSTROPHE;
	case 0xBC: return KEY_COMMA;
	case 0xBE: return KEY_DOT;
	case 0xBF: return KEY_SLASH;
	// VK codes: MULTIPLY=0x6A, ADD=0x6B, SEPARATOR=0x6C, SUBTRACT=0x6D,
	// DECIMAL=0x6E, DIVIDE=0x6F.  (0x6C, the numpad separator, has no good
	// US-layout evdev key, so it is left unsupported.)
	case 0x6A: return KEY_KPASTERISK;
	case 0x6B: return KEY_KPPLUS;
	case 0x6D: return KEY_KPMINUS;
	case 0x6E: return KEY_KPDOT;
	case 0x6F: return KEY_KPSLASH;
	case 0x60: return KEY_KP0;
	case 0x61: return KEY_KP1;
	case 0x62: return KEY_KP2;
	case 0x63: return KEY_KP3;
	case 0x64: return KEY_KP4;
	case 0x65: return KEY_KP5;
	case 0x66: return KEY_KP6;
	case 0x67: return KEY_KP7;
	case 0x68: return KEY_KP8;
	case 0x69: return KEY_KP9;
	}
	return 0;
}

// ---------------------------------------------------------------------------
// Input injection
// ---------------------------------------------------------------------------

bool LinuxWaylandKeyEvent(unsigned int aVK, bool aDown)
{
	LinuxWaylandState &s = LinuxWl();
	if (!s.active || !s.vkbd)
		return false;
	unsigned int kc = LinuxWaylandKeycodeForVk(aVK);
	if (!kc)
		return false;
	if (FILE *f = LinuxWlEvlog())
		fprintf(f, "send:k:%u:%s\n", kc, aDown ? "down" : "up");
	// xkb modifier bits used by compositors for binding matching:
	// Shift=1, Control=4, Mod1(Alt)=8, Mod4(Super/Logo)=64.
	unsigned int mod_bit = 0;
	switch (aVK)
	{
	case 0x10: case 0xA0: case 0xA1: mod_bit = 1; break; // Shift.
	case 0x11: case 0xA2: case 0xA3: mod_bit = 4; break; // Control.
	case 0x12: case 0xA4: case 0xA5: mod_bit = 8; break; // Alt.
	case 0x5B: mod_bit = 64; break;                      // Super/Logo.
	}
	if (aDown)
		s.vkbd_mods_depressed |= mod_bit;
	else
		s.vkbd_mods_depressed &= ~mod_bit;
	// Virtual-keyboard key events update the compositor's key state, but
	// the compositor's *modifier* state must be pushed explicitly with the
	// modifiers request (compositors such as sway match bindsym combos
	// against it).  Send it with every key event so combos like
	// Shift+Return are matched.
	zwp_virtual_keyboard_v1_modifiers(s.vkbd, s.vkbd_mods_depressed, 0, 0, 0);
	zwp_virtual_keyboard_v1_key(s.vkbd, 0, kc, aDown ? WL_KEYBOARD_KEY_STATE_PRESSED : WL_KEYBOARD_KEY_STATE_RELEASED);
	wl_display_flush(s.display);
	return true;
}

bool LinuxWaylandButtonEvent(unsigned int aButton, bool aDown)
{
	LinuxWaylandState &s = LinuxWl();
	if (!s.active || !s.vptr)
		return false;
	unsigned int code = 0;
	switch (aButton)
	{
	case 1: code = BTN_LEFT; break;
	case 2: code = BTN_MIDDLE; break;
	case 3: code = BTN_RIGHT; break;
	case 8: code = BTN_SIDE; break;
	case 9: code = BTN_EXTRA; break;
	default: return false;
	}
	zwlr_virtual_pointer_v1_button(s.vptr, 0, code, aDown ? WL_POINTER_BUTTON_STATE_PRESSED : WL_POINTER_BUTTON_STATE_RELEASED);
	wl_display_flush(s.display);
	// The virtual pointer device is created lazily by the compositor; the
	// first press needs a roundtrip so the device exists (documented).
	if (aDown && !s.vptr_used)
	{
		s.vptr_used = true;
		wl_display_roundtrip(s.display);
	}
	return true;
}

bool LinuxWaylandWheelEvent(unsigned int aButton, bool aDown)
{
	LinuxWaylandState &s = LinuxWl();
	if (!s.active || !s.vptr)
		return false;
	if (!aDown)
		return true; // Wheel "release" is a no-op (axes are momentary).
	unsigned int axis = 0; // 0 = vertical, 1 = horizontal.
	switch (aButton)
	{
	case 4: axis = 0; break;  // WheelUp.
	case 5: axis = 0; break;  // WheelDown.
	case 6: axis = 1; break;  // WheelLeft.
	case 7: axis = 1; break;  // WheelRight.
	default: return false;
	}
	int dir = (aButton == 4 || aButton == 6) ? 10 : -10;
	zwlr_virtual_pointer_v1_axis(s.vptr, 0, axis, wl_fixed_from_int(dir));
	wl_display_flush(s.display);
	return true;
}

bool LinuxWaylandMotionEvent(int aDX, int aDY)
{
	LinuxWaylandState &s = LinuxWl();
	if (!s.active || !s.vptr)
		return false;
	zwlr_virtual_pointer_v1_motion(s.vptr, 0, wl_fixed_from_int(aDX), wl_fixed_from_int(aDY));
	wl_display_flush(s.display);
	return true;
}

// Absolute pointer intent: Wayland clients cannot position the pointer
// absolutely, so the delta against the last tracked position is sent as
// relative motion (documented).  The tracked position starts at (0,0).
bool LinuxWaylandMotionTo(int aX, int aY)
{
	static int sX = 0, sY = 0;
	bool ok = LinuxWaylandMotionEvent(aX - sX, aY - sY);
	sX = aX;
	sY = aY;
	return ok;
}

// ---------------------------------------------------------------------------
// xdg toplevel windows (ToolTip etc.)
// ---------------------------------------------------------------------------

static void LinuxWlXdgConfigure(void *aData, xdg_surface *aXdg, uint32_t aSerial)
{
	xdg_surface_ack_configure(aXdg, aSerial);
	LinuxWaylandWindow *win = (LinuxWaylandWindow *)aData;
	if (!win || !win->surface)
		return;
	// The first commit after ack_configure may carry a buffer (attaching
	// one before the first configure is a protocol error: unconfigured
	// buffer).
	if (win->buffer && !win->buffer_attached)
	{
		wl_surface_attach(win->surface, win->buffer, 0, 0);
		wl_surface_damage(win->surface, 0, 0, 1, 1);
		win->buffer_attached = true;
	}
	wl_surface_commit(win->surface);
}

static const xdg_surface_listener sXdgSurfaceListener = {
	LinuxWlXdgConfigure
};

LinuxWaylandWindow *LinuxWaylandCreateWindow(const wchar_t *aTitle, int aW, int aH)
{
	LinuxWaylandState &s = LinuxWl();
	if (!s.active || !s.compositor || !s.wm_base)
		return nullptr;
	LinuxWaylandWindow *win = new (std::nothrow) LinuxWaylandWindow();
	if (!win)
		return nullptr;
	win->surface = wl_compositor_create_surface(s.compositor);
	win->xdg = xdg_wm_base_get_xdg_surface(s.wm_base, win->surface);
	xdg_surface_add_listener(win->xdg, &sXdgSurfaceListener, win);
	win->toplevel = xdg_surface_get_toplevel(win->xdg);
	if (aW > 0 && aH > 0)
		xdg_toplevel_set_min_size(win->toplevel, (int32_t)aW, (int32_t)aH);
	LinuxWaylandSetWindowTitle(win, aTitle ? aTitle : L"");
	xdg_toplevel_set_app_id(win->toplevel, "ahk");
	// Prepare a 1x1 transparent buffer (attached on the first configure, so
	// the compositor maps the window; see LinuxWlXdgConfigure).
	if (s.shm)
	{
		char name[64];
		snprintf(name, sizeof(name), "/ahk-win-%d-%zu", getpid(), s.windows.size());
		win->shm_fd = shm_open(name, O_CREAT | O_RDWR, 0600);
		shm_unlink(name);
		if (win->shm_fd >= 0 && ftruncate(win->shm_fd, 4) == 0)
		{
			void *map = mmap(nullptr, 4, PROT_READ | PROT_WRITE, MAP_SHARED, win->shm_fd, 0);
			if (map != MAP_FAILED)
			{
				*(uint32_t *)map = 0;
				munmap(map, 4);
			}
			win->pool = wl_shm_create_pool(s.shm, win->shm_fd, 4);
			win->buffer = wl_shm_pool_create_buffer(win->pool, 0, 1, 1, 4, WL_SHM_FORMAT_ARGB8888);
		}
	}
	wl_surface_commit(win->surface);
	wl_display_flush(s.display);
	s.windows.push_back(win);
	return win;
}

void LinuxWaylandSetWindowTitle(LinuxWaylandWindow *aWin, const wchar_t *aTitle)
{
	if (!aWin || !aTitle)
		return;
	char utf8[4096];
	size_t n = wcstombs(utf8, aTitle, sizeof(utf8) - 1);
	if (n == (size_t)-1)
		n = 0;
	utf8[n] = '\0';
	xdg_toplevel_set_title(aWin->toplevel, utf8);
	wl_display_flush(LinuxWl().display);
}

void LinuxWaylandDestroyWindow(LinuxWaylandWindow *aWin)
{
	if (!aWin)
		return;
	LinuxWaylandState &s = LinuxWl();
	for (size_t i = 0; i < s.windows.size(); ++i)
		if (s.windows[i] == aWin)
		{
			s.windows.erase(s.windows.begin() + i);
			break;
		}
	xdg_toplevel_destroy(aWin->toplevel);
	xdg_surface_destroy(aWin->xdg);
	if (aWin->buffer)
		wl_buffer_destroy(aWin->buffer);
	if (aWin->pool)
		wl_shm_pool_destroy(aWin->pool);
	if (aWin->shm_fd >= 0)
		close(aWin->shm_fd);
	wl_surface_destroy(aWin->surface);
	wl_display_flush(s.display);
	delete aWin;
}

// ---------------------------------------------------------------------------
// Main-loop integration
// ---------------------------------------------------------------------------

int LinuxWaylandPollFd()
{
	LinuxWaylandState &s = LinuxWl();
	return s.display ? wl_display_get_fd(s.display) : -1;
}

wl_display *LinuxWaylandDisplay()
{
	LinuxWaylandState &s = LinuxWl();
	return s.active ? s.display : nullptr;
}

void LinuxWaylandDispatch()
{
	LinuxWaylandState &s = LinuxWl();
	if (!s.display)
		return;
	wl_display_flush(s.display);
	while (wl_display_prepare_read(s.display) != 0)
		wl_display_dispatch_pending(s.display);
	wl_display_read_events(s.display);
	wl_display_dispatch_pending(s.display);
}

// ---------------------------------------------------------------------------
// Screen capture via wlr-screencopy (XWayland fallback)
// ---------------------------------------------------------------------------
// sway's XWayland root window has no backing store, so XGetImage of the
// root returns BadMatch; the compositor, however, exposes the real screen
// content through zwlr_screencopy_manager_v1.  When the X11 grab fails,
// the image module falls back to this function: it connects to the
// Wayland compositor (a separate connection; the main one may be inactive
// in XWayland mode), asks for the region in output-logical coordinates and
// converts the returned wl_shm buffer to 0xRRGGBB.

struct LinuxWlCapState
{
	wl_display *display = nullptr;
	wl_registry *registry = nullptr;
	wl_shm *shm = nullptr;
	wl_output *output = nullptr;
	zwlr_screencopy_manager_v1 *mgr = nullptr;
	int out_w = 0, out_h = 0; // Logical output size (physical / scale).
};

static LinuxWlCapState &LinuxWlCap()
{
	static LinuxWlCapState s;
	return s;
}

static void LinuxWlCapOutputGeometry(void *aData, wl_output *o, int32_t x, int32_t y
	, int32_t wmm, int32_t hmm, int32_t sub, const char *make, const char *model, int32_t transform)
{
}

static void LinuxWlCapOutputMode(void *aData, wl_output *o, uint32_t aFlags
	, int32_t aW, int32_t aH, int32_t aRefresh)
{
	LinuxWlCapState *c = (LinuxWlCapState *)aData;
	c->out_w = aW;
	c->out_h = aH;
}

static void LinuxWlCapOutputDone(void *aData, wl_output *o)
{
}

static void LinuxWlCapOutputScale(void *aData, wl_output *o, int32_t aFactor)
{
	LinuxWlCapState *c = (LinuxWlCapState *)aData;
	if (aFactor > 1)
	{
		c->out_w /= aFactor;
		c->out_h /= aFactor;
	}
}

static void LinuxWlCapOutputName(void *aData, wl_output *o, const char *aName)
{
}

static void LinuxWlCapOutputDescription(void *aData, wl_output *o, const char *aDesc)
{
}

static const wl_output_listener sCapOutputListener = {
	LinuxWlCapOutputGeometry,
	LinuxWlCapOutputMode,
	LinuxWlCapOutputDone,
	LinuxWlCapOutputScale,
	LinuxWlCapOutputName,
	LinuxWlCapOutputDescription
};

struct LinuxWlCapFrame
{
	bool have_buffer = false;
	uint32_t format = 0, width = 0, height = 0, stride = 0;
	bool y_invert = false;
	bool done = false, failed = false;
};

static void LinuxWlCapFrameBuffer(void *aData, zwlr_screencopy_frame_v1 *f
	, uint32_t aFormat, uint32_t aWidth, uint32_t aHeight, uint32_t aStride)
{
	LinuxWlCapFrame *fr = (LinuxWlCapFrame *)aData;
	fr->format = aFormat;
	fr->width = aWidth;
	fr->height = aHeight;
	fr->stride = aStride;
	fr->have_buffer = true;
}

static void LinuxWlCapFrameFlags(void *aData, zwlr_screencopy_frame_v1 *f, uint32_t aFlags)
{
	LinuxWlCapFrame *fr = (LinuxWlCapFrame *)aData;
	fr->y_invert = (aFlags & ZWLR_SCREENCOPY_FRAME_V1_FLAGS_Y_INVERT) != 0;
}

static void LinuxWlCapFrameReady(void *aData, zwlr_screencopy_frame_v1 *f
	, uint32_t aHi, uint32_t aLo, uint32_t aNsec)
{
	((LinuxWlCapFrame *)aData)->done = true;
}

static void LinuxWlCapFrameFailed(void *aData, zwlr_screencopy_frame_v1 *f)
{
	((LinuxWlCapFrame *)aData)->failed = true;
}

static const zwlr_screencopy_frame_v1_listener sCapFrameListener = {
	LinuxWlCapFrameBuffer, // buffer
	LinuxWlCapFrameFlags,  // flags
	LinuxWlCapFrameReady,  // ready
	LinuxWlCapFrameFailed  // failed
};

static void LinuxWlCapRegistryGlobal(void *aData, wl_registry *r, uint32_t aName
	, const char *aIface, uint32_t aVersion)
{
	LinuxWlCapState *c = (LinuxWlCapState *)aData;
	if (!strcmp(aIface, "wl_shm") && !c->shm)
		c->shm = (wl_shm *)wl_registry_bind(r, aName, &wl_shm_interface, 1);
	else if (!strcmp(aIface, "wl_output") && !c->output)
	{
		c->output = (wl_output *)wl_registry_bind(r, aName, &wl_output_interface, 2);
		wl_output_add_listener(c->output, &sCapOutputListener, c);
	}
	else if (!strcmp(aIface, "zwlr_screencopy_manager_v1") && !c->mgr)
		c->mgr = (zwlr_screencopy_manager_v1 *)wl_registry_bind(r, aName
			, &zwlr_screencopy_manager_v1_interface, 1);
}

static void LinuxWlCapRegistryRemove(void *aData, wl_registry *r, uint32_t aName)
{
}

static const wl_registry_listener sCapRegistryListener = {
	LinuxWlCapRegistryGlobal,
	LinuxWlCapRegistryRemove
};

// Registered once at first capture use (see LinuxWlCapRegisterAtExit below;
// forward-declared here because the connection code precedes the definition).
static void LinuxWlCapAtExit();
static bool LinuxWlCapRegisterAtExit();

static void LinuxWlCapDisconnect()
{
	LinuxWlCapState &c = LinuxWlCap();
	if (c.output)
	{
		wl_output_destroy(c.output);
		c.output = nullptr;
	}
	if (c.mgr)
	{
		zwlr_screencopy_manager_v1_destroy(c.mgr);
		c.mgr = nullptr;
	}
	if (c.shm)
	{
		wl_shm_destroy(c.shm);
		c.shm = nullptr;
	}
	if (c.registry)
	{
		// Destroy the registry proxy before disconnecting (the display's
		// proxies are not freed by wl_display_disconnect; LSan sees them).
		wl_registry_destroy(c.registry);
		c.registry = nullptr;
	}
	if (c.display)
	{
		wl_display_disconnect(c.display);
		c.display = nullptr;
	}
	c.out_w = c.out_h = 0;
}

// Dispatch until aCond or aTimeoutMs elapses; returns true when aCond.
static bool LinuxWlCapWait(bool &aCond, int aTimeoutMs)
{
	LinuxWlCapState &c = LinuxWlCap();
	int waited = 0;
	while (!aCond && waited < aTimeoutMs)
	{
		wl_display_flush(c.display);
		struct pollfd pfd = { wl_display_get_fd(c.display), POLLIN, 0 };
		int pr = poll(&pfd, 1, 50);
		if (pr > 0)
		{
			if (wl_display_dispatch(c.display) < 0)
				return false; // Compositor disconnected.
		}
		else if (pr < 0 && errno != EINTR)
			return false;
		waited += 50;
	}
	return aCond;
}

bool LinuxWaylandCaptureScreen(int aLeft, int aTop, int aWidth, int aHeight
	, std::vector<DWORD> &aPixels)
{
	aPixels.clear();
	LinuxWlCapState &c = LinuxWlCap();
	if (!c.display)
	{
		// Try WAYLAND_DISPLAY, then common names under XDG_RUNTIME_DIR
		// (the XWayland runner unsets WAYLAND_DISPLAY for the X suites).
		c.display = wl_display_connect(nullptr);
		if (!c.display)
		{
			const char *rt = getenv("XDG_RUNTIME_DIR");
			if (rt && *rt)
			{
				const char *names[] = { "wayland-0", "wayland-1" };
				for (const char *name : names)
				{
					std::string path = std::string(rt) + "/" + name;
					if (access(path.c_str(), R_OK) == 0)
					{
						c.display = wl_display_connect(path.c_str());
						if (c.display)
							break;
					}
				}
			}
		}
		if (!c.display)
			return false;
		LinuxWlCapRegisterAtExit();
		c.registry = wl_display_get_registry(c.display);
		wl_registry_add_listener(c.registry, &sCapRegistryListener, &c);
		wl_display_roundtrip(c.display);
		wl_display_roundtrip(c.display); // Mode/scale events arrive on the 2nd.
	}
	if (!c.mgr || !c.shm || !c.output || c.out_w <= 0 || c.out_h <= 0)
	{
		LinuxWlCapDisconnect();
		return false;
	}

	// The region is in output-logical coordinates (sway's XWayland root
	// spans the output 1:1 at scale 1; multi-output setups use the first
	// output's coordinate space -- documented).  Clip to the output like
	// XGetImage would fail for off-screen parts (BadMatch semantics).
	int x = aLeft, y = aTop, w = aWidth, h = aHeight;
	if (x < 0) { w += x; x = 0; }
	if (y < 0) { h += y; y = 0; }
	if (w <= 0 || h <= 0)
		return false;
	if (x + w > c.out_w) w = c.out_w - x;
	if (y + h > c.out_h) h = c.out_h - y;
	if (w <= 0 || h <= 0)
		return false;
	if (w != aWidth || h != aHeight)
		return false; // Off-screen region: XGetImage would BadMatch.

	LinuxWlCapFrame fr;
	zwlr_screencopy_frame_v1 *frame = zwlr_screencopy_manager_v1_capture_output_region(
		c.mgr, 0 /*overlay_cursor*/, c.output, x, y, w, h);
	if (!frame)
		return false;
	zwlr_screencopy_frame_v1_add_listener(frame, &sCapFrameListener, &fr);
	wl_display_flush(c.display);
	if (!LinuxWlCapWait(fr.have_buffer, 3000) || fr.failed)
	{
		zwlr_screencopy_frame_v1_destroy(frame);
		LinuxWlCapDisconnect();
		return false;
	}

	// Create the wl_shm buffer the compositor will copy into.
	char name[64];
	snprintf(name, sizeof(name), "/ahk-cap-%d-%p", getpid(), (void *)frame);
	int fd = shm_open(name, O_CREAT | O_RDWR, 0600);
	shm_unlink(name);
	if (fd < 0)
	{
		zwlr_screencopy_frame_v1_destroy(frame);
		return false;
	}
	size_t buf_size = (size_t)fr.stride * fr.height;
	if (ftruncate(fd, (off_t)buf_size) != 0)
	{
		close(fd);
		zwlr_screencopy_frame_v1_destroy(frame);
		return false;
	}
	void *map = mmap(nullptr, buf_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	if (map == MAP_FAILED)
	{
		close(fd);
		zwlr_screencopy_frame_v1_destroy(frame);
		return false;
	}
	wl_shm_pool *pool = wl_shm_create_pool(c.shm, fd, (int32_t)buf_size);
	wl_buffer *buf = wl_shm_pool_create_buffer(pool, 0, (int32_t)fr.width
		, (int32_t)fr.height, (int32_t)fr.stride, fr.format);
	wl_shm_pool_destroy(pool);
	zwlr_screencopy_frame_v1_copy(frame, buf);
	wl_display_flush(c.display);
	bool ok = LinuxWlCapWait(fr.done, 3000) && !fr.failed;

	if (ok)
	{
		// Convert to 0xRRGGBB.  sway delivers XRGB8888/ARGB8888
		// (little-endian memory order B,G,R,X).
		if (fr.format != WL_SHM_FORMAT_XRGB8888 && fr.format != WL_SHM_FORMAT_ARGB8888)
			ok = false;
		else
		{
			const uint8_t *base = (const uint8_t *)map;
			aPixels.resize((size_t)fr.width * fr.height);
			for (uint32_t yy = 0; yy < fr.height; ++yy)
			{
				uint32_t src_y = fr.y_invert ? fr.height - 1 - yy : yy;
				const uint8_t *row = base + (size_t)src_y * fr.stride;
				for (uint32_t xx = 0; xx < fr.width; ++xx)
				{
					const uint8_t *p = row + (size_t)xx * 4;
					aPixels[(size_t)yy * fr.width + xx] =
						((DWORD)p[2] << 16) | ((DWORD)p[1] << 8) | (DWORD)p[0];
				}
			}
		}
	}

	munmap(map, buf_size);
	close(fd);
	wl_buffer_destroy(buf);
	zwlr_screencopy_frame_v1_destroy(frame);
	wl_display_flush(c.display);
	if (!ok)
	{
		aPixels.clear();
		LinuxWlCapDisconnect();
	}
	return ok;
}

// The capture connection is cached (it may be reused across calls, e.g.
// PixelSearch loops); disconnect it at process exit so leak checkers
// (LSan) see a clean shutdown.
static void LinuxWlCapAtExit()
{
	LinuxWlCapDisconnect();
}

// Registered once at first capture use.
static bool LinuxWlCapRegisterAtExit()
{
	static bool sRegistered = false;
	if (!sRegistered)
	{
		atexit(LinuxWlCapAtExit);
		sRegistered = true;
	}
	return true;
}
