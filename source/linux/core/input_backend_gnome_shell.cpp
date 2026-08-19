// GNOME Shell extension backend (see input_backend_gnome_shell.h).
//
// Wire protocol (verified against gnome-shell 49.0 sources + mutter 49.0):
//  The extension registers a session-bus service
//    io.github.autohotkey.GlobalHotkeys1
//  with methods Register(u id, s accelerator, u flags) -> b,
//  Unregister(u id) -> b, ClearOwner(s owner) and signals
//  Activated(u id, u timestamp), Deactivated(u id, u timestamp).
//  The extension uses global.display.grab_accelerator() followed by
//  Main.wm.allowKeybinding() - the exact whitelist step that
//  xdg-desktop-portal-gnome performs via shellDBus GrabAccelerators
//  (verified in windowManager.js: _filterKeybinding() consults
//  Main.wm._allowedKeybindings; without allowKeybinding the binding is
//  registered in mutter but silently filtered and never activates).
//
// Registration model:
//  Each script/process is an independent D-Bus peer (owner).  The extension
//  tracks owner = unique bus name; ClearOwner is sent on shutdown so a dead
//  script can never leave grabs behind (the extension also cleans up on
//  name-vanished, fail-open either way).
#include "input_backend_gnome_shell.h"
#include "input_backend.h"
#include "../../hotkey.h"
#include <dbus/dbus.h>
#include <cstring>
#include <cstdlib>
#include <map>
#include <string>

