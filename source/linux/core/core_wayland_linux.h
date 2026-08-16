// Declarations of the Wayland display layer (implemented in
// core_wayland_linux.cpp).  The port prefers X11 when a display is
// available (which also covers XWayland); the Wayland layer is used only
// when no X display exists but WAYLAND_DISPLAY connects.
#pragma once

struct wl_display;

// Opaque handle to a script-owned xdg toplevel window.
struct LinuxWaylandWindow;

// True when the Wayland layer is connected and in use (no X display).
bool LinuxWaylandActive();

// Create/destroy a script-owned xdg toplevel window (ToolTip etc.).
// Returns null on failure.  aW/aH are the requested size hints.
LinuxWaylandWindow *LinuxWaylandCreateWindow(const wchar_t *aTitle, int aW, int aH);
void LinuxWaylandDestroyWindow(LinuxWaylandWindow *aWin);
void LinuxWaylandSetWindowTitle(LinuxWaylandWindow *aWin, const wchar_t *aTitle);

// Input injection through the virtual keyboard/pointer protocols.
// aVK is a Win32 virtual key; aButton an X11 button number (1/2/3/8/9,
// 4-7 wheel).  Returns false when the device is unavailable (no manager
// global on the compositor).
bool LinuxWaylandKeyEvent(unsigned int aVK, bool aDown);
bool LinuxWaylandButtonEvent(unsigned int aButton, bool aDown);
bool LinuxWaylandWheelEvent(unsigned int aButton, bool aDown);
bool LinuxWaylandMotionEvent(int aDX, int aDY); // Relative motion.
bool LinuxWaylandMotionTo(int aX, int aY);      // Absolute intent (tracked position).

// vk -> Linux evdev keycode (used by the virtual keyboard; no server
// round-trip needed).  0 when unmapped.
unsigned int LinuxWaylandKeycodeForVk(unsigned int aVK);

// Main-loop integration: the pollable fd and a dispatch call.
int LinuxWaylandPollFd();
void LinuxWaylandDispatch();

// The active Wayland display (for subsystems that need it, e.g. the
// clipboard's data-device calls).  Null unless the Wayland layer is
// connected and active.
wl_display *LinuxWaylandDisplay();

// Screen-capture fallback: grab the region (aLeft,aTop,aWidth,aHeight) via
// the wlr-screencopy protocol into aPixels as 0xRRGGBB row-major.  Used
// when XGetImage cannot read the root window (sway's XWayland root has no
// backing store and returns BadMatch).  Returns false when no compositor
// or no screencopy manager is reachable, or the capture failed.
bool LinuxWaylandCaptureScreen(int aLeft, int aTop, int aWidth, int aHeight
	, std::vector<DWORD> &aPixels);

// True when a Wayland connection attempt should be made (WAYLAND_DISPLAY
// set or a default socket exists) and no X display is in use.
bool LinuxWaylandShouldUse();
