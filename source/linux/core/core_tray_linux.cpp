// Tray / notification support for the Linux port (check_detail0821 §5-M5).
//
// TrayTip is delivered through the standard org.freedesktop.Notifications
// D-Bus interface (GNOME Shell, KDE, XFCE and most shells host it), so a
// notification appears on any desktop with a notification daemon.  When no
// daemon is reachable the call degrades to a silent no-op (TrayTip is
// documented as "never a critical error").
//
// The tray icon (TraySetIcon) uses the StatusNotifierItem protocol
// (org.kde.StatusNotifierItem + com.canonical.dbusmenu): a session-bus
// service is registered with the desktop's StatusNotifierWatcher and serves
// the item properties + a default menu (Pause / Suspend / Reload / Exit)
// whose clicks invoke the corresponding script commands.  This is the
// documented replacement for the Win32 tray icon; A_TrayMenu (a
// script-customizable menu) is the R2 follow-up.  No watcher -> silent no-op.
#include "../../stdafx.h"
#include "../../script.h"
#include "../../globaldata.h"
#include "core_tray_linux.h"
#include <dbus/dbus.h>
#include <unistd.h>
#include <cstdio>
#include <cstring>
#include <cwchar>

// ---------------------------------------------------------------------------
// TrayTip -> org.freedesktop.Notifications
// ---------------------------------------------------------------------------

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

// ---------------------------------------------------------------------------
// Tray icon: StatusNotifierItem + com.canonical.dbusmenu
// ---------------------------------------------------------------------------

static DBusConnection *sSniConn = nullptr;
static char sSniBusName[128];        // org.kde.StatusNotifierItem-<pid>-<id>
static char sSniIcon[256] = "application-x-executable";
static char sSniTitle[256] = "AutoHotkey";
static dbus_uint32_t sSniRevision = 1;

// Default tray menu (id 0 = root, 1..N = items).
struct SniMenuItem { const char *label; void (*action)(); };
static void SniActionPause() { PauseCurrentThread(); }
static void SniActionSuspend() { ToggleSuspendState(); }
static void SniActionReload() { g_script.ExitApp(EXIT_RELOAD); }
static void SniActionExit() { g_script.ExitApp(EXIT_EXIT); }
static const SniMenuItem sSniMenu[] = {
	{ "Pause Script", SniActionPause },
	{ "Suspend Hotkeys", SniActionSuspend },
	{ "Reload Script", SniActionReload },
	{ "Exit", SniActionExit },
};
static const int sSniMenuCount = (int)(sizeof(sSniMenu) / sizeof(sSniMenu[0]));

// --- a{sv} appenders (a dict entry is key + variant value) ----------------

static void SniDictString(DBusMessageIter *aDict, const char *aKey, const char *aValue)
{
	DBusMessageIter entry, var;
	dbus_message_iter_open_container(aDict, DBUS_TYPE_DICT_ENTRY, nullptr, &entry);
	dbus_message_iter_append_basic(&entry, DBUS_TYPE_STRING, &aKey);
	dbus_message_iter_open_container(&entry, DBUS_TYPE_VARIANT, "s", &var);
	dbus_message_iter_append_basic(&var, DBUS_TYPE_STRING, &aValue);
	dbus_message_iter_close_container(&entry, &var);
	dbus_message_iter_close_container(aDict, &entry);
}

static void SniDictBool(DBusMessageIter *aDict, const char *aKey, dbus_bool_t aValue)
{
	DBusMessageIter entry, var;
	dbus_message_iter_open_container(aDict, DBUS_TYPE_DICT_ENTRY, nullptr, &entry);
	dbus_message_iter_append_basic(&entry, DBUS_TYPE_STRING, &aKey);
	dbus_message_iter_open_container(&entry, DBUS_TYPE_VARIANT, "b", &var);
	dbus_message_iter_append_basic(&var, DBUS_TYPE_BOOLEAN, &aValue);
	dbus_message_iter_close_container(&entry, &var);
	dbus_message_iter_close_container(aDict, &entry);
}

static void SniDictInt32(DBusMessageIter *aDict, const char *aKey, dbus_int32_t aValue)
{
	DBusMessageIter entry, var;
	dbus_message_iter_open_container(aDict, DBUS_TYPE_DICT_ENTRY, nullptr, &entry);
	dbus_message_iter_append_basic(&entry, DBUS_TYPE_STRING, &aKey);
	dbus_message_iter_open_container(&entry, DBUS_TYPE_VARIANT, "i", &var);
	dbus_message_iter_append_basic(&var, DBUS_TYPE_INT32, &aValue);
	dbus_message_iter_close_container(&entry, &var);
	dbus_message_iter_close_container(aDict, &entry);
}

