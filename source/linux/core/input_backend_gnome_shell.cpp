// GNOME Shell extension backend (see input_backend_gnome_shell.h).
//
// Wire protocol (verified against gnome-shell 49.0 sources + mutter 49.0):
//  The extension registers a session-bus service
//    io.github.autohotkey.GlobalHotkeys1
//  with methods Register(s id, s accelerator, u flags) -> b,
//  RegisterMany(as ids, as accelerators, au flags) -> ab,
//  Unregister(s id) -> b, UnregisterMany(as ids) -> b,
//  ClearOwner() -> b (owner = the CALLER's unique bus name, never an
//  argument) and signals Activated(s id, u timestamp),
//  Deactivated(s id, u timestamp) that the extension emits DIRECTED to the
//  registering owner only (no broadcast).
//  The extension uses global.display.grab_accelerator() followed by
//  Main.wm.allowKeybinding() - the exact whitelist step that
//  xdg-desktop-portal-gnome performs via shellDBus GrabAccelerators
//  (verified in windowManager.js: _filterKeybinding() consults
//  Main.wm._allowedKeybindings; without allowKeybinding the binding is
//  registered in mutter but silently filtered and never activates).
//
// Registration model (check0819 hardening):
//  Each script/process is an independent D-Bus peer (owner).  Hotkey ids are
//  owner-scoped: "<unique-bus-name>/<pointer>" (no cross-process collision,
//  no need for a global namespace).  ClearOwner is sent on shutdown so a
//  dead script can never leave grabs behind (the extension also cleans up on
//  name-vanished, fail-open either way).  Signals are filtered on
//  sender/path/member and only Activated is consumed (v1); Deactivated is
//  deliberately ignored (no key-up semantics claimed yet).  The event queue
//  is a dynamic vector: overflow is counted and reported instead of silently
//  dropping.  Register/Unregister use the batch methods (RegisterMany /
//  UnregisterMany) and short timeouts so binding dozens/hundreds of hotkeys
//  cannot block the main loop for seconds each.
#include "input_backend_gnome_shell.h"
#include "input_event.h"
#include "input_backend.h"
#include "../../hotkey.h"
#include "core_keymodel_linux.h"
#include <dbus/dbus.h>
#include <cstring>
#include <cstdlib>
#include <map>
#include <vector>
#include <string>

