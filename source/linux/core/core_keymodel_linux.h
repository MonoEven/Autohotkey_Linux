#pragma once

#include "../../stdafx.h"
#include "../../keyboard_mouse.h"
#include <X11/Xlib.h>
#include <X11/keysym.h>
#include <stdint.h>

// Canonical three-layer view of one key on Linux.
struct AhkLinuxKeyIdentity
{
	uint32_t evdev_code; // physical KEY_* number
	sc_type sc;          // AHK/Windows set-1 scan code
	vk_type vk;          // logical Win32-style VK
	uint32_t text;       // Unicode scalar for current layout/modifiers, or 0
	KeySym keysym;
};

struct AhkLinuxKeyStroke
{
	KeyCode keycode;
	bool shift;
	bool altgr;
};

// set-1/extended AHK scan code <-> Linux evdev physical code.
uint32_t LinuxEvdevCodeForScanCode(sc_type aSC);
sc_type LinuxScanCodeForEvdev(uint32_t aEvdevCode);
KeyCode LinuxX11KeycodeForScanCode(sc_type aSC);

// Refresh after MappingNotify. Decode uses the live XKB state from the server.
bool LinuxKeyModelX11Refresh(Display *aDisplay);
bool LinuxKeyModelX11Decode(Display *aDisplay, KeyCode aKeycode, unsigned int aXState,
	AhkLinuxKeyIdentity &aOut);

// Find the least-modified key in the active layout which produces a Unicode
// scalar. Supported modifier combinations are none, Shift, AltGr and both.
bool LinuxKeyModelX11FindUtf32(Display *aDisplay, uint32_t aCodepoint, AhkLinuxKeyStroke &aOut);
