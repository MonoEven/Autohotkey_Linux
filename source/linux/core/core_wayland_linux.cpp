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
#include <wayland-client.h>
#include <xdg-shell-protocol.h>
#include <virtual-keyboard-unstable-v1-protocol.h>
#include <wlr-virtual-pointer-unstable-v1-protocol.h>
#include <xkbcommon/xkbcommon.h>
#include <linux/input-event-codes.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>
#include <poll.h>
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

// ---------------------------------------------------------------------------
// vk -> evdev keycode (US-ish layout; the same keys the Send engine emits)
// ---------------------------------------------------------------------------

unsigned int LinuxWaylandKeycodeForVk(unsigned int aVK)
{
	if (aVK >= 'A' && aVK <= 'Z')
		return KEY_A + (aVK - 'A');
	if (aVK >= '0' && aVK <= '9')
		return KEY_1 + (aVK - '0');
	if (aVK >= 0x70 && aVK <= 0x87) // F1-F24.
		return KEY_F1 + (aVK - 0x70);
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
	case 0x6B: return KEY_KPASTERISK;
	case 0x6C: return KEY_KPPLUS;
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
	zwp_virtual_keyboard_v1_key(s.vkbd, 0, kc, aDown ? WL_KEYBOARD_KEY_STATE_PRESSED : WL_KEYBOARD_KEY_STATE_RELEASED);
	wl_display_flush(s.display);
	// Modifier state must reach the compositor before subsequent keys are
	// matched against modifier combos (e.g. sway bindsym Shift+Return); a
	// roundtrip lets the compositor process the modifier press first.
	if (aDown)
	{
		switch (aVK)
		{
		case 0x10: case 0x11: case 0x12: case 0x5B:
		case 0xA0: case 0xA1: case 0xA2: case 0xA3: case 0xA4: case 0xA5:
			wl_display_roundtrip(s.display);
			break;
		}
	}
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