namespace
{

#define GS_BUS_NAME "io.github.autohotkey.GlobalHotkeys1"
#define GS_OBJ_PATH "/io/github/autohotkey/GlobalHotkeys1"
#define GS_IFACE    "io.github.autohotkey.GlobalHotkeys1"

DBusConnection *sConn = nullptr;
char *sOwnerName = nullptr; // Our unique bus name ("owner" for ClearOwner).
bool sFailed = false;       // Sticky connection failure.
bool sRegistered = false;   // At least one registration is live.
wchar_t sLastErrorBuf[512] = { 0 };

// Desired registration set (id -> accelerator), keyed by hotkey pointer
// address so Sync() can diff cheaply.
struct RegEntry
{
	std::string accel;
	Hotkey *hk;
};
std::map<std::string, RegEntry> sWanted;   // desired (id -> entry)
std::map<std::string, RegEntry> sLive;     // actually registered

// Set when the extension's name went away (reload / shell restart) while the
// session bus stayed up: registrations are gone in the compositor, so the
// next Dispatch must re-sync instead of waiting for a hotkey state change.
bool sNeedResync = false;

#define GS_MAX_PENDING 64
Hotkey *sPendingHk[GS_MAX_PENDING];
unsigned sPendingTs[GS_MAX_PENDING];
int sPendingCount = 0;

void SetError(const char *aText)
{
	mbstowcs(sLastErrorBuf, aText ? aText : "", _countof(sLastErrorBuf) - 1);
	sLastErrorBuf[_countof(sLastErrorBuf) - 1] = 0;
}

std::string IdForHotkey(Hotkey *aHk)
{
	char id[48];
	snprintf(id, sizeof(id), "ahk_%p", (void *)aHk);
	return std::string(id);
}

DBusHandlerResult Handler(DBusConnection *, DBusMessage *, void *); // forward

// --- D-Bus plumbing ---------------------------------------------------------

bool EnsureConnection()
{
	if (sConn)
		return true;
	if (sFailed)
		return false;
	DBusError err;
	dbus_error_init(&err);
	sConn = dbus_bus_get(DBUS_BUS_SESSION, &err);
	if (!sConn)
	{
		SetError(err.message ? err.message : "no session bus");
		dbus_error_free(&err);
		sFailed = true;
		return false;
	}
	dbus_error_free(&err);
	dbus_connection_set_exit_on_disconnect(sConn, FALSE);
	// Subscribe to the broker's signals (Activated/Deactivated).  Without a
	// match rule the bus daemon never forwards the broadcast to this client.
	DBusError em;
	dbus_error_init(&em);
	char rule[256];
	snprintf(rule, sizeof(rule),
		"type='signal',interface='%s'", GS_IFACE);
	dbus_bus_add_match(sConn, rule, &em);
	if (dbus_error_is_set(&em))
	{
		SetError(em.message);
		dbus_error_free(&em);
		sFailed = true;
		dbus_connection_close(sConn);
		dbus_connection_unref(sConn);
		sConn = nullptr;
		return false;
	}
	dbus_error_free(&em);
	// Also watch the broker's well-known name: an extension reload
	// (disable/enable) drops every grab while the session bus stays up, so a
	// NameOwnerChanged (owner going away) is the cue to forget our projection
	// state; the next Sync re-registers everything.
	{
		char rule2[256];
		snprintf(rule2, sizeof(rule2),
			"type='signal',interface='org.freedesktop.DBus',member='NameOwnerChanged',"
			"arg0='%s'", GS_BUS_NAME);
		DBusError em2;
		dbus_error_init(&em2);
		dbus_bus_add_match(sConn, rule2, &em2);
		if (dbus_error_is_set(&em2))
			dbus_error_free(&em2);
		else
			dbus_error_free(&em2);
	}
	// Route matched messages through our signal handler.
	dbus_connection_add_filter(sConn, Handler, nullptr, nullptr);
	const char *u = dbus_bus_get_unique_name(sConn);
	if (u)
		sOwnerName = strdup(u);
	return true;
}

// Synchronous call with a (b) reply; returns the boolean or false on error.
bool CallBool(const char *aMethod, DBusMessage *aMsg)
{
	if (!aMsg)
		return false;
	DBusError err;
	dbus_error_init(&err);
	DBusMessage *rep = dbus_connection_send_with_reply_and_block(sConn, aMsg, 10000, &err);
	dbus_message_unref(aMsg);
	if (!rep)
	{
		SetError(err.message ? err.message : aMethod);
		dbus_error_free(&err);
		return false;
	}
	dbus_error_free(&err);
	bool ok = false;
	DBusMessageIter it;
	dbus_message_iter_init(rep, &it);
	if (dbus_message_iter_get_arg_type(&it) == DBUS_TYPE_BOOLEAN)
	{
		dbus_bool_t b = FALSE;
		dbus_message_iter_get_basic(&it, &b);
		ok = b != FALSE;
	}
	dbus_message_unref(rep);
	if (!ok)
	{
		char buf[160];
		snprintf(buf, sizeof(buf), "GNOME Shell backend: %s rejected", aMethod);
		SetError(buf);
	}
	return ok;
}

bool SendRegister(const std::string &aId, const std::string &aAccel)
{
	DBusMessage *msg = dbus_message_new_method_call(GS_BUS_NAME, GS_OBJ_PATH,
		GS_IFACE, "Register");
	if (!msg) return false;
	DBusMessageIter it;
	dbus_message_iter_init_append(msg, &it);
	const char *id = aId.c_str();
	const char *accel = aAccel.c_str();
	dbus_uint32_t flags = 0; // exclusive (suppress) - extension's only mode today
	dbus_message_iter_append_basic(&it, DBUS_TYPE_STRING, &id);
	dbus_message_iter_append_basic(&it, DBUS_TYPE_STRING, &accel);
	dbus_message_iter_append_basic(&it, DBUS_TYPE_UINT32, &flags);
	return CallBool("Register", msg);
}

bool SendUnregister(const std::string &aId)
{
	DBusMessage *msg = dbus_message_new_method_call(GS_BUS_NAME, GS_OBJ_PATH,
		GS_IFACE, "Unregister");
	if (!msg) return false;
	DBusMessageIter it;
	dbus_message_iter_init_append(msg, &it);
	const char *id = aId.c_str();
	dbus_message_iter_append_basic(&it, DBUS_TYPE_STRING, &id);
	return CallBool("Unregister", msg);
}

void SendClearOwner()
{
	if (!sConn || !sOwnerName)
		return;
	DBusMessage *msg = dbus_message_new_method_call(GS_BUS_NAME, GS_OBJ_PATH,
		GS_IFACE, "ClearOwner");
	if (!msg) return;
	DBusMessageIter it;
	dbus_message_iter_init_append(msg, &it);
	const char *owner = sOwnerName;
	dbus_message_iter_append_basic(&it, DBUS_TYPE_STRING, &owner);
	DBusMessage *rep = dbus_connection_send_with_reply_and_block(sConn, msg, 10000, nullptr);
	dbus_message_unref(msg); // send_with_reply_and_block refs internally; we own ours.
	if (rep)
		dbus_message_unref(rep);
}

// --- D-Bus filter: Activated/Deactivated signals ----------------------------

DBusHandlerResult Handler(DBusConnection *, DBusMessage *aMsg, void *)
{
	const char *iface = dbus_message_get_interface(aMsg);
	const char *member = dbus_message_get_member(aMsg);
	if (!iface || !member)
		return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;

	// Extension reload (disable/enable): the well-known name loses its owner,
	// which drops every grab in the compositor.  Forget our projection state
	// so the next Sync re-registers everything (state truth is the runtime).
	if (!strcmp(iface, "org.freedesktop.DBus") && !strcmp(member, "NameOwnerChanged"))
	{
		DBusMessageIter it;
		dbus_message_iter_init(aMsg, &it);
		if (dbus_message_iter_get_arg_type(&it) == DBUS_TYPE_STRING)
		{
			const char *name = nullptr;
			dbus_message_iter_get_basic(&it, &name);
			if (name && !strcmp(name, GS_BUS_NAME))
			{
				// arg1 = old owner, arg2 = new owner.  Any change invalidates
				// the projection (the compositor dropped our grabs); when a
				// new owner appears we can re-register immediately.
				const char *newOwner = nullptr;
				if (dbus_message_iter_next(&it) &&
					dbus_message_iter_get_arg_type(&it) == DBUS_TYPE_STRING)
				{
					// arg1 (old owner)
					dbus_message_iter_get_basic(&it, &newOwner);
					(void)newOwner;
				}
				if (dbus_message_iter_next(&it) &&
					dbus_message_iter_get_arg_type(&it) == DBUS_TYPE_STRING)
				{
					// arg2 (new owner)
					dbus_message_iter_get_basic(&it, &newOwner);
				}
				sLive.clear();
				sRegistered = false;
				if (newOwner && newOwner[0])
					sNeedResync = true; // new extension instance is live
			}
		}
		return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
	}

	if (strcmp(iface, GS_IFACE) != 0)
		return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;

	DBusMessageIter it;
	dbus_message_iter_init(aMsg, &it);
	if (dbus_message_iter_get_arg_type(&it) != DBUS_TYPE_STRING)
		return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
	const char *id = nullptr;
	dbus_message_iter_get_basic(&it, &id);
	unsigned ts = 0;
	if (dbus_message_iter_next(&it) && dbus_message_iter_get_arg_type(&it) == DBUS_TYPE_UINT32)
	{
		dbus_uint32_t t = 0;
		dbus_message_iter_get_basic(&it, &t);
		ts = (unsigned)t;
	}
	if (!id || sPendingCount >= GS_MAX_PENDING)
		return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;

	// Resolve the id to the registered Hotkey once, here (off the hot path).
	Hotkey *hk = nullptr;
	for (std::map<std::string, RegEntry>::iterator l = sLive.begin(); l != sLive.end(); ++l)
	{
		if (l->first == id)
		{
			hk = l->second.hk;
			break;
		}
	}
	sPendingHk[sPendingCount] = hk;
	sPendingTs[sPendingCount] = ts;
	sPendingCount++;
	(void)member; // Activated vs Deactivated: v1 only consumes Activated.
	return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
}

} // namespace

