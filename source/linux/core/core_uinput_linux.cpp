// Native evdev/uinput injection lane (check0820 direction-B).
//
// Compositors without the zwp_virtual_keyboard protocol (GNOME/Mutter, ...)
// cannot receive key events through the Wayland virtual-keyboard channel.
// uinput (the kernel virtual-input API) is a compositor-agnostic fallback:
// a virtual USB keyboard is created and key/button/relative-motion events are
// written into the kernel input stream.  libinput/compositors accept it like
// a physical device (ydotool uses the same mechanism), so arbitrary-key Send
// becomes possible on GNOME Wayland too.  The full keyboard *capture* side
// (EVIOCGRAB / evdev listeners + hotkey semantics) remains the future
// ahk-inputd broker -- out of scope here (needs root/input-group daemon).
//
// The lane can be disabled explicitly with AHK_UINPUT=0/off/false/no (a
// typo must not silently flip the behavior).  The virtual keyboard is
// created once and kept for the process lifetime: short-lived uinput
// keyboards can be dropped by compositor modifier handling, while a
// persistent device is the reliable lane.

#include "../../stdafx.h"
#include "core_uinput_linux.h"
#include "core_wayland_linux.h" // LinuxWaylandKeycodeForVk (evdev table)
#include <linux/uinput.h>
#include <linux/input.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <cerrno>
#include <cstring>
#include <cstdio>

namespace {

int sFD = -1;             // /dev/uinput descriptor; -1 = not yet created.
bool sTried = false;      // A create attempt has been made.
bool sUsable = false;     // The device exists and accepts events.
bool sDenied = false;     // AHK_UINPUT=0/off/false/no disables the lane.

bool CreateUinputDevice()
{
	sFD = open("/dev/uinput", O_WRONLY | O_NONBLOCK);
	if (sFD < 0)
		return false;

	// Enable the kernel event types we emit: keyboard keys and relative
	// pointer/wheel axes.
	for (unsigned int t : { EV_KEY, EV_REL })
	{
		if (ioctl(sFD, UI_SET_EVBIT, t) != 0)
			goto fail;
	}
	// Enable every keycode (the evdev table covers the US layout; enabling
	// the whole range avoids rejections for mapped keycodes).
	for (unsigned int k = 1; k < KEY_MAX; ++k)
	{
		if (ioctl(sFD, UI_SET_KEYBIT, k) != 0)
			goto fail;
	}
	// Relative pointer + wheel axes.
	for (unsigned int a : { REL_X, REL_Y, REL_WHEEL, REL_HWHEEL })
	{
		if (ioctl(sFD, UI_SET_RELBIT, a) != 0)
			goto fail;
	}

	struct uinput_setup setup;
	memset(&setup, 0, sizeof(setup));
	setup.id.bustype = BUS_USB;
	setup.id.vendor  = 0x2C2F;
	setup.id.product = 0x0001;
	setup.id.version = 1;
	strncpy(setup.name, "AHK Linux virtual keyboard",
		sizeof(setup.name));
	if (ioctl(sFD, UI_DEV_SETUP, &setup) != 0)
		goto fail;
	if (ioctl(sFD, UI_DEV_CREATE) != 0)
		goto fail;
	sUsable = true;
	sTried = true;
	return true;

fail:
	{
		int err = errno;
		close(sFD);
		sFD = -1;
		sTried = true;
		fprintf(stderr,
			"AHK uinput: cannot create the virtual keyboard device "
			"(%s).\n"
			"To send on a compositor without virtual-keyboard support, "
			"run as root or make /dev/uinput writable (input group / udev "
			"rule); the Wayland clipboard-paste fallback is unaffected.\n",
			strerror(err));
		return false;
	}
}

// Write one kernel input event (input_event requires >=16 bytes; the
// kernel fills tv_sec/tv_usec when the fd is a uinput device).
void WriteUinputEvent(int aType, int aCode, int aValue)
{
	if (sFD < 0)
		return;
	struct input_event e;
	memset(&e, 0, sizeof(e));
	e.type  = (__u16)aType;
	e.code  = (__u16)aCode;
	e.value = (__s32)aValue;
	if (write(sFD, &e, sizeof(e)) != (ssize_t)sizeof(e))
	{
		if (errno != EAGAIN)
		{
			// Hard failure: the device vanished (user unplugged? kernel
			// limits).  Recreate on the next call.
			close(sFD);
			sFD = -1;
			sUsable = false;
		}
	}
}

} // namespace

bool LinuxUinputInjectionAvailable()
{
	if (!sTried)
	{
		const char *v = getenv("AHK_UINPUT");
		if (v && *v)
		{
			char c = (char)v[0];
			if (c == '0' || c == 'o' || c == 'f' || c == 'n')
				sDenied = true;
		}
		if (!sDenied)
			CreateUinputDevice();
		else
			sTried = true; // Explicly disabled: do not even probe.
	}
	return sUsable && sFD >= 0;
}

bool LinuxUinputKeyEvent(unsigned int aVK, bool aDown)
{
	if (!LinuxUinputInjectionAvailable())
		return false;
	unsigned int kc = LinuxWaylandKeycodeForVk(aVK); // Same evdev table.
	if (!kc)
		return false;
	WriteUinputEvent(EV_KEY, (int)kc, aDown ? 1 : 0);
	return true;
}

bool LinuxUinputButtonEvent(unsigned int aButton, bool aDown)
{
	if (!LinuxUinputInjectionAvailable())
		return false;
	unsigned int btn = 0;
	switch (aButton)
	{
	case 1: btn = BTN_LEFT;   break;
	case 2: btn = BTN_MIDDLE; break;
	case 3: btn = BTN_RIGHT;  break;
	case 8: btn = BTN_SIDE;   break;
	case 9: btn = BTN_EXTRA;  break;
	default: return false;
	}
	WriteUinputEvent(EV_KEY, (int)btn, aDown ? 1 : 0);
	return true;
}

bool LinuxUinputWheelEvent(unsigned int aButton, bool aDown)
{
	if (!LinuxUinputInjectionAvailable())
		return false;
	if (!aDown)
		return true;
	switch (aButton)
	{
	case 4: WriteUinputEvent(EV_REL, REL_WHEEL, 1);  return true;
	case 5: WriteUinputEvent(EV_REL, REL_WHEEL, -1); return true;
	case 6: WriteUinputEvent(EV_REL, REL_HWHEEL, 1);  return true;
	case 7: WriteUinputEvent(EV_REL, REL_HWHEEL, -1); return true;
	default: return false;
	}
}

bool LinuxUinputMotionEvent(int aDX, int aDY)
{
	if (!LinuxUinputInjectionAvailable())
		return false;
	if (aDX)
		WriteUinputEvent(EV_REL, REL_X, aDX);
	if (aDY)
		WriteUinputEvent(EV_REL, REL_Y, aDY);
	return true;
}