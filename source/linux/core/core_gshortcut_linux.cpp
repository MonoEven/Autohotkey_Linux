// XDG Global Shortcuts Portal backend (Wayland global hotkeys).
// See core_gshortcut_linux.h for the rationale.
//
// Wire protocol (verified against xdg-desktop-portal 1.20.3 + GNOME 49):
//  CreateSession(a{sv}{session_handle_token, handle_token}) -> o request
//    IMPORTANT: this synchronous reply is the REQUEST object, NOT the
//    session.  The real session handle arrives asynchronously in
//    org.freedesktop.portal.Request::Response (response==0,
//    results["session_handle"] == /.../session/<sender>/<token>); in some
//    versions that value is a D-Bus string, convert s -> object path.
//    Tokens must be dash-free ([A-Za-z0-9_]): xdg-desktop-portal <= 1.20.x
//    builds the object path from them and '-' -> invalid path -> NULL deref
//    (SIGSEGV, upstream PR #1748 / fixed in 1.21).
//  BindShortcuts(o session, a(sa{sv}) {id,{description,triggers}}, s, a{sv})
//    -> o request; the outcome comes via Request::Response (0 = bound,
//    2 = permission pending/dismissed).
//  Signals on the session object: Activated(o, s id, t, a{sv}) etc.
#include "core_gshortcut_linux.h"
#include "input_backend.h"
#include "input_event.h"
#include "../../script.h"
#include "../../globaldata.h"
#include "../../script_func_impl.h"
#include "../../hotkey.h"
#include "../../keyboard_mouse.h"
#include "../../application.h"
#include "core_keymodel_linux.h"
#include <dbus/dbus.h>
#include <cstring>
#include <cstdlib>
#include <map>
#include <string>