// --- public API -------------------------------------------------------------

bool LinuxGnomeShellAvailable()
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
	DBusMessage *msg = dbus_message_new_method_call("org.freedesktop.DBus",
		"/org/freedesktop/DBus", "org.freedesktop.DBus", "NameHasOwner");
	if (!msg)
	{
		dbus_connection_unref(c);
		return false;
	}
	DBusMessageIter it;
	dbus_message_iter_init_append(msg, &it);
	const char *name = GS_BUS_NAME;
	dbus_message_iter_append_basic(&it, DBUS_TYPE_STRING, &name);
	DBusMessage *rep = dbus_connection_send_with_reply_and_block(c, msg, 3000, nullptr);
	dbus_connection_unref(c);
	if (!rep)
		return false;
	bool has = false;
	DBusMessageIter rit;
	dbus_message_iter_init(rep, &rit);
	if (dbus_message_iter_get_arg_type(&rit) == DBUS_TYPE_BOOLEAN)
	{
		dbus_bool_t b = FALSE;
		dbus_message_iter_get_basic(&rit, &b);
		has = b != FALSE;
	}
	dbus_message_unref(rep);
	return has;
}

void LinuxGnomeShellSync()
{
	if (sFailed)
		return;
	if (!EnsureConnection())
		return;

	// Rebuild the desired set.
	sWanted.clear();
	for (int i = 0; i < Hotkey::sHotkeyCount; ++i)
	{
		Hotkey *hk = Hotkey::shk[i];
		if (!AhkBackendHotkeyEnabled(hk)) continue;
		if (hk->mType != HK_NORMAL && hk->mType != HK_KEYBD_HOOK) continue;
		std::string accel = AhkBackendComboForHotkey(hk);
		if (accel.empty()) continue;
		RegEntry e = { accel, hk };
		sWanted[IdForHotkey(hk)] = e;
	}

	// Diff: unregister gone, register new.
	for (std::map<std::string, RegEntry>::iterator l = sLive.begin(); l != sLive.end();)
	{
		if (sWanted.find(l->first) == sWanted.end())
		{
			SendUnregister(l->first);
			sLive.erase(l++);
			continue;
		}
		++l;
	}
	for (std::map<std::string, RegEntry>::iterator w = sWanted.begin(); w != sWanted.end(); ++w)
	{
		if (sLive.find(w->first) != sLive.end())
			continue;
		if (SendRegister(w->first, w->second.accel))
			sLive[w->first] = w->second;
	}
	sRegistered = !sLive.empty();
}