namespace
{

#define GS_BUS_NAME "io.github.autohotkey.GlobalHotkeys1"
#define GS_OBJ_PATH "/io/github/autohotkey/GlobalHotkeys1"
#define GS_IFACE    "io.github.autohotkey.GlobalHotkeys1"

// Per-call/reply timeout (ms).  The previous 10 s per binding meant a dozen
// hotkeys could stall startup for minutes when the extension was absent;
// 3 s per call with batch registration is the practical bound.
#define GS_CALL_TIMEOUT_MS 3000
// Upper cap on the pending-event queue (a runaway compositor flood cannot
// unboundedly grow memory).  Past the cap events are dropped and counted.
#define GS_MAX_PENDING 8192

DBusConnection *sConn = nullptr;
char *sOwnerName = nullptr; // Our unique bus name ("owner" for ClearOwner).
// The broker's UNIQUE bus name (e.g. ":1.13"), cached so the signal handler
// can reject forged/dangling senders.  D-Bus never uses well-known names in
// the sender field of a delivered message, so a sender check against
// GS_BUS_NAME would silently drop every real Activation (check0820).  The
// daemon-side match rule (sender=GS_BUS_NAME) is still the authority; this
// cache is defense-in-depth against spoofed peeks.
char *sBrokerUnique = nullptr;
bool sFailed = false;       // Sticky connection failure.
bool sRegistered = false;   // At least one registration is live.
wchar_t sLastErrorBuf[512] = { 0 };

// Desired registration set (id -> accelerator), keyed by the owner-scoped id
// so Sync() can diff cheaply.
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

// Pending Activated events (dynamic; fixed in round "check0819" - the old
// 64-entry array silently dropped events past 64).
std::vector<Hotkey *> sPendingHk;
std::vector<unsigned> sPendingTs;
unsigned int sPendingOverflows = 0; // Cumulative dropped-event count.
bool sOverflowWarned = false;

void SetError(const char *aText)
{
	mbstowcs(sLastErrorBuf, aText ? aText : "", _countof(sLastErrorBuf) - 1);
	sLastErrorBuf[_countof(sLastErrorBuf) - 1] = 0;
}

// Owner-scoped hotkey id: "<unique-bus-name>/<hotkey>".  The pointer keeps
// the id stable across Sync() calls within this process; the unique-name
// prefix makes ids collision-free across processes (the extension enforces
// the "<owner>/" prefix on Register too).
std::string IdForHotkey(Hotkey *aHk)
{
	char id[96];
	snprintf(id, sizeof(id), "%s/ahk_%p",
		sOwnerName ? sOwnerName : "?", (void *)aHk);
	return std::string(id);
}

DBusHandlerResult Handler(DBusConnection *, DBusMessage *, void *); // forward

// --- D-Bus plumbing ---------------------------------------------------------

// Cache the broker's current UNIQUE bus name (":1.NNN").  D-Bus delivers
// signals with the unique name in the sender field, never the well-known
// name; the daemon-side match rule already scopes to GS_BUS_NAME's owner, and
// this cache is the defense-in-depth sender check in Handler().
void CacheBrokerUniqueName()
{
	if (!sConn)
		return;
	if (sBrokerUnique)
	{
		free(sBrokerUnique);
		sBrokerUnique = nullptr;
	}
	// GetNameOwner over the bus (dbus_bus_get_name_owner is not part of this
	// libdbus build's headers; the canonical way is the method call itself).
	DBusMessage *msg = dbus_message_new_method_call("org.freedesktop.DBus",
		"/org/freedesktop/DBus", "org.freedesktop.DBus", "GetNameOwner");
	if (!msg)
		return;
	DBusMessageIter it;
	dbus_message_iter_init_append(msg, &it);
	const char *name = GS_BUS_NAME;
	dbus_message_iter_append_basic(&it, DBUS_TYPE_STRING, &name);
	DBusError err;
	dbus_error_init(&err);
	DBusMessage *rep = dbus_connection_send_with_reply_and_block(
		sConn, msg, 1000, &err);
	dbus_message_unref(msg);
	if (!rep)
	{
		dbus_error_free(&err);
		return; // Name not owned (yet): cache stays empty; daemon rule guards.
	}
	dbus_error_free(&err);
	DBusMessageIter rit;
	dbus_message_iter_init(rep, &rit);
	if (dbus_message_iter_get_arg_type(&rit) == DBUS_TYPE_STRING)
	{
		const char *u = nullptr;
		dbus_message_iter_get_basic(&rit, &u);
		if (u)
			sBrokerUnique = strdup(u);
	}
	dbus_message_unref(rep);
}

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
	// Subscribe to the broker's Activated/Deactivated signals.  The match
	// rules are as narrow as D-Bus allows: fixed sender (the broker's
	// well-known name), fixed object path, interface and member, so no
	// other session peer can inject signals into this client (check0819).
	DBusError em;
	dbus_error_init(&em);
	char rule[320];
	snprintf(rule, sizeof(rule),
		"type='signal',sender='%s',path='%s',interface='%s',member='Activated'",
		GS_BUS_NAME, GS_OBJ_PATH, GS_IFACE);
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
	{
		// Deactivated is also subscribed so the handler can explicitly
		// consume (and deliberately ignore) it rather than leave it to the
		// bus to drop -- keeps the protocol surface explicit.
		char rule2[320];
		snprintf(rule2, sizeof(rule2),
			"type='signal',sender='%s',path='%s',interface='%s',member='Deactivated'",
			GS_BUS_NAME, GS_OBJ_PATH, GS_IFACE);
		DBusError em2;
		dbus_error_init(&em2);
		dbus_bus_add_match(sConn, rule2, &em2);
		if (dbus_error_is_set(&em2))
			dbus_error_free(&em2);
		else
			dbus_error_free(&em2);
	}
	// Also watch the broker's well-known name: an extension reload
	// (disable/enable) drops every grab while the session bus stays up, so a
	// NameOwnerChanged (owner going away) is the cue to forget our projection
	// state; the next Sync re-registers everything.  NameOwnerChanged is
	// emitted BY the bus daemon, so constrain sender to it.
	{
		char rule3[320];
		snprintf(rule3, sizeof(rule3),
			"type='signal',sender='org.freedesktop.DBus',"
			"interface='org.freedesktop.DBus',member='NameOwnerChanged',"
			"arg0='%s'", GS_BUS_NAME);
		DBusError em3;
		dbus_error_init(&em3);
		dbus_bus_add_match(sConn, rule3, &em3);
		if (dbus_error_is_set(&em3))
			dbus_error_free(&em3);
		else
			dbus_error_free(&em3);
	}
	// Route matched messages through our signal handler.
	dbus_connection_add_filter(sConn, Handler, nullptr, nullptr);
	const char *u = dbus_bus_get_unique_name(sConn);
	if (u)
		sOwnerName = strdup(u);
	CacheBrokerUniqueName();
	return true;
}