namespace
{

DBusConnection *sConn = nullptr;
char *sSessionPath = nullptr;   // Resolved /session/... handle.
char *sRequestPath = nullptr;   // CreateSession request handle (pending session).
bool sBindPending = false;      // A bind was requested before the session resolved.
bool sSessionJustResolved = false; // Set when the session handle materialised (first bind due).
wchar_t sLastErrorBuf[512] = { 0 };
bool sFailed = false;
bool sBound = false;
std::map<std::string, Hotkey *> sShortcutMap;
std::map<std::string, Hotkey *> sWanted; // Desired set awaiting a bind.

#define GS_MAX_PENDING 64
std::string sPending[GS_MAX_PENDING];
int sPendingCount = 0;

void SetError(const char *aText)
{
	mbstowcs(sLastErrorBuf, aText ? aText : "", _countof(sLastErrorBuf) - 1);
	sLastErrorBuf[_countof(sLastErrorBuf) - 1] = 0;
}

bool Applicable()
{
	// Follow the unified input-backend selection: the portal activates only
	// when the router chose it for this session (Wayland without the GNOME
	// Shell extension, or explicit AHK_INPUT_BACKEND=portal /
	// AHK_FORCE_GLOBAL_SHORTCUTS=1).  X11 sessions keep XGrabKey instead.
	return LinuxInputBackendKind() == AhkInputBackendKind::PORTAL;
}

void WideToUtf8(const wchar_t *aIn, char *aOut, size_t aSize)
{
	size_t o = 0;
	for (const wchar_t *p = aIn; *p && o + 4 < aSize; ++p)
	{
		unsigned int cp = (unsigned int)*p;
		if (cp < 0x80) aOut[o++] = (char)cp;
		else if (cp < 0x800) { aOut[o++] = (char)(0xC0 | (cp >> 6)); aOut[o++] = (char)(0x80 | (cp & 0x3F)); }
		else if (cp < 0x10000) { aOut[o++] = (char)(0xE0 | (cp >> 12)); aOut[o++] = (char)(0x80 | ((cp >> 6) & 0x3F)); aOut[o++] = (char)(0x80 | (cp & 0x3F)); }
		else { aOut[o++] = (char)(0xF0 | (cp >> 18)); aOut[o++] = (char)(0x80 | ((cp >> 12) & 0x3F)); aOut[o++] = (char)(0x80 | ((cp >> 6) & 0x3F)); aOut[o++] = (char)(0x80 | (cp & 0x3F)); }
	}
	aOut[o] = '\0';
}

bool SendNoReply(DBusMessage *aMsg)
{
	if (!sConn) return false;
	DBusError err; dbus_error_init(&err);
	DBusMessage *rep = dbus_connection_send_with_reply_and_block(sConn, aMsg, 20000, &err);
	dbus_message_unref(aMsg);
	if (!rep)
	{
		SetError(err.message ? err.message : "D-Bus call failed");
		dbus_error_free(&err);
		return false;
	}
	dbus_error_free(&err);
	dbus_message_unref(rep);
	return true;
}

void AppendTokens(DBusMessageIter *aOut, const char *aK1, const char *aV1,
	const char *aK2, const char *aV2, const char *aK3, const char *aV3)
{
	DBusMessageIter arr, de, var;
	dbus_message_iter_open_container(aOut, DBUS_TYPE_ARRAY, "{sv}", &arr);
	dbus_message_iter_open_container(&arr, DBUS_TYPE_DICT_ENTRY, NULL, &de);
	dbus_message_iter_append_basic(&de, DBUS_TYPE_STRING, &aK1);
	dbus_message_iter_open_container(&de, DBUS_TYPE_VARIANT, "s", &var);
	dbus_message_iter_append_basic(&var, DBUS_TYPE_STRING, &aV1);
	dbus_message_iter_close_container(&de, &var);
	dbus_message_iter_close_container(&arr, &de);
	dbus_message_iter_open_container(&arr, DBUS_TYPE_DICT_ENTRY, NULL, &de);
	dbus_message_iter_append_basic(&de, DBUS_TYPE_STRING, &aK2);
	dbus_message_iter_open_container(&de, DBUS_TYPE_VARIANT, "s", &var);
	dbus_message_iter_append_basic(&var, DBUS_TYPE_STRING, &aV2);
	dbus_message_iter_close_container(&de, &var);
	dbus_message_iter_close_container(&arr, &de);
	dbus_message_iter_open_container(&arr, DBUS_TYPE_DICT_ENTRY, NULL, &de);
	dbus_message_iter_append_basic(&de, DBUS_TYPE_STRING, &aK3);
	dbus_message_iter_open_container(&de, DBUS_TYPE_VARIANT, "s", &var);
	dbus_message_iter_append_basic(&var, DBUS_TYPE_STRING, &aV3);
	dbus_message_iter_close_container(&de, &var);
	dbus_message_iter_close_container(&arr, &de);
	dbus_message_iter_close_container(aOut, &arr);
}

void AppendEmptyDict(DBusMessageIter *aOut)
{
	DBusMessageIter arr;
	dbus_message_iter_open_container(aOut, DBUS_TYPE_ARRAY, "{sv}", &arr);
	dbus_message_iter_close_container(aOut, &arr);
}

void AppendShortcut(DBusMessageIter *aArr, const std::string &aId, const std::string &aDesc, const std::string &aCombo)
{
	DBusMessageIter st, dic, de, var, trig, tst, o;
	dbus_message_iter_open_container(aArr, DBUS_TYPE_STRUCT, NULL, &st);
	dbus_message_iter_append_basic(&st, DBUS_TYPE_STRING, &aId);
	dbus_message_iter_open_container(&st, DBUS_TYPE_ARRAY, "{sv}", &dic);
	const char *kd = "description";
	dbus_message_iter_open_container(&dic, DBUS_TYPE_DICT_ENTRY, NULL, &de);
	dbus_message_iter_append_basic(&de, DBUS_TYPE_STRING, &kd);
	dbus_message_iter_open_container(&de, DBUS_TYPE_VARIANT, "s", &var);
	dbus_message_iter_append_basic(&var, DBUS_TYPE_STRING, &aDesc);
	dbus_message_iter_close_container(&de, &var);
	dbus_message_iter_close_container(&dic, &de);
	const char *kt = "triggers";
	dbus_message_iter_open_container(&dic, DBUS_TYPE_DICT_ENTRY, NULL, &de);
	dbus_message_iter_append_basic(&de, DBUS_TYPE_STRING, &kt);
	dbus_message_iter_open_container(&de, DBUS_TYPE_VARIANT, "a(ssa{sv})", &var);
	dbus_message_iter_open_container(&var, DBUS_TYPE_ARRAY, "(ssa{sv})", &trig);
	dbus_message_iter_open_container(&trig, DBUS_TYPE_STRUCT, NULL, &tst);
	const char *ty = "keyboard";
	dbus_message_iter_append_basic(&tst, DBUS_TYPE_STRING, &ty);
	dbus_message_iter_append_basic(&tst, DBUS_TYPE_STRING, &aCombo);
	dbus_message_iter_open_container(&tst, DBUS_TYPE_ARRAY, "{sv}", &o);
	dbus_message_iter_close_container(&tst, &o);
	dbus_message_iter_close_container(&trig, &tst);
	dbus_message_iter_close_container(&var, &trig);
	dbus_message_iter_close_container(&de, &var);
	dbus_message_iter_close_container(&dic, &de);
	dbus_message_iter_close_container(&st, &dic);
	dbus_message_iter_close_container(aArr, &st);
}

bool HotkeyEnabled(Hotkey *aHk)
{
	return aHk && !g_IsSuspended && (aHk->mType == HK_NORMAL || aHk->mType == HK_KEYBD_HOOK);
}

bool VariantFireable(Hotkey *aHk, HotkeyVariant *aV)
{
	if (!aV || !aV->mEnabled) return false;
	if (g_IsSuspended && !aV->mSuspendExempt) return false;
	if (aV->mHotCriterion && !HotCriterionAllowsFiring(aV->mHotCriterion, aHk->mName)) return false;
	return true;
}

std::string ComboForHotkey(Hotkey *aHk)
{
	std::string combo;
	unsigned int mods = (unsigned int)aHk->mModifiers;
	if (mods & MOD_CONTROL) combo += "<Control>";
	if (mods & MOD_ALT) combo += "<Alt>";
	if (mods & MOD_SHIFT) combo += "<Shift>";
	if (mods & MOD_WIN) combo += "<Super>";
	TCHAR kb[128] = { 0 };
	GetKeyName(aHk->mVK, 0, kb, _countof(kb), _T(""));
	if (kb[0])
	{
		char n[196];
		WideToUtf8(kb, n, sizeof(n));
		combo += n;
	}
	else
	{
		char n[32];
		snprintf(n, sizeof(n), "k%u", (unsigned)aHk->mVK);
		combo += n;
	}
	return combo;
}

void FireShortcut(const std::string &aId)
{
	std::map<std::string, Hotkey *>::iterator it = sShortcutMap.find(aId);
	if (it == sShortcutMap.end()) return;
	Hotkey *hk = it->second;
	if (!HotkeyEnabled(hk)) return;
	HotkeyVariant *vp = nullptr;
	for (HotkeyVariant *v = hk->mFirstVariant; v; v = v->mNextVariant)
		if (VariantFireable(hk, v)) { vp = v; break; }
	if (!vp) return;
	AhkInputEvent event = {
		LinuxInputEventMonotonicUs(), LinuxEvdevCodeForScanCode(hk->mSC),
		hk->mVK, hk->mSC, 0, false, false, AhkInputSource::PHYSICAL, -1, 0,
		AhkInputOrigin::PORTAL
	};
	LinuxInputEventTrace(event);
	++g_nThreads;
	++g;
	InitNewThread(vp->mPriority, false, false);
	hk->PerformInNewThreadMadeByCaller(*vp);
	ResumeUnderlyingThread();
}

// --- D-Bus filter: session/bind responses + Activated ----------------------

void DoBind(); // forward

DBusHandlerResult Handler(DBusConnection *, DBusMessage *aMsg, void *)
{
	const char *iface = dbus_message_get_interface(aMsg);
	const char *member = dbus_message_get_member(aMsg);
	if (!iface || !member) return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;

	if (!strcmp(iface, "org.freedesktop.portal.Request") && !strcmp(member, "Response"))
	{
		const char *path = dbus_message_get_path(aMsg) ? dbus_message_get_path(aMsg) : "";
		DBusMessageIter it;
		dbus_message_iter_init(aMsg, &it);
		int code = -1;
		if (dbus_message_iter_get_arg_type(&it) == DBUS_TYPE_UINT32)
		{
			dbus_uint32_t c = 0;
			dbus_message_iter_get_basic(&it, &c);
			code = (int)c;
		}
		// The CreateSession response materializes the session handle.
		if (sRequestPath && path && !strcmp(path, sRequestPath))
		{
			if (code == 0)
			{
				// results["session_handle"] (string or object path).
				dbus_message_iter_next(&it);
				if (dbus_message_iter_get_arg_type(&it) == DBUS_TYPE_ARRAY)
				{
					DBusMessageIter arr = it, de, var;
					dbus_message_iter_recurse(&arr, &arr);
					while (dbus_message_iter_get_arg_type(&arr) == DBUS_TYPE_DICT_ENTRY)
					{
						de = arr;
						dbus_message_iter_recurse(&arr, &de);
						const char *key = nullptr;
						dbus_message_iter_get_basic(&de, &key);
						dbus_message_iter_next(&de);
						if (key && !strcmp(key, "session_handle") && dbus_message_iter_get_arg_type(&de) == DBUS_TYPE_VARIANT)
						{
							dbus_message_iter_recurse(&de, &var);
							if (dbus_message_iter_get_arg_type(&var) == DBUS_TYPE_STRING
								|| dbus_message_iter_get_arg_type(&var) == DBUS_TYPE_OBJECT_PATH)
							{
								const char *v = nullptr;
								dbus_message_iter_get_basic(&var, &v);
								if (v)
								{
									free(sSessionPath);
									sSessionPath = strdup(v);
								}
							}
							break;
						}
						dbus_message_iter_next(&arr);
					}
				}
				sSessionJustResolved = true;
							// Fall back to the documented constructed path if the results
				// omitted the handle (implementation quirk).
				if (!sSessionPath)
				{
					const char *u = dbus_bus_get_unique_name(sConn);
					char sender[128] = "1";
					if (u) { snprintf(sender, sizeof(sender), "%s", u + 1); for (char *p = sender; *p; ++p) if (*p=='.') *p='_'; }
					char buf[512];
					snprintf(buf, sizeof(buf), "/org/freedesktop/portal/desktop/session/%s/ahk_gs1", sender);
					sSessionPath = strdup(buf);
				}
			}
			free(sRequestPath);
			sRequestPath = nullptr;
		}
		else if (sSessionPath && sBindPending)
		{
			// BindShortcuts outcome: 0 = bound, 1/2 = user denied/dismissed.
			sBindPending = false;
			if (code != 0)
				SetError("GlobalShortcuts: shortcut bind not granted yet (compositor permission pending?)");
			else
			{
				sLastErrorBuf[0] = 0;
				sBound = true;
				sShortcutMap = sWanted;
			}
		}
		return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
	}

	if (strstr(iface, "GlobalShortcuts") && !strcmp(member, "Activated"))
	{
		DBusMessageIter it;
		dbus_message_iter_init(aMsg, &it);
		DBusMessageIter sub = it;
		dbus_message_iter_next(&sub);
		if (dbus_message_iter_get_arg_type(&sub) != DBUS_TYPE_STRING)
			return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
		const char *id = nullptr;
		dbus_message_iter_get_basic(&sub, &id);
		if (id && sPendingCount < GS_MAX_PENDING)
			sPending[sPendingCount++] = id;
	}
	return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
}

// Send CreateSession (async: the session handle comes via Request::Response).
void SendCreateSession()
{
	DBusMessage *msg = dbus_message_new_method_call(
		"org.freedesktop.portal.Desktop", "/org/freedesktop/portal/desktop",
		"org.freedesktop.portal.GlobalShortcuts", "CreateSession");
	if (!msg) return;
	DBusMessageIter it;
	dbus_message_iter_init_append(msg, &it);
	AppendTokens(&it, "session_handle_token", "ahk_gs1",
		"handle_token", "ahk_gh1", "app_id", "org.autohotkey.linux");
	DBusError err; dbus_error_init(&err);
	DBusMessage *rep = dbus_connection_send_with_reply_and_block(sConn, msg, 20000, &err);
	dbus_message_unref(msg);
	if (!rep)
	{
		SetError(err.message ? err.message : "CreateSession failed");
		sFailed = true; // Sticky: the portal is unusable (e.g. the 1.20.x SIGSEGV).
		dbus_error_free(&err);
		return;
	}
	dbus_error_free(&err);
	DBusMessageIter rit;
	dbus_message_iter_init(rep, &rit);
	if (dbus_message_iter_get_arg_type(&rit) != DBUS_TYPE_OBJECT_PATH)
	{
		dbus_message_unref(rep);
		SetError("CreateSession returned no request handle");
		return;
	}
	const char *rq = nullptr;
	dbus_message_iter_get_basic(&rit, &rq);
	if (rq) sRequestPath = strdup(rq);
	dbus_message_unref(rep);
	if (!sRequestPath)
	{
		SetError("CreateSession returned an empty request handle");
		return;
	}
	// Match the Request.Response for this request object.
	char rule[512];
	snprintf(rule, sizeof(rule),
		"type='signal',interface='org.freedesktop.portal.Request',path='%s'", sRequestPath);
	DBusError em; dbus_error_init(&em);
	dbus_bus_add_match(sConn, rule, &em);
	if (dbus_error_is_set(&em))
	{
		SetError(em.message);
		dbus_error_free(&em);
	}
}

void DoBind()
{
	if (!sSessionPath || sWanted.empty()) return;
	DBusMessage *msg = dbus_message_new_method_call(
		"org.freedesktop.portal.Desktop", "/org/freedesktop/portal/desktop",
		"org.freedesktop.portal.GlobalShortcuts", "BindShortcuts");
	if (!msg) return;
	DBusMessageIter it, arr;
	dbus_message_iter_init_append(msg, &it);
	dbus_message_iter_append_basic(&it, DBUS_TYPE_OBJECT_PATH, &sSessionPath);
	dbus_message_iter_open_container(&it, DBUS_TYPE_ARRAY, "(sa{sv})", &arr);
	for (std::map<std::string, Hotkey *>::iterator w = sWanted.begin(); w != sWanted.end(); ++w)
	{
		TCHAR kb[96] = { 0 };
		GetKeyName(w->second->mVK, 0, kb, _countof(kb), _T(""));
		char d[128] = { 0 };
		WideToUtf8(kb, d, sizeof(d));
		std::string desc = d[0] ? std::string(d) : w->first;
		AppendShortcut(&arr, w->first, desc, ComboForHotkey(w->second));
	}
	dbus_message_iter_close_container(&it, &arr);
	const char *parent = "/";
	dbus_message_iter_append_basic(&it, DBUS_TYPE_STRING, &parent);
	{
		// Also carry the app_id in the bind options: some backends (GNOME's
		// GlobalShortcutsProvider) reject binds with an empty app_id.
		DBusMessageIter opts, de, var;
		dbus_message_iter_open_container(&it, DBUS_TYPE_ARRAY, "{sv}", &opts);
		const char *ka = "app_id";
		dbus_message_iter_open_container(&opts, DBUS_TYPE_DICT_ENTRY, NULL, &de);
		dbus_message_iter_append_basic(&de, DBUS_TYPE_STRING, &ka);
		const char *av = "org.autohotkey.linux";
		dbus_message_iter_open_container(&de, DBUS_TYPE_VARIANT, "s", &var);
		dbus_message_iter_append_basic(&var, DBUS_TYPE_STRING, &av);
		dbus_message_iter_close_container(&de, &var);
		dbus_message_iter_close_container(&opts, &de);
		dbus_message_iter_close_container(&it, &opts);
	}
	if (SendNoReply(msg))
		sBindPending = true; // Outcome via Request::Response (0 bound / 2 pending-permission).
}

} // namespace