void LinuxGnomeShellDispatch()
{
	if (!sConn)
	{
		// Extension reload / shell restart: keep re-syncing on a slow cadence
		// so registrations come back once the extension re-owns its name.
		// (The main loop calls Dispatch continuously; retry every ~0.5 s.)
		static int retry = 0;
		if ((++retry & 7) == 0)
		{
			sFailed = false; // allow a fresh connect attempt
			LinuxGnomeShellSync();
		}
		return;
	}
	if (dbus_connection_read_write_dispatch(sConn, 0) == FALSE)
	{
		// Connection lost (extension reloaded, shell restarted, bus bounced):
		// drop the projection state and reconnect+re-sync.  State truth lives
		// in the AHK runtime; the extension is only a compositor projection,
		// so re-registering everything after a restart is the recovery path.
		sLive.clear();
		sRegistered = false;
		sNeedResync = true;
		SetError("GNOME Shell backend: connection lost (reconnecting)");
		dbus_connection_remove_filter(sConn, Handler, nullptr);
		dbus_connection_close(sConn);
		dbus_connection_unref(sConn);
		sConn = nullptr;
		sFailed = false; // Allow a fresh connection with a new match rule.
		if (sOwnerName) { free(sOwnerName); sOwnerName = nullptr; }
		LinuxGnomeShellSync();
		return;
	}
	// Extension reload with a live bus: NameOwnerChanged cleared sLive; the
	// compositor grabs are gone, so re-register the full desired set now.
	if (sNeedResync)
	{
		sNeedResync = false;
		LinuxGnomeShellSync();
	}
	if (sPendingCount)
	{
		Hotkey *hks[GS_MAX_PENDING];
		int n = sPendingCount;
		for (int i = 0; i < n; ++i) hks[i] = sPendingHk[i];
		sPendingCount = 0;
		for (int i = 0; i < n; ++i)
			if (hks[i])
				AhkBackendFireHotkey(hks[i]);
	}
}

void LinuxGnomeShellShutdown()
{
	if (sConn)
	{
		SendClearOwner();
		dbus_connection_remove_filter(sConn, Handler, nullptr);
		dbus_connection_close(sConn);
		dbus_connection_unref(sConn);
		sConn = nullptr;
	}
	if (sOwnerName) { free(sOwnerName); sOwnerName = nullptr; }
	sLive.clear();
	sWanted.clear();
	sRegistered = false;
	sFailed = false;
}

const wchar_t *LinuxGnomeShellLastError()
{
	return sLastErrorBuf;
}

bool LinuxGnomeShellActive()
{
	return sConn && sRegistered;
}
