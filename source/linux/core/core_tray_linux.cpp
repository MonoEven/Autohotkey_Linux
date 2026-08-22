// Tray / notification support for the Linux port (check_detail0821 §5-M5).
//
// TrayTip is delivered through the standard org.freedesktop.Notifications
// D-Bus interface (GNOME Shell, KDE, XFCE and most shells host it), so a
// notification appears on any desktop with a notification daemon.  When no
// daemon is reachable the call degrades to a silent no-op (TrayTip is
// documented as "never a critical error").  The SNI tray icon
// (org.kde.StatusNotifierItem + com.canonical.dbusmenu) that TraySetIcon
// needs is the R2 follow-up; TraySetIcon still raises "not ported" for now.
#include "../../stdafx.h"
#include <dbus/dbus.h>
#include <cstdio>
#include <cwchar>

// Send a desktop notification.  Returns true when the daemon confirmed the
// call; false when there is no daemon / the bus is unreachable (caller keeps
// the "never a critical error" TrayTip contract).
bool LinuxTrayNotify(const wchar_t *aTitle, const wchar_t *aText)
{
	DBusError err;
	dbus_error_init(&err);
	DBusConnection *c = dbus_bus_get(DBUS_BUS_SESSION, &err);
	if (!c)
	{
		dbus_error_free(&err);
		return false;
	}
	dbus_error_free(&err);

	char summary[512], body[8192];
	if (!aTitle || wcstombs(summary, aTitle, sizeof(summary)) == (size_t)-1)
		summary[0] = '\0';
	else
		summary[sizeof(summary) - 1] = '\0';
	if (!aText || wcstombs(body, aText, sizeof(body)) == (size_t)-1)
		body[0] = '\0';
	else
		body[sizeof(body) - 1] = '\0';

	DBusMessage *msg = dbus_message_new_method_call("org.freedesktop.Notifications",
		"/org/freedesktop/Notifications", "org.freedesktop.Notifications", "Notify");
	bool ok = false;
	if (msg)
	{
		DBusMessageIter it, arr, dict;
		dbus_message_iter_init_append(msg, &it);
		const char *app = "AutoHotkey";
		dbus_uint32_t replaces = 0;
		const char *icon = "";
		const char *summary_p = summary;
		const char *body_p = body;
		dbus_int32_t timeout = -1;
		dbus_message_iter_append_basic(&it, DBUS_TYPE_STRING, &app);
		dbus_message_iter_append_basic(&it, DBUS_TYPE_UINT32, &replaces);
		dbus_message_iter_append_basic(&it, DBUS_TYPE_STRING, &icon);
		dbus_message_iter_append_basic(&it, DBUS_TYPE_STRING, &summary_p);
		dbus_message_iter_append_basic(&it, DBUS_TYPE_STRING, &body_p);
		dbus_message_iter_open_container(&it, DBUS_TYPE_ARRAY, "s", &arr);
		dbus_message_iter_close_container(&it, &arr);
		dbus_message_iter_open_container(&it, DBUS_TYPE_ARRAY, "{sv}", &dict);
		dbus_message_iter_close_container(&it, &dict);
		dbus_message_iter_append_basic(&it, DBUS_TYPE_INT32, &timeout);
		DBusMessage *rep = dbus_connection_send_with_reply_and_block(c, msg, 5000, &err);
		dbus_message_unref(msg);
		if (rep)
		{
			ok = true;
			dbus_message_unref(rep);
		}
		dbus_error_free(&err);
	}
	dbus_connection_unref(c);
	return ok;
}