// --- public API -------------------------------------------------------------

void LinuxGShortcutSync()
{
	if (!Applicable() || sFailed)
		return; // Sticky failure keeps its previous message visible.
	sLastErrorBuf[0] = 0; // Fresh: only failures in THIS sync remain visible.

	// Rebuild the desired set.
	sWanted.clear();
	for (int i = 0; i < Hotkey::sHotkeyCount; ++i)
	{
		Hotkey *hk = Hotkey::shk[i];
		if (!HotkeyEnabled(hk)) continue;
		if (hk->mType != HK_NORMAL && hk->mType != HK_KEYBD_HOOK) continue;
		char id[48];
		snprintf(id, sizeof(id), "ahk_%p", (void *)hk);
		sWanted[id] = hk;
	}

	if (!sConn)
	{
		DBusError err; dbus_error_init(&err);
		sConn = dbus_bus_get(DBUS_BUS_SESSION, &err);
		if (!sConn)
		{
			SetError(err.message ? err.message : "no session bus");
			dbus_error_free(&err);
			return;
		}
		dbus_error_free(&err);
		dbus_connection_set_exit_on_disconnect(sConn, FALSE);
		dbus_connection_add_filter(sConn, Handler, nullptr, nullptr);
	}

	if (!sSessionPath && !sRequestPath)
		SendCreateSession(); // The Response (dispatched later) materializes the session.
	else if (sSessionPath && !sBindPending)
	{
		sSessionJustResolved = true; // let Dispatch issue exactly one bind
	}
}