static void SniDictUint32(DBusMessageIter *aDict, const char *aKey, dbus_uint32_t aValue)
{
	DBusMessageIter entry, var;
	dbus_message_iter_open_container(aDict, DBUS_TYPE_DICT_ENTRY, nullptr, &entry);
	dbus_message_iter_append_basic(&entry, DBUS_TYPE_STRING, &aKey);
	dbus_message_iter_open_container(&entry, DBUS_TYPE_VARIANT, "u", &var);
	dbus_message_iter_append_basic(&var, DBUS_TYPE_UINT32, &aValue);
	dbus_message_iter_close_container(&entry, &var);
	dbus_message_iter_close_container(aDict, &entry);
}

// --- StatusNotifierItem properties -----------------------------------------

static void SniAppendItemProps(DBusMessageIter *aDict)
{
	SniDictString(aDict, "Category", "ApplicationStatus");
	SniDictString(aDict, "Id", "autohotkey");
	SniDictString(aDict, "Title", sSniTitle);
	SniDictString(aDict, "Status", "Active");
	SniDictString(aDict, "IconName", sSniIcon);
	SniDictString(aDict, "Menu", "/MenuBar");
	SniDictBool(aDict, "ItemIsMenu", FALSE);
	SniDictUint32(aDict, "WindowId", 0);
}

static DBusMessage *SniReplyGetAll(DBusMessage *aMsg)
{
	DBusMessage *rep = dbus_message_new_method_return(aMsg);
	if (!rep)
		return nullptr;
	DBusMessageIter it, dict;
	dbus_message_iter_init_append(rep, &it);
	dbus_message_iter_open_container(&it, DBUS_TYPE_ARRAY, "{sv}", &dict);
	SniAppendItemProps(&dict);
	dbus_message_iter_close_container(&it, &dict);
	return rep;
}

static DBusMessage *SniReplyGet(DBusMessage *aMsg)
{
	const char *iface = nullptr, *prop = nullptr;
	if (!dbus_message_get_args(aMsg, nullptr, DBUS_TYPE_STRING, &iface, DBUS_TYPE_STRING, &prop, DBUS_TYPE_INVALID)
		|| !prop)
		return dbus_message_new_error(aMsg, "org.freedesktop.DBus.Error.InvalidArgs", "bad args");
	DBusMessage *rep = dbus_message_new_method_return(aMsg);
	if (!rep)
		return nullptr;
	// Get returns a SINGLE variant (v), not a {sv} dict entry.
	DBusMessageIter it, var;
	dbus_message_iter_init_append(rep, &it);
	const char *sval = "";
	dbus_bool_t bval = FALSE;
	dbus_uint32_t uval = 0;
	if (!strcmp(prop, "Category")) { sval = "ApplicationStatus"; }
	else if (!strcmp(prop, "Id")) { sval = "autohotkey"; }
	else if (!strcmp(prop, "Title")) { sval = sSniTitle; }
	else if (!strcmp(prop, "Status")) { sval = "Active"; }
	else if (!strcmp(prop, "IconName")) { sval = sSniIcon; }
	else if (!strcmp(prop, "Menu")) { sval = "/MenuBar"; }
	else if (!strcmp(prop, "ItemIsMenu")) { bval = FALSE; }
	else if (!strcmp(prop, "WindowId")) { uval = 0; }
	if (!strcmp(prop, "ItemIsMenu"))
	{
		dbus_message_iter_open_container(&it, DBUS_TYPE_VARIANT, "b", &var);
		dbus_message_iter_append_basic(&var, DBUS_TYPE_BOOLEAN, &bval);
		dbus_message_iter_close_container(&it, &var);
	}
	else if (!strcmp(prop, "WindowId"))
	{
		dbus_message_iter_open_container(&it, DBUS_TYPE_VARIANT, "u", &var);
		dbus_message_iter_append_basic(&var, DBUS_TYPE_UINT32, &uval);
		dbus_message_iter_close_container(&it, &var);
	}
	else
	{
		dbus_message_iter_open_container(&it, DBUS_TYPE_VARIANT, "s", &var);
		dbus_message_iter_append_basic(&var, DBUS_TYPE_STRING, &sval);
		dbus_message_iter_close_container(&it, &var);
	}
	return rep;
}