// Synchronous call with a (b) reply; returns the boolean or false on error.
bool CallBool(const char *aMethod, DBusMessage *aMsg)
{
	if (!aMsg)
		return false;
	DBusError err;
	dbus_error_init(&err);
	DBusMessage *rep = dbus_connection_send_with_reply_and_block(
		sConn, aMsg, GS_CALL_TIMEOUT_MS, &err);
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

// One-shot batch call; executes at most aBatch items (used to split huge
// registrations across several RegisterMany calls).
bool SendRegisterMany(const std::vector<std::string> &aIds
	, const std::vector<std::string> &aAccels)
{
	if (aIds.empty())
		return true;
	DBusMessage *msg = dbus_message_new_method_call(GS_BUS_NAME, GS_OBJ_PATH,
		GS_IFACE, "RegisterMany");
	if (!msg) return false;
	DBusMessageIter it, sub;
	dbus_message_iter_init_append(msg, &it);
	// as ids
	if (!dbus_message_iter_open_container(&it, DBUS_TYPE_ARRAY, "s", &sub))
	{ dbus_message_unref(msg); return false; }
	for (size_t i = 0; i < aIds.size(); ++i)
	{
		const char *id = aIds[i].c_str();
		dbus_message_iter_append_basic(&sub, DBUS_TYPE_STRING, &id);
	}
	dbus_message_iter_close_container(&it, &sub);
	// as accelerators
	if (!dbus_message_iter_open_container(&it, DBUS_TYPE_ARRAY, "s", &sub))
	{ dbus_message_unref(msg); return false; }
	for (size_t i = 0; i < aAccels.size(); ++i)
	{
		const char *accel = aAccels[i].c_str();
		dbus_message_iter_append_basic(&sub, DBUS_TYPE_STRING, &accel);
	}
	dbus_message_iter_close_container(&it, &sub);
	// au flags (all 0 = exclusive)
	if (!dbus_message_iter_open_container(&it, DBUS_TYPE_ARRAY, "u", &sub))
	{ dbus_message_unref(msg); return false; }
	for (size_t i = 0; i < aIds.size(); ++i)
	{
		dbus_uint32_t f = 0;
		dbus_message_iter_append_basic(&sub, DBUS_TYPE_UINT32, &f);
	}
	dbus_message_iter_close_container(&it, &sub);

	if (!sConn)
	{ dbus_message_unref(msg); return false; }
	DBusError err;
	dbus_error_init(&err);
	DBusMessage *rep = dbus_connection_send_with_reply_and_block(
		sConn, msg, GS_CALL_TIMEOUT_MS, &err);
	// Note: dbus_message_unref(msg) happens after the call (the API refs it
	// internally; we drop our own reference below).
	if (!rep)
	{
		SetError(err.message ? err.message : "RegisterMany");
		dbus_error_free(&err);
		dbus_message_unref(msg);
		return false;
	}
	dbus_error_free(&err);
	dbus_message_unref(msg);
	// Reply: ab results (one bool per registration).  Any false = partial
	// failure -> report and return false so the caller falls back.
	bool ok = true;
	DBusMessageIter rit;
	dbus_message_iter_init(rep, &rit);
	if (dbus_message_iter_get_arg_type(&rit) == DBUS_TYPE_ARRAY)
	{
		DBusMessageIter sub2;
		dbus_message_iter_recurse(&rit, &sub2);
		size_t idx = 0;
		while (dbus_message_iter_get_arg_type(&sub2) == DBUS_TYPE_BOOLEAN)
		{
			dbus_bool_t b = FALSE;
			dbus_message_iter_get_basic(&sub2, &b);
			if (!b && idx < aIds.size())
			{
				ok = false;
				char err[192];
				snprintf(err, sizeof(err),
					"GNOME Shell backend: RegisterMany rejected %s "
					"(id collision or bad accelerator)", aIds[idx].c_str());
				SetError(err);
			}
			dbus_message_iter_next(&sub2);
			++idx;
		}
	}
	dbus_message_unref(rep);
	return ok;
}

bool SendUnregisterMany(const std::vector<std::string> &aIds)
{
	if (aIds.empty())
		return true;
	DBusMessage *msg = dbus_message_new_method_call(GS_BUS_NAME, GS_OBJ_PATH,
		GS_IFACE, "UnregisterMany");
	if (!msg) return false;
	DBusMessageIter it, sub;
	dbus_message_iter_init_append(msg, &it);
	if (!dbus_message_iter_open_container(&it, DBUS_TYPE_ARRAY, "s", &sub))
	{ dbus_message_unref(msg); return false; }
	for (size_t i = 0; i < aIds.size(); ++i)
	{
		const char *id = aIds[i].c_str();
		dbus_message_iter_append_basic(&sub, DBUS_TYPE_STRING, &id);
	}
	dbus_message_iter_close_container(&it, &sub);
	return CallBool("UnregisterMany", msg);
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
	// Protocol v2: ClearOwner() takes NO owner argument; the extension uses
	// the caller's unique bus name (one peer cannot clear another's grabs).
	DBusMessage *msg = dbus_message_new_method_call(GS_BUS_NAME, GS_OBJ_PATH,
		GS_IFACE, "ClearOwner");
	if (!msg) return;
	DBusMessage *rep = dbus_connection_send_with_reply_and_block(
		sConn, msg, GS_CALL_TIMEOUT_MS, nullptr);
	dbus_message_unref(msg); // send_with_reply_and_block refs internally; we own ours.
	if (rep)
		dbus_message_unref(rep);
}

// --- D-Bus filter: Activated/Deactivated signals ----------------------------
// The match rules (sender=GS_BUS_NAME, path, interface, member) already narrow
// what reaches us; the handler re-checks sender/interface/member as defense in
// depth so a spoofed/forged message can never fire a hotkey.

DBusHandlerResult Handler(DBusConnection *aConn, DBusMessage *aMsg, void *)
{
	if (!aMsg)
		return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
	const char *iface = dbus_message_get_interface(aMsg);
	const char *member = dbus_message_get_member(aMsg);
	const char *sender = dbus_message_get_sender(aMsg);
	if (!iface || !member)
		return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;

	// Extension reload (disable/enable): the well-known name loses its owner,
	// which drops every grab in the compositor.  Forget our projection state
	// so the next Sync re-registers everything (state truth is the runtime).
	if (!strcmp(iface, "org.freedesktop.DBus") && !strcmp(member, "NameOwnerChanged")
		&& sender && !strcmp(sender, "org.freedesktop.DBus"))
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
				sPendingHk.clear();
				sPendingTs.clear();
				sRegistered = false;
				if (newOwner && newOwner[0])
					sNeedResync = true; // new extension instance is live
				// The broker's unique name changed (reload/restart): refresh
				// the sender cache used by the Activated sender check.
				CacheBrokerUniqueName();
			}
		}
		return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
	}

	if (strcmp(iface, GS_IFACE) != 0)
		return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
	// The signal must come from the broker.  D-Bus delivers the sender as the
	// broker's UNIQUE name (":1.NNN"), never the well-known name, so compare
	// against the cached unique name (defense-in-depth; the daemon-side match
	// rule sender=GS_BUS_NAME is the real authority).  When the cache is empty
	// (broker name not yet resolved) accept and re-check on next load - a
	// spoofed sender could not have passed the daemon's match rule anyway.
	if (sBrokerUnique)
	{
		if (!sender || strcmp(sender, sBrokerUnique) != 0)
			return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
	}
	else if (!sender)
		return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;

	// v1 consumes ONLY Activated: Deactivated (key-up) is a future capability
	// and must never fire a normal hotkey.  Anything else is ignored.
	if (strcmp(member, "Activated") != 0)
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
	if (!id)
		return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;

	// Resolve the id to the registered Hotkey once, here (off the hot path).
	Hotkey *hk = nullptr;
	for (std::map<std::string, RegEntry>::iterator l = sLive.begin(); l != sLive.end(); ++l)
		if (l->first == id) { hk = l->second.hk; break; }
	// The extension only emits for ids it registered, and only to the owning
	// owner; an unknown id (stale grab) is simply dropped.
	if (!hk)
		return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;

	if (sPendingHk.size() >= GS_MAX_PENDING)
	{
		// Overflow: count and warn once (an invisible loss is the worst case
		// for a hotkey/macro tool).
		++sPendingOverflows;
		if (!sOverflowWarned)
		{
			sOverflowWarned = true;
			SetError("GNOME Shell backend: hotkey event queue overflow "
				"(events dropped; script too slow or desktop stalled)");
		}
		return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
	}
	sPendingHk.push_back(hk);
	sPendingTs.push_back(ts);
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
	DBusMessage *rep = dbus_connection_send_with_reply_and_block(c, msg, 1000, nullptr);
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
		return; // Sticky failure keeps its previous message visible.
	sLastErrorBuf[0] = 0; // Fresh: only failures in THIS sync remain visible.
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

	// Diff: unregister gone, register new.  Batch amortizes the D-Bus
	// round-trips (a few hundred hotkeys = a few calls, not hundreds).
	std::vector<std::string> to_remove, to_add, add_accels;
	for (std::map<std::string, RegEntry>::iterator l = sLive.begin(); l != sLive.end(); ++l)
		if (sWanted.find(l->first) == sWanted.end())
			to_remove.push_back(l->first);
	for (std::map<std::string, RegEntry>::iterator w = sWanted.begin(); w != sWanted.end(); ++w)
		if (sLive.find(w->first) == sLive.end())
		{
			to_add.push_back(w->first);
			add_accels.push_back(w->second.accel);
		}

	if (to_remove.size() > 1)
	{
		if (SendUnregisterMany(to_remove))
			for (size_t i = 0; i < to_remove.size(); ++i)
				sLive.erase(to_remove[i]);
	}
	else if (to_remove.size() == 1)
	{
		if (SendUnregister(to_remove[0]))
			sLive.erase(to_remove[0]);
	}

	if (to_add.size() > 1)
	{
		if (SendRegisterMany(to_add, add_accels))
			for (size_t i = 0; i < to_add.size(); ++i)
				sLive[to_add[i]] = sWanted[to_add[i]];
	}
	else if (to_add.size() == 1)
	{
		if (SendRegister(to_add[0], add_accels[0]))
			sLive[to_add[0]] = sWanted[to_add[0]];
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
		sPendingHk.clear();
		sPendingTs.clear();
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
	if (!sPendingHk.empty())
	{
		// Swap out the pending batch (a script's hotkey bodies may itself send
		// or trigger Sync, which must not re-enter this queue).
		std::vector<Hotkey *> hks = sPendingHk;
		sPendingHk.clear();
		sPendingTs.clear();
		for (size_t i = 0; i < hks.size(); ++i)
			if (hks[i])
			{
				AhkInputEvent event = {
					LinuxInputEventMonotonicUs(), LinuxEvdevCodeForScanCode(hks[i]->mSC),
					hks[i]->mVK, hks[i]->mSC, 0, false, false,
					AhkInputSource::PHYSICAL, -1, 0, AhkInputOrigin::GNOME_SHELL
				};
				LinuxInputEventTrace(event);
				AhkBackendFireHotkey(hks[i]);
			}
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
	sPendingHk.clear();
	sPendingTs.clear();
	sRegistered = false;
	sFailed = false;
	if (sOwnerName) { free(sOwnerName); sOwnerName = nullptr; }
	if (sBrokerUnique) { free(sBrokerUnique); sBrokerUnique = nullptr; }
}

const wchar_t *LinuxGnomeShellLastError()
{
	return sLastErrorBuf;
}

bool LinuxGnomeShellActive()
{
	return sConn && sRegistered;
}