void LinuxGShortcutDispatch()
{
	static int dbg = 0;
	if (!sConn) return;
	if (dbus_connection_read_write_dispatch(sConn, 0) == FALSE)
	{
		// Portal connection dropped (portal restarted/crashed): reset and retry.
		if (sRequestPath) { free(sRequestPath); sRequestPath = nullptr; }
		if (sSessionPath) { free(sSessionPath); sSessionPath = nullptr; }
		sBound = false;
		sBindPending = false;
		return;
	}
	// The very first bind happens once the session handle materialises.
	// Subsequent re-binds are driven by LinuxGShortcutSync (state changes),
	// never re-issued here (avoids a permission-dialog loop).
	if (sSessionJustResolved && !sBindPending)
	{
		sSessionJustResolved = false;
		DoBind();
	}

	if (sPendingCount)
	{
		std::string ids[GS_MAX_PENDING];
		int n = sPendingCount;
		for (int i = 0; i < n; ++i) ids[i] = sPending[i];
		sPendingCount = 0;
		for (int i = 0; i < n; ++i) FireShortcut(ids[i]);
	}
}

void LinuxGShortcutShutdown()
{
	if (sConn)
	{
		dbus_connection_remove_filter(sConn, Handler, nullptr);
		dbus_connection_close(sConn);
		dbus_connection_unref(sConn);
		sConn = nullptr;
	}
	if (sRequestPath) { free(sRequestPath); sRequestPath = nullptr; }
	if (sSessionPath) { free(sSessionPath); sSessionPath = nullptr; }
	sBound = false;
	sBindPending = false;
	sShortcutMap.clear();
	sWanted.clear();
	sFailed = false;
}

const wchar_t *LinuxGShortcutLastError()
{
	return sLastErrorBuf;
}

bool LinuxGShortcutActive()
{
	return sConn && sSessionPath && sBound;
}