static DBusHandlerResult SniItemHandler(DBusConnection *aConn, DBusMessage *aMsg, void *aData)
{
	if (dbus_message_is_method_call(aMsg, "org.freedesktop.DBus.Properties", "GetAll"))
	{
		if (DBusMessage *rep = SniReplyGetAll(aMsg))
		{
			dbus_connection_send(aConn, rep, nullptr);
			dbus_message_unref(rep);
		}
		return DBUS_HANDLER_RESULT_HANDLED;
	}
	if (dbus_message_is_method_call(aMsg, "org.freedesktop.DBus.Properties", "Get"))
	{
		if (DBusMessage *rep = SniReplyGet(aMsg))
		{
			dbus_connection_send(aConn, rep, nullptr);
			dbus_message_unref(rep);
		}
		return DBUS_HANDLER_RESULT_HANDLED;
	}
	if (dbus_message_is_method_call(aMsg, "org.kde.StatusNotifierItem", "ContextMenu")
		|| dbus_message_is_method_call(aMsg, "org.kde.StatusNotifierItem", "Activate")
		|| dbus_message_is_method_call(aMsg, "org.kde.StatusNotifierItem", "SecondaryActivate"))
	{
		// No-op reply: the host drives the menu via the dbusmenu protocol.
		DBusMessage *rep = dbus_message_new_method_return(aMsg);
		if (rep)
		{
			dbus_connection_send(aConn, rep, nullptr);
			dbus_message_unref(rep);
		}
		return DBUS_HANDLER_RESULT_HANDLED;
	}
	return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
}

// --- com.canonical.dbusmenu -------------------------------------------------

#ifndef MFS_GRAYED
#define MFS_GRAYED 0x1
#endif
#ifndef MFS_DISABLED
#define MFS_DISABLED 0x2
#endif
#ifndef MFS_CHECKED
#define MFS_CHECKED 0x8
#endif

// The tray menu shown by the SNI host: the script's A_TrayMenu when it has
// items (check_detail0821 §5-M5 / R2), otherwise the built-in defaults.
static UserMenu *SniTrayMenu()
{
	UserMenu *tm = g_script.mTrayMenu;
	return (tm && tm->mFirstMenuItem) ? tm : nullptr;
}

// Invoke a script menu item's callback with A_ThisMenuItem / A_ThisMenuItemPos
// / A_ThisMenu (matches script_menu_linux.cpp's FireMenuItem).
static void SniFireUserItem(UserMenuItem *aItem)
{
	if (!aItem || !aItem->mCallback)
		return;
	UserMenu *menu = aItem->mMenu;
	menu->AddRef();
	ExprTokenType param[] = { aItem->mName, (__int64)(aItem->Pos() + 1), menu };
	aItem->mCallback->ExecuteInNewThread(_T("Menu"), param, _countof(param));
	menu->Release();
}

// Append one menu node.  aId==0 is the root (children-display submenu).
// aUserItem (script A_TrayMenu item) and aDefaultItem (built-in) are
// mutually exclusive.
static void SniAppendMenuNode(DBusMessageIter *aParent, int aId, UserMenuItem *aUserItem, const SniMenuItem *aDefaultItem)
{
	DBusMessageIter node, props, kids;
	dbus_message_iter_open_container(aParent, DBUS_TYPE_STRUCT, nullptr, &node);
	dbus_int32_t id = aId;
	dbus_message_iter_append_basic(&node, DBUS_TYPE_INT32, &id);
	dbus_message_iter_open_container(&node, DBUS_TYPE_ARRAY, "{sv}", &props);
	if (aId == 0)
	{
		SniDictString(&props, "children-display", "submenu");
	}
	else if (aUserItem)
	{
		if (!aUserItem->mName || !*aUserItem->mName)
		{
			SniDictString(&props, "type", "separator");
		}
		else
		{
			char narrow[1024];
			size_t n = wcstombs(narrow, aUserItem->mName, sizeof(narrow) - 1);
			if (n == (size_t)-1)
				n = 0;
			narrow[n] = 0;
			bool enabled = !(aUserItem->mMenuState & (MFS_DISABLED | MFS_GRAYED));
			bool checked = (aUserItem->mMenuState & MFS_CHECKED) != 0;
			SniDictString(&props, "label", narrow);
			SniDictBool(&props, "enabled", enabled ? TRUE : FALSE);
			SniDictBool(&props, "visible", TRUE);
			SniDictString(&props, "type", checked ? "checkmark" : "normal");
		}
	}
	else if (aDefaultItem)
	{
		SniDictString(&props, "label", aDefaultItem->label);
		SniDictBool(&props, "enabled", TRUE);
		SniDictBool(&props, "visible", TRUE);
		SniDictString(&props, "type", "normal");
	}
	dbus_message_iter_close_container(&node, &props);
	// Children: the dbusmenu layout carries them as an array of VARIANTS
	// ("av"), each variant wrapping a (ia{sv}av) node.  Using the raw struct
	// signature here made libdbus abort the process (GetLayout crashed).
	dbus_message_iter_open_container(&node, DBUS_TYPE_ARRAY, "v", &kids);
	if (aId == 0)
	{
		if (UserMenu *tm = SniTrayMenu())
		{
			int nid = 1;
			for (UserMenuItem *item = tm->mFirstMenuItem; item; item = item->mNextMenuItem)
			{
				DBusMessageIter kid_var;
				dbus_message_iter_open_container(&kids, DBUS_TYPE_VARIANT, "(ia{sv}av)", &kid_var);
				SniAppendMenuNode(&kid_var, nid++, item, nullptr);
				dbus_message_iter_close_container(&kids, &kid_var);
			}
		}
		else
		{
			for (int i = 0; i < sSniMenuCount; ++i)
			{
				DBusMessageIter kid_var;
				dbus_message_iter_open_container(&kids, DBUS_TYPE_VARIANT, "(ia{sv}av)", &kid_var);
				SniAppendMenuNode(&kid_var, i + 1, nullptr, &sSniMenu[i]);
				dbus_message_iter_close_container(&kids, &kid_var);
			}
		}
	}
	dbus_message_iter_close_container(&node, &kids);
	dbus_message_iter_close_container(aParent, &node);
}

