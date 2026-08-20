// Native evdev/uinput injection lane (check0820 direction-B).
//
// Compositors without the zwp_virtual_keyboard protocol (GNOME/Mutter,
// KWin, ...) cannot receive key events through the Wayland virtual-keyboard
// channel.  uinput (the kernel virtual-input API) is a compositor-agnostic
// fallback: a virtual USB keyboard device is created and key/button/relative
// events are written into the kernel input stream, which libinput/compositors
// accept like a physical device.  This makes arbitrary-key Send possible on
// GNOME Wayland (ydotool uses the same mechanism).  Capture (EVIOCGRAB-based
// global hook semantics) is NOT provided here: that stays in the future
// ahk-inputd broker (needs root/input-group daemon; see docs).
#pragma once

// True when the uinput virtual device is usable (opening /dev/uinput and
// creating the keyboard device succeeded).  Idempotent (initializes once).
bool LinuxUinputInjectionAvailable();

// Send one key event through the uinput virtual keyboard.  aVK is a Win32
// virtual key; returning false when the lane is unavailable.
bool LinuxUinputKeyEvent(unsigned int aVK, bool aDown);

// Mouse events through the uinput virtual pointer.  aButton uses the X11
// numbering (1/2/3/8/9, 4-7 wheel); motion is relative deltas.
bool LinuxUinputButtonEvent(unsigned int aButton, bool aDown);
bool LinuxUinputWheelEvent(unsigned int aButton, bool aDown);
bool LinuxUinputMotionEvent(int aDX, int aDY);