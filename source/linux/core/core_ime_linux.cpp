// Linux input-method (IME) state query (check0820 §2: ibus/fcitx active state).
//
// Purpose: give scripts a deterministic read of "is an input-method
// framework present and what is the effective keyboard state right now".
// Windows AutoHotkey exposes IME via A_IME / WM_IME_*; the Linux port has
// no equivalent, so this adds a small query used by the ImeGetState()
// built-in:
//
//   lm_ime_state() -> "ibus|fcitx5|none"   (framework detected on the bus)
//   xkb group index on X11                  (0 = base layout, >0 = IME group)
//
// Honest scope (check0820):
//   - Framework detection is authoritative: org.freedesktop.IBus /
//     org.fcitx.Fcitx5.Controller1 ownership on the session bus.
//   - The *active layout group* (X11 XKB group) is the reliable
//     "effective IME activation state" for the keyboard: IMEs switch the
//     group (e.g. ibus maps Group 2 to the IME).  It is read without
//     touching the IME process.
//   - Reading the preedit string of a foreign window, or *knowing* which
//     engine a focused app uses, is per-window and depends on the IME's
//     private D-Bus protocol; not exposed here (documented as out of
//     scope in docs/IME-Integration.md).
#include "core_ime_linux.h"
#include "core_win_linux.h"
#include "../../stdafx.h"
#include <dbus/dbus.h>
#include <X11/Xlib.h>
#include <X11/XKBlib.h>
#include <cstdlib>
#include <cstring>
#include <string>

namespace
{

// Has the given well-known session-bus name a CURRENT owner?
bool NameHasOwner(const char *aName)
{
	DBusError err;
	dbus_error_init(&err);
	DBusConnection *c = dbus_bus_get(DBUS_BUS_SESSION, &err);
	if (!c)
	{
		dbus_error_free(&err);
		return false;
	}
	DBusMessage *msg = dbus_message_new_method_call("org.freedesktop.DBus",
		"/org/freedesktop/DBus", "org.freedesktop.DBus", "NameHasOwner");
	if (!msg)
	{
		dbus_connection_unref(c);
		dbus_error_free(&err);
		return false;
	}
	DBusMessageIter it;
	dbus_message_iter_init_append(msg, &it);
	dbus_message_iter_append_basic(&it, DBUS_TYPE_STRING, &aName);
	DBusMessage *rep = dbus_connection_send_with_reply_and_block(c, msg, 1000, nullptr);
	dbus_message_unref(msg);
	bool has = false;
	if (rep)
	{
		DBusMessageIter rit;
		dbus_message_iter_init(rep, &rit);
		if (dbus_message_iter_get_arg_type(&rit) == DBUS_TYPE_BOOLEAN)
		{
			dbus_bool_t b = FALSE;
			dbus_message_iter_get_basic(&rit, &b);
			has = b != FALSE;
		}
		dbus_message_unref(rep);
	}
	dbus_connection_unref(c);
	dbus_error_free(&err);
	return has;
}

} // namespace

// Current XKB group index on the X display (-1 when no working X display).
int LinuxImeXkbGroup()
{
	Display *d = LinuxX11Display();
	if (!d)
		return -1;
	XkbStateRec st;
	if (XkbGetState(d, XkbUseCoreKbd, &st) == Success)
		return (int)st.group;
	return -1;
}

// Detect the active input-method framework (bus-name ownership).
int LinuxImeFramework()
{
	if (NameHasOwner("org.freedesktop.IBus"))
		return LINUX_IME_IBUS;
	if (NameHasOwner("org.fcitx.Fcitx5.Controller1")
		|| NameHasOwner("org.fcitx.Fcitx.Controller1"))
		return LINUX_IME_FCITX5;
	return LINUX_IME_NONE;
}