static DBusMessage *SniReplyGetLayout(DBusMessage *aMsg)
{
	DBusMessage *rep = dbus_message_new_method_return(aMsg);
	if (!rep)
		return nullptr;
	DBusMessageIter it, arr;
	dbus_message_iter_init_append(rep, &it);
	dbus_message_iter_append_basic(&it, DBUS_TYPE_UINT32, &sSniRevision);
	dbus_message_iter_open_container(&it, DBUS_TYPE_ARRAY, "(ia{sv}av)", &arr);
	SniAppendMenuNode(&arr, 0, nullptr, nullptr); // root with the items as children
	dbus_message_iter_close_container(&it, &arr);
	return rep;
}

static DBusMessage *SniReplyAboutToShow(DBusMessage *aMsg)
{
	DBusMessage *rep = dbus_message_new_method_return(aMsg);
	if (!rep)
		return nullptr;
	dbus_bool_t show = TRUE;
	dbus_message_append_args(rep, DBUS_TYPE_BOOLEAN, &show, DBUS_TYPE_INVALID);
	return rep;
}

static void SniHandleEvent(DBusMessage *aMsg)
{
	// Event(i id, s eventId, v data, u timestamp).  dbus_message_get_args has
	// limited variant support, so iterate manually and skip the data variant.
	DBusMessageIter it;
	if (!dbus_message_iter_init(aMsg, &it))
		return;
	if (dbus_message_iter_get_arg_type(&it) != DBUS_TYPE_INT32)
		return;
	dbus_int32_t id = 0;
	dbus_message_iter_get_basic(&it, &id);
	dbus_message_iter_next(&it);
	if (dbus_message_iter_get_arg_type(&it) != DBUS_TYPE_STRING)
		return;
	const char *event_id = nullptr;
	dbus_message_iter_get_basic(&it, &event_id);
	dbus_message_iter_next(&it); // skip the data variant
	if (!event_id || strcmp(event_id, "clicked") || id < 1)
		return;
	if (UserMenu *tm = SniTrayMenu())
	{
		int idx = 1;
		for (UserMenuItem *item = tm->mFirstMenuItem; item; item = item->mNextMenuItem, ++idx)
		{
			if (idx == id)
			{
				SniFireUserItem(item); // Runs on the main dispatch thread.
				break;
			}
		}
	}
	else if (id <= sSniMenuCount)
		sSniMenu[id - 1].action(); // Runs on the main dispatch thread.
}

static DBusHandlerResult SniMenuHandler(DBusConnection *aConn, DBusMessage *aMsg, void *aData)
{
	DBusMessage *rep = nullptr;
	if (dbus_message_is_method_call(aMsg, "com.canonical.dbusmenu", "GetLayout"))
		rep = SniReplyGetLayout(aMsg);
	else if (dbus_message_is_method_call(aMsg, "com.canonical.dbusmenu", "AboutToShow"))
		rep = SniReplyAboutToShow(aMsg);
	else if (dbus_message_is_method_call(aMsg, "com.canonical.dbusmenu", "Event"))
	{
		SniHandleEvent(aMsg);
		rep = dbus_message_new_method_return(aMsg);
	}
	else
		return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
	if (rep)
	{
		dbus_connection_send(aConn, rep, nullptr);
		dbus_message_unref(rep);
	}
	return DBUS_HANDLER_RESULT_HANDLED;
}

