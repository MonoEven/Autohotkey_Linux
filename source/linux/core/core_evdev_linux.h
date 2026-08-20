// Native evdev keyboard-capture lane (check0820 direction-B: the capture
// half of the evdev/uinput broker).
//
// While the X11 backend captures keys through XGrabKey passive grabs, a
// native-Wayland session has no equivalent protocol hook: the compositor
// owns the keyboard.  The kernel input stream (/dev/input/event*) is the
// only compositor-agnostic place that sees EVERY physical key event
// (GNOME/Mutter, KWin, sway, Hyprland read the same stream through
// libinput).  This module:
//
//   - scans /dev/input/event* and reads EV_KEY events (poll, non-blocking),
//   - matches them against the Hotkey table (vk + modifiers, left/right
//     aware; key-up variants; pass-through selectors),
//   - fires the AHK hotkey body in a new quasi-thread (same semantics as
//     the portal/X11 backends),
//   - in SUPPRESS mode (EVIOCGRAB succeeded) the physical key is exclusive
//     to us: matching hotkeys are consumed and everything else is replayed
//     through /dev/uinput so the compositor still receives normal typing
//     (the uinput lane is in core_uinput_linux.cpp),
//   - in LISTEN mode (EVIOCGRAB failed: no read permission or no grab
//     support) hotkeys still fire but the key reaches the application too
//     (a pass-through hotkey, exactly what `~` selects anyway).
//
// The permission model (check0820): reading /dev/input/event* requires
// membership in the `input` group or root; /dev/uinput typically needs the
// same or a udev rule.  See docs-v2/docs/linux-port.htm.  A crashed or
// killed process automatically releases EVIOCGRAB (the kernel drops the
// grab when the fd closes), so hotkeys can never stay stuck - the same
// fail-open property as the X11/portal backends.

#pragma once

// True when at least one keyboard event device is open (the backend is
// usable).  Idempotent: scans once.
bool LinuxEvdevActive();

// True when the backend may SUPPRESS matched hotkeys (at least one device
// accepted EVIOCGRAB).  Without suppression the backend is listen-only.
bool LinuxEvdevCanSuppress();

// Pump pending evdev events (poll + read all fds).  Called from the main
// loop (MsgSleep) alongside LinuxInputBackendDispatch().
void LinuxEvdevDispatch();

// Release devices (script exit / reload).
void LinuxEvdevShutdown();

// Human-readable reason when the backend could not start (permission).
const char *LinuxEvdevLastError();