// --- registration -----------------------------------------------------------

static void SniRegisterWithWatcher(DBusConnection *aConn, const char *aWatcherName)
{
	// Register with the WELL-KNOWN item name (org.kde.StatusNotifierItem-<pid>).
	// The ayatana watcher's cleanup relies on its NameWatcher for the item's
	// name, which is only set up when uniqueId === service (the well-known
	// form).  The unique-name+path form registers but is never cleaned up on
	// the process exit (Gio.DBusProxy does not observe unique-name vanishes),
	// so items leaked -- verified on the GNOME VM (check_detail0821 §5-M5).
	DBusMessage *m = dbus_message_new_method_call(aWatcherName, "/StatusNotifierWatcher"
		, "org.kde.StatusNotifierWatcher", "RegisterStatusNotifierItem");
	if (!m)
		return;
	const char *service = sSniBusName;
	dbus_message_append_args(m, DBUS_TYPE_STRING, &service, DBUS_TYPE_INVALID);
	dbus_connection_send(aConn, m, nullptr);
	dbus_message_unref(m);
}

static bool SniEnsure()
{
	if (sSniConn)
		return true;
	DBusError err;
	dbus_error_init(&err);
	// A PRIVATE connection: the shared session connection is also pumped by
	// the input-backend code, and its name request / object registrations can
	// interfere; the SNI service owns its own connection.
	DBusConnection *conn = dbus_bus_get_private(DBUS_BUS_SESSION, &err);
	if (!conn)
	{
		fprintf(stderr, "AHK tray: no session bus: %s\n", err.message ? err.message : "?");
		dbus_error_free(&err);
		return false;
	}
	dbus_error_free(&err);
	dbus_connection_set_exit_on_disconnect(conn, FALSE);
	snprintf(sSniBusName, sizeof(sSniBusName), "org.kde.StatusNotifierItem-%lu-1"
		, (unsigned long)getpid());
	dbus_bus_request_name(conn, sSniBusName, DBUS_NAME_FLAG_DO_NOT_QUEUE, &err);
	if (dbus_error_is_set(&err))
		dbus_error_free(&err);
	DBusObjectPathVTable vt_item = { nullptr, SniItemHandler, nullptr, nullptr, nullptr, nullptr };
	DBusObjectPathVTable vt_menu = { nullptr, SniMenuHandler, nullptr, nullptr, nullptr, nullptr };
	dbus_connection_register_object_path(conn, "/StatusNotifierItem", &vt_item, nullptr);
	dbus_connection_register_object_path(conn, "/MenuBar", &vt_menu, nullptr);
	sSniConn = conn;
	// The host may own either well-known watcher name (KDE/swaybar -> org.kde,
	// some hosts -> org.freedesktop); register with both, senders ignored.
	SniRegisterWithWatcher(conn, "org.kde.StatusNotifierWatcher");
	SniRegisterWithWatcher(conn, "org.freedesktop.StatusNotifierWatcher");
	return true;
}

// --- public API -------------------------------------------------------------

bool LinuxTraySetIcon(const wchar_t *aIconFile)
{
	// Reduce a path to a themed icon name (basename without extension); a
	// bare name is used as-is.  Best effort; the host renders it.
	if (aIconFile && *aIconFile)
	{
		char narrow[256];
		size_t n = wcstombs(narrow, aIconFile, sizeof(narrow) - 1);
		if (n == (size_t)-1)
			n = 0;
		narrow[n] = 0;
		const char *base = strrchr(narrow, '/');
		base = base ? base + 1 : narrow;
		char name[256];
		snprintf(name, sizeof(name), "%s", base);
		char *dot = strrchr(name, '.');
		if (dot && dot != name)
			*dot = 0; // strip extension for a themed name
		if (name[0])
			strncpy(sSniIcon, name, sizeof(sSniIcon) - 1);
	}
	else
		strncpy(sSniIcon, "application-x-executable", sizeof(sSniIcon) - 1);
	if (!SniEnsure())
		return false;
	// Notify hosts that the icon changed.
	DBusMessage *sig = dbus_message_new_signal("/StatusNotifierItem"
		, "org.kde.StatusNotifierItem", "NewIcon");
	if (sig)
	{
		dbus_connection_send(sSniConn, sig, nullptr);
		dbus_message_unref(sig);
	}
	return true;
}

void LinuxTrayDispatch()
{
	if (!sSniConn || !dbus_connection_get_is_connected(sSniConn))
		return;
	dbus_connection_read_write_dispatch(sSniConn, 0);
}
