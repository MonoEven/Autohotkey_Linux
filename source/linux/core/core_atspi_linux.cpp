// AT-SPI accessibility backend (check0820 direction-B item 2).
//
// Minimal libdbus client for the AT-SPI bus.  GTK/Qt/Electron apps with
// accessibility enabled expose org.a11y.atspi.Accessible objects on the
// at-spi bus; this module walks the desktop tree and provides the
// primitives the Wayland Control* fallback needs:
//   - bus address: org.a11y.Bus.GetAddress (on the session bus) returns
//     the at-spi bus address; the registry lives there as
//     org.a11y.atspi.Registry with root /org/a11y/atspi/accessible/root.
//   - Cache.GetItems -> a((so)(so)(so)iiassusau), one bulk call per app;
//     unsupported/old-signature apps fall back to Accessible.GetChildren.
//   - Accessible.Name / ChildCount are D-Bus PROPERTIES
//   - Accessible.GetRoleName() -> s
//   - Text.GetText(0, -1) -> s ; EditableText.SetTextContents(s)
//   - Action.GetNactions/GetName(i)/DoAction(i)
#include "../../stdafx.h"
#include "../../globaldata.h"
#include "../../application.h"
#include "core_atspi_linux.h"
#include <dbus/dbus.h>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <ctime>
#include <string>
#include <vector>
#include <algorithm>
#include <cmath>

namespace {

const char *REG = "org.a11y.atspi.Registry";
const char *IFACE = "org.a11y.atspi.Accessible";
const char *CACHE_IFACE = "org.a11y.atspi.Cache";
const char *CACHE_PATH = "/org/a11y/atspi/cache";

DBusConnection *sBus = nullptr;
bool sTried = false;
int sLastCacheApps = 0;
int sLastFallbackApps = 0;
int sLastCacheItems = 0;
int sLastBudgetMs = 0;
bool sLastBudgetExceeded = false;
uint64_t sQueryDeadlineUs = 0;
unsigned long sPendingCalls = 0;
unsigned long sPendingPumpSlices = 0;
bool sDelayInjectionUsed = false;
int sPendingWaitDepth = 0;

static uint64_t AtspiNowUs();

static void AtspiSetLastError(int aError)
{
	if (g)
		g->LastError = (DWORD)aError;
	SetLastError((DWORD)aError);
}

static int AtspiFailureCode(int aFallback = EIO)
{
	if (sLastBudgetExceeded)
		return ETIMEDOUT;
	if (sQueryDeadlineUs)
	{
		uint64_t now = AtspiNowUs();
		if (now && now >= sQueryDeadlineUs)
			return ETIMEDOUT;
	}
	return aFallback;
}

static bool AtspiFail(int aError)
{
	AtspiSetLastError(aError);
	return false;
}

static bool AtspiSucceed()
{
	AtspiSetLastError(0);
	return true;
}

static int AtspiDbusErrorCode(const DBusError &aError, int aFallback = EIO)
{
	if (!dbus_error_is_set(&aError) || !aError.name)
		return AtspiFailureCode(aFallback);
	if (!strcmp(aError.name, DBUS_ERROR_NO_REPLY)
		|| !strcmp(aError.name, DBUS_ERROR_TIMEOUT))
		return ETIMEDOUT;
	if (!strcmp(aError.name, DBUS_ERROR_DISCONNECTED)
		|| !strcmp(aError.name, DBUS_ERROR_SERVICE_UNKNOWN)
		|| !strcmp(aError.name, DBUS_ERROR_NAME_HAS_NO_OWNER))
		return ENOTCONN;
	if (!strcmp(aError.name, DBUS_ERROR_UNKNOWN_INTERFACE)
		|| !strcmp(aError.name, DBUS_ERROR_UNKNOWN_METHOD)
		|| !strcmp(aError.name, DBUS_ERROR_UNKNOWN_PROPERTY))
		return ENOTSUP;
	return aFallback;
}

static uint64_t AtspiNowUs()
{
	struct timespec ts;
	if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0)
		return 0;
	return (uint64_t)ts.tv_sec * 1000000ULL + (uint64_t)ts.tv_nsec / 1000ULL;
}

static bool AtspiBudgetExpired()
{
	if (!sQueryDeadlineUs)
		return false;
	if (AtspiNowUs() < sQueryDeadlineUs)
		return false;
	sLastBudgetExceeded = true;
	return true;
}

static int AtspiBoundTimeout(int aRequestedMs)
{
	if (!sQueryDeadlineUs)
		return aRequestedMs;
	uint64_t now = AtspiNowUs();
	if (now >= sQueryDeadlineUs)
	{
		sLastBudgetExceeded = true;
		return 0;
	}
	uint64_t remain = (sQueryDeadlineUs - now + 999ULL) / 1000ULL;
	if (remain < (uint64_t)aRequestedMs)
		return (int)remain;
	return aRequestedMs;
}

struct AtspiRef
{
	std::string name;
	std::string path;
	std::string dest; // D-Bus destination hosting this object.
	std::string interfaces; // Comma-separated Cache.GetItems interface names.
	std::string description;
	uint32_t role = 0;
};
std::vector<AtspiRef> sTable;

// ---------------------------------------------------------------------------
// Low-level helpers
// ---------------------------------------------------------------------------

struct AtspiPendingDepthGuard
{
	AtspiPendingDepthGuard() { ++sPendingWaitDepth; }
	~AtspiPendingDepthGuard() { --sPendingWaitDepth; }
};

// Submit a libdbus pending call and wait in bounded slices while pumping the
// existing Linux message loop. Public Control APIs remain synchronous, but a
// slow accessibility peer no longer freezes GTK/Wayland/hotkey/timer dispatch.
// A nested AT-SPI call from an interrupting script thread returns EBUSY rather
// than mutating the shared cache/deadline while this call is pending.
static DBusMessage *AtspiPendingReply(DBusConnection *aBus, DBusMessage *aMessage,
	int aTimeoutMs, DBusError *aError)
{
	if (!aBus || !aMessage || aTimeoutMs <= 0)
	{
		if (aError)
			dbus_set_error(aError, DBUS_ERROR_NO_REPLY, "AT-SPI call budget expired");
		AtspiSetLastError(ETIMEDOUT);
		return nullptr;
	}
	DBusPendingCall *pending = nullptr;
	if (!dbus_connection_send_with_reply(aBus, aMessage, &pending, aTimeoutMs) || !pending)
	{
		if (aError)
			dbus_set_error(aError, DBUS_ERROR_NO_MEMORY, "Unable to allocate AT-SPI pending call");
		AtspiSetLastError(ENOMEM);
		return nullptr;
	}
	++sPendingCalls;
	AtspiPendingDepthGuard pending_depth;
	dbus_connection_flush(aBus);
	uint64_t now_start = AtspiNowUs();
	uint64_t deadline = now_start + (uint64_t)aTimeoutMs * 1000ULL;
	uint64_t injected_not_before = 0;
	if (!sDelayInjectionUsed)
		if (const char *delay = getenv("AHK_ATSPI_TEST_REPLY_DELAY_MS"))
		{
			int delay_ms = atoi(delay);
			if (delay_ms > 0 && delay_ms < aTimeoutMs)
			{
				sDelayInjectionUsed = true;
				injected_not_before = now_start + (uint64_t)delay_ms * 1000ULL;
			}
		}
	for (;;)
	{
		uint64_t now = AtspiNowUs();
		bool completed = dbus_pending_call_get_completed(pending);
		if (completed && (!injected_not_before || now >= injected_not_before))
			break;
		if (now && now >= deadline)
		{
			dbus_pending_call_cancel(pending);
			if (aError)
				dbus_set_error(aError, DBUS_ERROR_NO_REPLY, "AT-SPI pending call timed out");
			AtspiSetLastError(ETIMEDOUT);
			dbus_pending_call_unref(pending);
			return nullptr;
		}
		int remain_ms = now && deadline > now ? (int)((deadline - now + 999ULL) / 1000ULL) : 10;
		int slice_ms = remain_ms > 10 ? 10 : remain_ms;
		if (slice_ms < 1) slice_ms = 1;
		++sPendingPumpSlices;
		if (!completed && !dbus_connection_read_write_dispatch(aBus, slice_ms))
		{
			dbus_pending_call_cancel(pending);
			if (aError)
				dbus_set_error(aError, DBUS_ERROR_DISCONNECTED, "AT-SPI bus disconnected");
			AtspiSetLastError(ENOTCONN);
			dbus_pending_call_unref(pending);
			return nullptr;
		}
		MsgSleep(completed ? 1 : 0, RETURN_AFTER_MESSAGES_SPECIAL_FILTER);
	}
	DBusMessage *reply = dbus_pending_call_steal_reply(pending);
	dbus_pending_call_unref(pending);
	if (reply && dbus_message_get_type(reply) == DBUS_MESSAGE_TYPE_ERROR)
	{
		if (aError)
		{
			dbus_set_error_from_message(aError, reply);
			AtspiSetLastError(AtspiDbusErrorCode(*aError));
		}
		else
			AtspiSetLastError(EIO);
		dbus_message_unref(reply);
		return nullptr;
	}
	return reply;
}

// One-shot pending call on the at-spi bus; caller owns the reply.
DBusMessage *AtspiCall(DBusConnection *bus, const char *aPath, const char *aIface,
	const char *aMethod, int aTimeoutMs = 3000)
{
	if (!bus || !aPath || !*aPath)
		return nullptr;
	aTimeoutMs = AtspiBoundTimeout(aTimeoutMs);
	if (aTimeoutMs <= 0)
		return nullptr;
	DBusMessage *msg = dbus_message_new_method_call(REG, aPath, aIface, aMethod);
	if (!msg)
		return nullptr;
	DBusError err;
	dbus_error_init(&err);
	DBusMessage *rep = AtspiPendingReply(bus, msg, aTimeoutMs, &err);
	dbus_message_unref(msg);
	if (dbus_error_is_set(&err))
	{
		dbus_error_free(&err);
		return nullptr;
	}
	return rep;
}

// Same, but the destination is explicit (app unique names host their
// accessible objects; the registry is only the root).
DBusMessage *AtspiCallTo(const char *aDest, const char *aPath, const char *aIface,
	const char *aMethod, int aTimeoutMs = 3000)
{
	if (!sBus || !aDest || !*aDest || !aPath || !*aPath)
		return nullptr;
	aTimeoutMs = AtspiBoundTimeout(aTimeoutMs);
	if (aTimeoutMs <= 0)
		return nullptr;
	DBusMessage *msg = dbus_message_new_method_call(aDest, aPath, aIface, aMethod);
	if (!msg)
		return nullptr;
	DBusError err;
	dbus_error_init(&err);
	DBusMessage *rep = AtspiPendingReply(sBus, msg, aTimeoutMs, &err);
	dbus_message_unref(msg);
	if (dbus_error_is_set(&err))
	{
		dbus_error_free(&err);
		return nullptr;
	}
	return rep;
}

// Read a (so) reference from an iterator.
bool ReadRef(DBusMessageIter *aIt, std::string &aName, std::string &aPath)
{
	if (dbus_message_iter_get_arg_type(aIt) != DBUS_TYPE_STRUCT)
		return false;
	DBusMessageIter sub;
	dbus_message_iter_recurse(aIt, &sub);
	const char *name = nullptr;
	dbus_message_iter_get_basic(&sub, &name);
	dbus_message_iter_next(&sub);
	const char *path = nullptr;
	if (dbus_message_iter_get_arg_type(&sub) == DBUS_TYPE_OBJECT_PATH)
		dbus_message_iter_get_basic(&sub, &path);
	aName = name ? name : "";
	aPath = path ? path : "";
	return true;
}

// Read the Name D-Bus property of an accessible object (org.freedesktop.
// D-Bus.Properties.Get, 'org.a11y.atspi.Accessible', 'Name').
std::string GetNameProp(const char *aDest, const char *aPath, int aTimeoutMs = 2000)
{
	if (!sBus || !aDest || !*aDest || !aPath || !*aPath)
		return std::string();
	DBusMessage *msg = dbus_message_new_method_call(aDest, aPath,
		"org.freedesktop.DBus.Properties", "Get");
	if (!msg)
		return std::string();
	const char *iface = "org.a11y.atspi.Accessible";
	const char *prop = "Name";
	if (!dbus_message_append_args(msg, DBUS_TYPE_STRING, &iface, DBUS_TYPE_STRING, &prop, DBUS_TYPE_INVALID))
	{
		dbus_message_unref(msg);
		return std::string();
	}
	DBusError err;
	dbus_error_init(&err);
	int timeout = AtspiBoundTimeout(aTimeoutMs);
	if (timeout <= 0)
	{
		dbus_message_unref(msg);
		dbus_error_free(&err);
		return std::string();
	}
	DBusMessage *rep = AtspiPendingReply(sBus, msg, timeout, &err);
	dbus_message_unref(msg);
	std::string out;
	if (rep)
	{
		DBusMessageIter it, sub;
		if (dbus_message_iter_init(rep, &it) && dbus_message_iter_get_arg_type(&it) == DBUS_TYPE_VARIANT)
		{
			dbus_message_iter_recurse(&it, &sub);
			const char *s = nullptr;
			if (dbus_message_iter_get_arg_type(&sub) == DBUS_TYPE_STRING)
			{
				dbus_message_iter_get_basic(&sub, &s);
				out = s ? s : "";
			}
		}
		dbus_message_unref(rep);
	}
	dbus_error_free(&err);
	return out;
}

// Parse one item of the preferred Cache.GetItems signature:
// ((so)(so)(so)iiassusau).  Capture enough metadata for honest GUI-host
// matrices without extra per-node round trips.
static bool ReadCacheItemNew(DBusMessageIter *aItem, const char *aFallbackDest, AtspiRef &aOut)
{
	if (dbus_message_iter_get_arg_type(aItem) != DBUS_TYPE_STRUCT)
		return false;
	DBusMessageIter fields;
	dbus_message_iter_recurse(aItem, &fields);
	std::string obj_dest, obj_path;
	if (!ReadRef(&fields, obj_dest, obj_path) || obj_path.empty())
		return false;
	// main -> application -> parent -> index -> child count -> interfaces.
	for (int i = 0; i < 5; ++i)
		if (!dbus_message_iter_next(&fields))
			return false;
	if (dbus_message_iter_get_arg_type(&fields) != DBUS_TYPE_ARRAY)
		return false; // Old Qt signature reaches a different type here.
	DBusMessageIter interfaces;
	dbus_message_iter_recurse(&fields, &interfaces);
	while (dbus_message_iter_get_arg_type(&interfaces) == DBUS_TYPE_STRING)
	{
		const char *iface = nullptr;
		dbus_message_iter_get_basic(&interfaces, &iface);
		if (iface && *iface)
		{
			if (!aOut.interfaces.empty())
				aOut.interfaces += ',';
			aOut.interfaces += iface;
		}
		dbus_message_iter_next(&interfaces);
	}
	if (!dbus_message_iter_next(&fields)
		|| dbus_message_iter_get_arg_type(&fields) != DBUS_TYPE_STRING)
		return false;
	const char *name = nullptr;
	dbus_message_iter_get_basic(&fields, &name);
	aOut.name = name ? name : "";
	if (!dbus_message_iter_next(&fields)
		|| dbus_message_iter_get_arg_type(&fields) != DBUS_TYPE_UINT32)
		return false;
	dbus_message_iter_get_basic(&fields, &aOut.role);
	if (!dbus_message_iter_next(&fields)
		|| dbus_message_iter_get_arg_type(&fields) != DBUS_TYPE_STRING)
		return false;
	const char *description = nullptr;
	dbus_message_iter_get_basic(&fields, &description);
	aOut.description = description ? description : "";
	aOut.path = obj_path;
	aOut.dest = obj_dest.empty() ? (aFallbackDest ? aFallbackDest : "") : obj_dest;
	return !aOut.dest.empty();
}

static bool ParseCacheItems(DBusMessage *aReply, const char *aDest,
	std::vector<AtspiRef> &aItems)
{
	aItems.clear();
	if (!aReply)
		return false;
	DBusMessageIter it;
	if (!dbus_message_iter_init(aReply, &it)
		|| dbus_message_iter_get_arg_type(&it) != DBUS_TYPE_ARRAY)
		return false;
	DBusMessageIter arr;
	dbus_message_iter_recurse(&it, &arr);
	while (dbus_message_iter_get_arg_type(&arr) != DBUS_TYPE_INVALID)
	{
		AtspiRef item;
		if (!ReadCacheItemNew(&arr, aDest, item))
		{
			aItems.clear();
			return false;
		}
		aItems.push_back(std::move(item));
		dbus_message_iter_next(&arr);
	}
	return !aItems.empty();
}

static void AppendCacheItems(std::vector<AtspiRef> &aItems)
{
	sLastCacheApps++;
	sLastCacheItems += (int)aItems.size();
	for (auto &item : aItems)
		sTable.push_back(std::move(item));
}

// One bulk round-trip per application.  Unsupported/old signatures fall back
// to WalkChildren for that application only, preserving Qt compatibility.
static bool LoadCacheForApp(const char *aDest)
{
	// Test/diagnostic override: prove the per-application GetChildren fallback
	// without requiring an old-signature Qt process.
	if (getenv("AHK_ATSPI_DISABLE_CACHE"))
		return false;
	DBusMessage *rep = AtspiCallTo(aDest, CACHE_PATH, CACHE_IFACE, "GetItems", 500);
	if (!rep)
		return false;
	std::vector<AtspiRef> items;
	bool valid = ParseCacheItems(rep, aDest, items);
	dbus_message_unref(rep);
	if (!valid)
		return false;
	AppendCacheItems(items);
	return true;
}

void WalkChildren(const char *aDest, const char *aPath, int aDepth, int aMaxDepth);

static bool AtspiWindowLikeRole(uint32_t aRole)
{
	// AT-SPI roles: Alert=2, DesktopFrame=14, Dialog=16, FileChooser=19,
	// Frame=23, InternalFrame=28. Role 0 means fallback metadata is unavailable.
	return aRole == 0 || aRole == 2 || aRole == 14 || aRole == 16
		|| aRole == 19 || aRole == 23 || aRole == 28;
}

static std::string AtspiComparableIdentity(const std::string &aText)
{
	std::string normalized;
	for (unsigned char ch : aText)
		if ((ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9'))
			normalized += (char)ch;
		else if (ch >= 'A' && ch <= 'Z')
			normalized += (char)(ch - 'A' + 'a');
	return normalized;
}

static bool AtspiTitleRelated(const std::string &aIdentity, const std::string &aTitle)
{
	std::string identity = AtspiComparableIdentity(aIdentity);
	std::string title = AtspiComparableIdentity(aTitle);
	if (identity.size() < 3 || title.size() < 3)
		return false;
	bool title_ends_with_identity = title.size() >= identity.size()
		&& title.compare(title.size() - identity.size(), identity.size(), identity) == 0;
	bool partial_title_starts_identity = identity.size() >= title.size()
		&& identity.compare(0, title.size(), title) == 0;
	return title_ends_with_identity || partial_title_starts_identity;
}

// Read the desktop's application roots once, then load each application's
// cache in bulk.  The registry itself has no per-app cache.
static bool RefreshApplications(int aMaxDepth, const char *aWindowTitle)
{
	DBusMessage *rep = AtspiCall(sBus, "/org/a11y/atspi/accessible/root", IFACE, "GetChildren");
	if (!rep)
		return false;
	DBusMessageIter it;
	if (!dbus_message_iter_init(rep, &it)
		|| dbus_message_iter_get_arg_type(&it) != DBUS_TYPE_ARRAY)
	{
		dbus_message_unref(rep);
		return false;
	}
	auto load_app = [&](const std::string &owner, const std::string &path,
		const std::string &display) {
		sTable.push_back(AtspiRef{display, path, owner});
		if (!LoadCacheForApp(owner.c_str()))
		{
			if (AtspiBudgetExpired())
				return;
			++sLastFallbackApps;
			WalkChildren(owner.c_str(), path.c_str(), 1, aMaxDepth);
		}
	};
	std::string hint = aWindowTitle ? aWindowTitle : "";
	DBusMessageIter arr;
	dbus_message_iter_recurse(&it, &arr);
	if (hint.empty())
	{
		// Preserve the normal diagnostic/full-refresh order exactly.
		while (dbus_message_iter_get_arg_type(&arr) != DBUS_TYPE_INVALID)
		{
			if (AtspiBudgetExpired())
				break;
			std::string owner, path;
			if (ReadRef(&arr, owner, path) && !owner.empty() && !path.empty())
			{
				std::string display = owner;
				if (display[0] == ':')
				{
					std::string name = GetNameProp(owner.c_str(), path.c_str());
					if (!name.empty())
						display = name;
				}
				load_app(owner, path, display);
			}
			dbus_message_iter_next(&arr);
		}
		dbus_message_unref(rep);
		return true;
	}

	struct AppRoot
	{
		std::string owner;
		std::string path;
		std::string display;
		bool preferred;
	};
	std::vector<AppRoot> apps;
	while (dbus_message_iter_get_arg_type(&arr) != DBUS_TYPE_INVALID)
	{
		std::string owner, path;
		if (ReadRef(&arr, owner, path) && !owner.empty() && !path.empty())
			apps.push_back(AppRoot{owner, path, owner, false});
		dbus_message_iter_next(&arr);
	}
	dbus_message_unref(rep);
	// Probe every root name concurrently under one 500ms wall-clock budget.
	// Serial per-app timeouts let one stale desktop process starve the target.
	struct RootPending { size_t index; DBusPendingCall *call; };
	std::vector<RootPending> pending;
	int root_timeout = AtspiBoundTimeout(500);
	if (root_timeout > 0)
		for (size_t index = 0; index < apps.size(); ++index)
		{
			DBusMessage *message = dbus_message_new_method_call(apps[index].owner.c_str(),
				apps[index].path.c_str(), "org.freedesktop.DBus.Properties", "Get");
			if (!message)
				continue;
			const char *iface = "org.a11y.atspi.Accessible";
			const char *property = "Name";
			dbus_message_append_args(message, DBUS_TYPE_STRING, &iface,
				DBUS_TYPE_STRING, &property, DBUS_TYPE_INVALID);
			DBusPendingCall *call = nullptr;
			if (dbus_connection_send_with_reply(sBus, message, &call, root_timeout) && call)
			{
				pending.push_back(RootPending{index, call});
				++sPendingCalls;
			}
			dbus_message_unref(message);
		}
	if (!pending.empty())
	{
		dbus_connection_flush(sBus);
		uint64_t deadline = AtspiNowUs() + (uint64_t)root_timeout * 1000ULL;
		if (sQueryDeadlineUs && sQueryDeadlineUs < deadline)
			deadline = sQueryDeadlineUs;
		bool preferred_found = false;
		AtspiPendingDepthGuard pending_depth;
		for (;;)
		{
			bool any = false;
			for (auto &entry : pending)
			{
				if (!entry.call)
					continue;
				any = true;
				if (!dbus_pending_call_get_completed(entry.call))
					continue;
				DBusMessage *reply = dbus_pending_call_steal_reply(entry.call);
				dbus_pending_call_unref(entry.call);
				entry.call = nullptr;
				std::string name;
				if (reply && dbus_message_get_type(reply) == DBUS_MESSAGE_TYPE_METHOD_RETURN)
				{
					DBusMessageIter value, text;
					if (dbus_message_iter_init(reply, &value)
						&& dbus_message_iter_get_arg_type(&value) == DBUS_TYPE_VARIANT)
					{
						dbus_message_iter_recurse(&value, &text);
						if (dbus_message_iter_get_arg_type(&text) == DBUS_TYPE_STRING)
						{
							const char *raw = nullptr;
							dbus_message_iter_get_basic(&text, &raw);
							name = raw ? raw : "";
						}
					}
				}
				if (reply)
					dbus_message_unref(reply);
				AppRoot &app = apps[entry.index];
				if (!name.empty())
					app.display = name;
				app.preferred = AtspiTitleRelated(app.display, hint);
				if (app.preferred)
				{
					preferred_found = true;
					break;
				}
			}
			if (preferred_found || !any)
				break;
			uint64_t now = AtspiNowUs();
			if (now && now >= deadline)
				break;
			++sPendingPumpSlices;
			if (!dbus_connection_read_write_dispatch(sBus, 10))
				break;
			MsgSleep(0, RETURN_AFTER_MESSAGES_SPECIAL_FILTER);
		}
		for (auto &entry : pending)
			if (entry.call)
			{
				dbus_pending_call_cancel(entry.call);
				dbus_pending_call_unref(entry.call);
			}
	}
	bool preferred_known = std::any_of(apps.begin(), apps.end(),
		[](const AppRoot &app) { return app.preferred; });
	if (!preferred_known && !getenv("AHK_ATSPI_DISABLE_CACHE"))
	{
		struct CachePending { size_t index; DBusPendingCall *call; };
		std::vector<CachePending> cache_pending;
		int cache_timeout = AtspiBoundTimeout(1000);
		if (cache_timeout > 0)
			for (size_t index = 0; index < apps.size(); ++index)
			{
				DBusMessage *message = dbus_message_new_method_call(apps[index].owner.c_str(),
					CACHE_PATH, CACHE_IFACE, "GetItems");
				if (!message)
					continue;
				DBusPendingCall *call = nullptr;
				if (dbus_connection_send_with_reply(sBus, message, &call, cache_timeout) && call)
				{
					cache_pending.push_back(CachePending{index, call});
					++sPendingCalls;
				}
				dbus_message_unref(message);
			}
		if (!cache_pending.empty())
		{
			dbus_connection_flush(sBus);
			uint64_t deadline = AtspiNowUs() + (uint64_t)cache_timeout * 1000ULL;
			if (sQueryDeadlineUs && sQueryDeadlineUs < deadline)
				deadline = sQueryDeadlineUs;
			AtspiPendingDepthGuard pending_depth;
			bool target_loaded = false;
			for (;;)
			{
				bool any = false;
				for (auto &entry : cache_pending)
				{
					if (!entry.call)
						continue;
					any = true;
					if (!dbus_pending_call_get_completed(entry.call))
						continue;
					DBusMessage *reply = dbus_pending_call_steal_reply(entry.call);
					dbus_pending_call_unref(entry.call);
					entry.call = nullptr;
					std::vector<AtspiRef> items;
					bool valid = reply && dbus_message_get_type(reply) == DBUS_MESSAGE_TYPE_METHOD_RETURN
						&& ParseCacheItems(reply, apps[entry.index].owner.c_str(), items);
					if (reply)
						dbus_message_unref(reply);
					if (!valid)
						continue;
					std::string normalized_hint = AtspiComparableIdentity(hint);
					bool match = std::any_of(items.begin(), items.end(), [&](const AtspiRef &item) {
						std::string normalized_name = AtspiComparableIdentity(item.name);
						bool raw_match = !hint.empty() && item.name.find(hint) != std::string::npos;
						bool normalized_match = normalized_hint.size() >= 3
							&& normalized_name.find(normalized_hint) != std::string::npos;
						return AtspiWindowLikeRole(item.role) && (raw_match || normalized_match);
					});
					if (match)
					{
						AppRoot &app = apps[entry.index];
						app.preferred = true;
						sTable.push_back(AtspiRef{app.display, app.path, app.owner});
						AppendCacheItems(items);
						target_loaded = true;
						break;
					}
				}
				if (target_loaded || !any)
					break;
				uint64_t now = AtspiNowUs();
				if (now && now >= deadline)
					break;
				++sPendingPumpSlices;
				if (!dbus_connection_read_write_dispatch(sBus, 10))
					break;
				MsgSleep(0, RETURN_AFTER_MESSAGES_SPECIAL_FILTER);
			}
			for (auto &entry : cache_pending)
				if (entry.call)
				{
					dbus_pending_call_cancel(entry.call);
					dbus_pending_call_unref(entry.call);
				}
			if (target_loaded)
				return true;
		}
	}
	std::stable_sort(apps.begin(), apps.end(), [](const AppRoot &left, const AppRoot &right) {
		return left.preferred && !right.preferred;
	});
	for (const auto &app : apps)
	{
		if (AtspiBudgetExpired())
			break;
		load_app(app.owner, app.path, app.display);
		if (app.preferred)
			break; // WinTitle scopes the operation to this application only.
	}
	return true;
}

// Recursive BFS over the accessible tree (bounded depth + node count).
// AT-SPI model: root's children are [unique-bus-name, /org/a11y/atspi/
// accessible/root] placeholders; the NAME is the D-Bus destination that
// hosts the app's objects (the path is shared with the registry).
void WalkChildren(const char *aDest, const char *aPath, int aDepth, int aMaxDepth)
{
	if (AtspiBudgetExpired())
		return;
	if (aDepth > aMaxDepth || aDepth > 32)
		return;
	if (sTable.size() > 8192)
		return;
	DBusMessage *rep = AtspiCallTo(aDest, aPath, IFACE, "GetChildren");
	if (!rep)
		return;
	DBusMessageIter it;
	dbus_message_iter_init(rep, &it);
	if (dbus_message_iter_get_arg_type(&it) == DBUS_TYPE_ARRAY)
	{
		DBusMessageIter arr;
		dbus_message_iter_recurse(&it, &arr);
		while (dbus_message_iter_get_arg_type(&arr) != DBUS_TYPE_INVALID)
		{
			if (AtspiBudgetExpired())
				break;
			std::string name, path;
			if (ReadRef(&arr, name, path) && !path.empty())
			{
				const char *dest = aDest;
				if (path.find("/org/a11y/atspi/accessible/root") == 0)
					dest = name.c_str(); // The placeholder carries the owner.
				// The reference name is the D-Bus destination for the root
				// placeholders; the accessible NAME is a property.  Fetch it
				// for depth-limited nodes (cheap: one round trip each).
				std::string disp = name;
				if (disp.empty() || disp[0] == ':')
				{
					std::string n = GetNameProp(dest, path.c_str());
					if (!n.empty())
						disp = n;
				}
				sTable.push_back(AtspiRef{disp, path, dest});
				WalkChildren(dest, path.c_str(), aDepth + 1, aMaxDepth);
			}
			dbus_message_iter_next(&arr);
		}
	}
	dbus_message_unref(rep);
}

// ---------------------------------------------------------------------------
// Bootstrap
// ---------------------------------------------------------------------------

bool InitOnce()
{
	if (sTried)
		return sBus != nullptr;
	sTried = true;
	const char *diag = getenv("AHK_ATSPI_DUMP"); // reuse: also prints init steps.
	auto dbg = [diag](const char *s) {
		if (diag && *diag)
		{
			FILE *f = fopen("/tmp/atspi_init.log", "a");
			if (f) { fprintf(f, "%s\n", s); fclose(f); }
		}
	};
	// Resolve the at-spi bus address through the session bus.
	DBusConnection *sess = dbus_bus_get(DBUS_BUS_SESSION, nullptr);
	if (!sess) { dbg("no session bus"); return false; }
	dbg("session bus ok");
	DBusError err;
	dbus_error_init(&err);
	DBusMessage *m = dbus_message_new_method_call("org.a11y.Bus", "/org/a11y/bus",
		"org.a11y.Bus", "GetAddress");
	DBusMessage *rep = m ? AtspiPendingReply(sess, m, 2000, &err) : nullptr;
	if (m)
		dbus_message_unref(m);
	std::string addr;
	if (rep)
	{
		const char *s = nullptr;
		if (dbus_message_get_args(rep, nullptr, DBUS_TYPE_STRING, &s, DBUS_TYPE_INVALID))
			addr = s ? s : "";
		dbus_message_unref(rep);
	}
	dbus_error_free(&err);
	if (addr.empty()) { dbg("GetAddress empty"); return false; }
	std::string log = "at-spi addr=" + addr;
	dbg(log.c_str());
	sBus = dbus_connection_open_private(addr.c_str(), &err);
	if (!sBus) { std::string e2 = "open failed: " + std::string(err.message ? err.message : "?"); dbg(e2.c_str()); dbus_error_free(&err); return false; }
	dbus_connection_set_exit_on_disconnect(sBus, FALSE);
	// CRITICAL: a libdbus private connection is not a registered peer until
	// dbus_bus_register() (which allocates the unique name).  Without it the
	// at-spi bus never routes method calls back (probe times out with "Did
	// not receive a reply").
	if (!dbus_bus_register(sBus, &err))
	{
		std::string e3 = "register failed: " + std::string(err.message ? err.message : "?");
		dbg(e3.c_str());
		dbus_error_free(&err);
		dbus_connection_close(sBus);
		sBus = nullptr;
		return false;
	}
	dbg("at-spi bus open");
	// Verify the registry answers.
	DBusMessage *probe = AtspiCall(sBus, "/org/a11y/atspi/accessible/root", IFACE, "GetRoleName");
	if (!probe) { dbg("probe GetRoleName failed"); dbus_connection_close(sBus); sBus = nullptr; return false; }
	dbus_message_unref(probe);
	dbg("probe ok");
	// Also verify root children are visible (>=1 placeholder).
	DBusMessage *ch = AtspiCall(sBus, "/org/a11y/atspi/accessible/root", IFACE, "GetChildren");
	if (ch)
	{
		DBusMessageIter it;
		dbus_message_iter_init(ch, &it);
		int n = 0;
		if (dbus_message_iter_get_arg_type(&it) == DBUS_TYPE_ARRAY)
		{
			DBusMessageIter arr;
			dbus_message_iter_recurse(&it, &arr);
			while (dbus_message_iter_get_arg_type(&arr) != DBUS_TYPE_INVALID) { ++n; dbus_message_iter_next(&arr); }
		}
		char b[64]; snprintf(b, sizeof(b), "root children=%d", n);
		dbg(b);
		dbus_message_unref(ch);
	}
	return true;
}

} // namespace

bool LinuxAtspiAvailable()
{
	if (sPendingWaitDepth > 0)
		return AtspiFail(EBUSY);
	bool available = InitOnce() && sBus != nullptr;
	AtspiSetLastError(available ? 0 : ENOTCONN);
	return available;
}

int LinuxAtspiRefresh(const char *aWindowTitle)
{
	AtspiSetLastError(0);
	sTable.clear();
	sLastCacheApps = 0;
	sLastFallbackApps = 0;
	sLastCacheItems = 0;
	sLastBudgetExceeded = false;
	sPendingCalls = 0;
	sPendingPumpSlices = 0;
	if (!LinuxAtspiAvailable())
		return 0;
	int budget_ms = 2000;
	if (const char *budget = getenv("AHK_ATSPI_TOTAL_BUDGET_MS"))
		budget_ms = atoi(budget);
	if (budget_ms < 1)
		budget_ms = 1;
	sLastBudgetMs = budget_ms;
	sQueryDeadlineUs = AtspiNowUs() + (uint64_t)budget_ms * 1000ULL;
	int max = 6; // Fallback depth reaches app windows and common controls.
	if (const char *extra = getenv("AHK_ATSPI_MAXDEPTH"))
		max = atoi(extra);
	if (max < 1)
		max = 1;
	if (!RefreshApplications(max, aWindowTitle))
		WalkChildren(REG, "/org/a11y/atspi/accessible/root", 1, max);
	AtspiBudgetExpired();
	sQueryDeadlineUs = 0; // Later GetText/Action calls have their own timeout.
	// Diagnostics: AHK_ATSPI_DUMP=<path> writes cache/fallback counts and the
	// flat table (name<TAB>path).  This is also the external bulk-cache oracle.
	if (const char *dump = getenv("AHK_ATSPI_DUMP"))
	{
		FILE *f = fopen(dump, "w");
		if (f)
		{
			fprintf(f, "# cache_apps=%d fallback_apps=%d cache_items=%d nodes=%zu budget_ms=%d budget_exceeded=%d pending_calls=%lu pump_slices=%lu\n",
				sLastCacheApps, sLastFallbackApps, sLastCacheItems, sTable.size(),
				sLastBudgetMs, sLastBudgetExceeded ? 1 : 0,
				sPendingCalls, sPendingPumpSlices);
			for (const auto &e : sTable)
				fprintf(f, "%s\t%s\tdest=%s\trole=%u\tinterfaces=%s\tdescription=%s\n",
					e.name.c_str(), e.path.c_str(), e.dest.c_str(), e.role,
					e.interfaces.c_str(), e.description.c_str());
			fclose(f);
		}
	}
	AtspiSetLastError(sLastBudgetExceeded ? ETIMEDOUT : 0);
	return (int)sTable.size();
}

void LinuxAtspiDump(std::string &aDebug)
{
	for (const auto &e : sTable)
	{
		aDebug += e.name;
		aDebug += "\t";
		aDebug += e.path;
		aDebug += "\n";
	}
}

// Application scope of a node: its D-Bus destination.  AT-SPI object paths
// are flat ids, so application identity is the destination (every object of
// one app lives under its unique bus name).  The root placeholder itself
// (/accessible/root) is the app entry point, not a descendant window.
static bool AtspiAppScope(const AtspiRef &aNode, std::string &aDest)
{
	static const char kApp[] = "/org/a11y/atspi/accessible/";
	size_t base = strlen(kApp);
	if (aNode.path.compare(0, base, kApp) != 0)
		return false;
	if (aNode.path == "/org/a11y/atspi/accessible/root")
		return false; // the application root placeholder, not a window
	if (!AtspiWindowLikeRole(aNode.role))
		return false; // menu/action labels are not WinTitle candidates.
	aDest = aNode.dest;
	return true;
}

// Resolve aWinTitle to the application (D-Bus destination) that owns a window
// with that accessible name, so Control* searches stay inside that app.
static bool AtspiScopeForWindowTitle(const char *aWinTitle, std::string &aDest)
{
	if (!aWinTitle || !*aWinTitle)
		return false;
	std::string title(aWinTitle);
	std::string dest;
	bool found = false;
	for (const auto &e : sTable)
		if (e.name == title && AtspiAppScope(e, dest))
		{
			found = true;
			break;
		}
	if (!found)
		for (const auto &e : sTable)
			if (e.name.find(title) != std::string::npos && AtspiAppScope(e, dest))
			{
				found = true;
				break;
			}
	if (!found)
		return false;
	aDest = dest;
	return true;
}

// Diagnostics (AHK_ATSPI_DUMP): log the cached table entries whose name
// matches aWindowTitle or aName, so subtree-scope issues are visible.
static void AtspiDumpMatches(const char *aWinTitle, const char *aName)
{
	const char *diag = getenv("AHK_ATSPI_DUMP");
	if (!diag || !*diag)
		return;
	FILE *f = fopen("/tmp/atspi_table.log", "a");
	if (!f)
		return;
	fprintf(f, "query title=[%s] name=[%s]\n",
		aWinTitle ? aWinTitle : "<null>", aName ? aName : "<null>");
	for (const auto &e : sTable)
		if ((aWinTitle && e.name.find(aWinTitle) != std::string::npos)
			|| (aName && e.name.find(aName) != std::string::npos))
			fprintf(f, "name=%s dest=%s path=%s\n", e.name.c_str(), e.dest.c_str(), e.path.c_str());
	fclose(f);
}

// Application-root placeholder nodes (path ends in /accessible/root) are the
// D-Bus entry points, not operable controls; their Name property can collide
// with a control's name, so never match them for Control*.
static bool AtspiIsAppRoot(const AtspiRef &e)
{
	static const char kApp[] = "/org/a11y/atspi/accessible/root";
	return e.path == kApp;
}

bool LinuxAtspiFindByName(const char *aName, std::string &aOutPath, const char *aWindowTitle)
{
	if (!aName || !*aName)
		return AtspiFail(EINVAL);
	aOutPath.clear();
	std::string n(aName);
	std::string scope_dest;
	AtspiDumpMatches(aWindowTitle, aName);
	if (aWindowTitle && *aWindowTitle)
		if (!AtspiScopeForWindowTitle(aWindowTitle, scope_dest))
			return AtspiFail(sLastBudgetExceeded ? ETIMEDOUT : ENOENT); // named window absent
	auto in_scope = [&](const AtspiRef &e) {
		return scope_dest.empty() || e.dest == scope_dest;
	};
	for (const auto &e : sTable)
		if (!AtspiIsAppRoot(e) && in_scope(e) && e.name == n) { aOutPath = e.dest + "|" + e.path; return AtspiSucceed(); }
	for (const auto &e : sTable)
		if (!AtspiIsAppRoot(e) && in_scope(e) && e.name.find(n) != std::string::npos) { aOutPath = e.dest + "|" + e.path; return AtspiSucceed(); }
	return AtspiFail(sLastBudgetExceeded ? ETIMEDOUT : ENOENT);
}

// Split a "dest|path" handle produced by LinuxAtspiFindByName.
static void SplitHandle(const std::string &aHandle, std::string &aDest, std::string &aPath)
{
	size_t p = aHandle.find('|');
	if (p == std::string::npos)
	{
		aDest = REG;
		aPath = aHandle;
	}
	else
	{
		aDest = aHandle.substr(0, p);
		aPath = aHandle.substr(p + 1);
	}
}

static const AtspiRef *AtspiFindRef(const std::string &aDest, const std::string &aPath);
static bool AtspiKnownWithoutInterface(const std::string &aDest, const std::string &aPath,
	const char *aInterface);
static bool AtspiGetProperty(const std::string &aDest, const std::string &aPath,
	const char *aIface, const char *aProperty, DBusMessage **aReply);

bool LinuxAtspiGetText(const char *aPath, std::string &aText)
{
	if (!aPath || !*aPath)
		return AtspiFail(EINVAL);
	if (!LinuxAtspiAvailable())
		return false;
	std::string dest, path;
	SplitHandle(aPath, dest, path);
	if (AtspiKnownWithoutInterface(dest, path, "org.a11y.atspi.Text"))
		return AtspiFail(ENOTSUP);
	// Text.GetText(start, end) with start=0, end=-1 (0x7fffffff).
	DBusMessage *msg = dbus_message_new_method_call(dest.c_str(), path.c_str(), "org.a11y.atspi.Text", "GetText");
	if (!msg)
		return false;
	int start = 0, end = 0x7fffffff;
	if (!dbus_message_append_args(msg, DBUS_TYPE_INT32, &start, DBUS_TYPE_INT32, &end, DBUS_TYPE_INVALID))
	{
		dbus_message_unref(msg);
		return false;
	}
	DBusError err;
	dbus_error_init(&err);
	DBusMessage *rep = AtspiPendingReply(sBus, msg, 3000, &err);
	dbus_message_unref(msg);
	if (!rep)
	{
		int code = AtspiDbusErrorCode(err);
		dbus_error_free(&err);
		return AtspiFail(code);
	}
	const char *s = nullptr;
	bool ok = false;
	if (dbus_message_get_args(rep, nullptr, DBUS_TYPE_STRING, &s, DBUS_TYPE_INVALID))
	{
		aText = s ? s : "";
		ok = true;
	}
	dbus_message_unref(rep);
	dbus_error_free(&err);
	AtspiSetLastError(ok ? 0 : EPROTO);
	return ok;
}

bool LinuxAtspiSetText(const char *aPath, const char *aText)
{
	if (!aPath || !*aPath || !aText)
		return AtspiFail(EINVAL);
	if (!LinuxAtspiAvailable())
		return false;
	std::string dest, path;
	SplitHandle(aPath, dest, path);
	if (AtspiKnownWithoutInterface(dest, path, "org.a11y.atspi.EditableText"))
		return AtspiFail(ENOTSUP);
	DBusMessage *msg = dbus_message_new_method_call(dest.c_str(), path.c_str(), "org.a11y.atspi.EditableText", "SetTextContents");
	if (!msg)
		return false;
	if (!dbus_message_append_args(msg, DBUS_TYPE_STRING, &aText, DBUS_TYPE_INVALID))
	{
		dbus_message_unref(msg);
		return false;
	}
	DBusError err;
	dbus_error_init(&err);
	DBusMessage *rep = AtspiPendingReply(sBus, msg, 3000, &err);
	dbus_message_unref(msg);
	bool ok = rep && dbus_message_get_type(rep) == DBUS_MESSAGE_TYPE_METHOD_RETURN;
	if (rep)
	{
		if (ok)
		{
			dbus_bool_t result = TRUE; // Some toolkit versions return void, others bool.
			if (dbus_message_has_signature(rep, "b"))
				dbus_message_get_args(rep, nullptr, DBUS_TYPE_BOOLEAN, &result, DBUS_TYPE_INVALID);
			ok = result != FALSE;
		}
		dbus_message_unref(rep);
	}
	int code = ok ? 0 : AtspiDbusErrorCode(err, EIO);
	dbus_error_free(&err);
	AtspiSetLastError(code);
	return ok;
}

static bool AtspiGetProperty(const std::string &aDest, const std::string &aPath,
	const char *aIface, const char *aProperty, DBusMessage **aReply);

bool LinuxAtspiDoAction(const char *aPath, int aIndex, const char *aNameOrNull)
{
	if (!aPath || !*aPath)
		return AtspiFail(EINVAL);
	if (!LinuxAtspiAvailable())
		return false;
	std::string dest, path;
	SplitHandle(aPath, dest, path);
	int idx = aIndex;
	if (aNameOrNull && *aNameOrNull)
	{
		DBusMessage *rep = AtspiCallTo(dest.c_str(), path.c_str(), "org.a11y.atspi.Action", "GetNactions");
		int n = 0;
		if (rep)
		{
			dbus_message_get_args(rep, nullptr, DBUS_TYPE_INT32, &n, DBUS_TYPE_INVALID);
			dbus_message_unref(rep);
		}
		else if (AtspiGetProperty(dest, path, "org.a11y.atspi.Action", "NActions", &rep))
		{
			DBusMessageIter value, number;
			if (dbus_message_iter_init(rep, &value)
				&& dbus_message_iter_get_arg_type(&value) == DBUS_TYPE_VARIANT)
			{
				dbus_message_iter_recurse(&value, &number);
				if (dbus_message_iter_get_arg_type(&number) == DBUS_TYPE_INT32)
					dbus_message_iter_get_basic(&number, &n);
			}
			dbus_message_unref(rep);
		}
		bool found = false;
		for (int i = 0; i < n && !found; ++i)
		{
			DBusMessage *m = dbus_message_new_method_call(dest.c_str(), path.c_str(), "org.a11y.atspi.Action", "GetName");
			if (!m)
				break;
			int arg = i;
			if (dbus_message_append_args(m, DBUS_TYPE_INT32, &arg, DBUS_TYPE_INVALID))
			{
				DBusMessage *r = AtspiPendingReply(sBus, m, 3000, nullptr);
				if (r)
				{
					const char *s = nullptr;
					if (dbus_message_get_args(r, nullptr, DBUS_TYPE_STRING, &s, DBUS_TYPE_INVALID)
						&& s && strcmp(s, aNameOrNull) == 0)
					{
						idx = i;
						found = true;
					}
					dbus_message_unref(r);
				}
			}
			dbus_message_unref(m);
		}
		if (!found)
			return AtspiFail(n > 0 ? ENOENT : ENOTSUP);
	}
	DBusMessage *m = dbus_message_new_method_call(dest.c_str(), path.c_str(), "org.a11y.atspi.Action", "DoAction");
	if (!m)
		return false;
	int arg = idx;
	dbus_message_append_args(m, DBUS_TYPE_INT32, &arg, DBUS_TYPE_INVALID);
	DBusError err;
	dbus_error_init(&err);
	DBusMessage *rep = AtspiPendingReply(sBus, m, 5000, &err);
	dbus_message_unref(m);
	dbus_bool_t result = FALSE;
	if (rep)
	{
		dbus_message_get_args(rep, nullptr, DBUS_TYPE_BOOLEAN, &result, DBUS_TYPE_INVALID);
		dbus_message_unref(rep);
	}
	int code = result != FALSE ? 0 : AtspiDbusErrorCode(err, EIO);
	dbus_error_free(&err);
	AtspiSetLastError(code);
	return result != FALSE;
}

static const AtspiRef *AtspiFindRef(const std::string &aDest, const std::string &aPath)
{
	for (const auto &entry : sTable)
		if (entry.dest == aDest && entry.path == aPath)
			return &entry;
	return nullptr;
}

static bool AtspiKnownWithoutInterface(const std::string &aDest, const std::string &aPath,
	const char *aInterface)
{
	const AtspiRef *entry = AtspiFindRef(aDest, aPath);
	if (!entry || entry->interfaces.empty())
		return false; // fallback tree has no interface metadata: probe normally.
	std::string wanted(aInterface ? aInterface : "");
	size_t start = 0;
	while (start <= entry->interfaces.size())
	{
		size_t end = entry->interfaces.find(',', start);
		if (entry->interfaces.compare(start, end == std::string::npos
			? std::string::npos : end - start, wanted) == 0)
			return false;
		if (end == std::string::npos) break;
		start = end + 1;
	}
	return true;
}

static bool AtspiGetChildren(const std::string &aHandle, std::vector<AtspiRef> &aChildren)
{
	aChildren.clear();
	std::string dest, path;
	SplitHandle(aHandle, dest, path);
	DBusMessage *reply = AtspiCallTo(dest.c_str(), path.c_str(), IFACE, "GetChildren");
	if (!reply)
		return false;
	DBusMessageIter it;
	bool ok = dbus_message_iter_init(reply, &it)
		&& dbus_message_iter_get_arg_type(&it) == DBUS_TYPE_ARRAY;
	if (ok)
	{
		DBusMessageIter array;
		dbus_message_iter_recurse(&it, &array);
		while (dbus_message_iter_get_arg_type(&array) == DBUS_TYPE_STRUCT)
		{
			std::string child_dest, child_path;
			if (ReadRef(&array, child_dest, child_path) && !child_path.empty()
				&& child_path != "/org/a11y/atspi/null")
			{
				if (child_dest.empty()) child_dest = dest;
				AtspiRef child;
				child.dest = child_dest;
				child.path = child_path;
				if (const AtspiRef *cached = AtspiFindRef(child_dest, child_path))
					child.name = cached->name;
				else
					child.name = GetNameProp(child_dest.c_str(), child_path.c_str());
				aChildren.push_back(std::move(child));
			}
			dbus_message_iter_next(&array);
		}
	}
	dbus_message_unref(reply);
	return ok;
}

static bool AtspiReadIntReply(DBusMessage *aReply, int &aValue)
{
	if (!aReply)
		return false;
	DBusMessageIter it, value;
	if (!dbus_message_iter_init(aReply, &it))
		return false;
	if (dbus_message_iter_get_arg_type(&it) == DBUS_TYPE_VARIANT)
	{
		dbus_message_iter_recurse(&it, &value);
		if (dbus_message_iter_get_arg_type(&value) != DBUS_TYPE_INT32)
			return false;
		dbus_message_iter_get_basic(&value, &aValue);
		return true;
	}
	if (dbus_message_iter_get_arg_type(&it) != DBUS_TYPE_INT32)
		return false;
	dbus_message_iter_get_basic(&it, &aValue);
	return true;
}

static bool AtspiSelectionCount(const std::string &aHandle, int &aCount)
{
	aCount = 0;
	std::string dest, path;
	SplitHandle(aHandle, dest, path);
	DBusMessage *reply = nullptr;
	bool ok = AtspiGetProperty(dest, path, "org.a11y.atspi.Selection",
		"NSelectedChildren", &reply) && AtspiReadIntReply(reply, aCount);
	if (reply) dbus_message_unref(reply);
	if (!ok)
	{
		reply = AtspiCallTo(dest.c_str(), path.c_str(),
			"org.a11y.atspi.Selection", "GetNSelectedChildren");
		ok = AtspiReadIntReply(reply, aCount);
		if (reply) dbus_message_unref(reply);
	}
	return ok && aCount >= 0;
}

bool LinuxAtspiSelectionGetItems(const char *aPath, std::vector<std::string> &aItems)
{
	aItems.clear();
	if (!aPath || !*aPath)
		return AtspiFail(EINVAL);
	if (!LinuxAtspiAvailable())
		return false;
	int selected_count = 0;
	if (!AtspiSelectionCount(aPath, selected_count))
		return AtspiFail(ENOTSUP);
	std::vector<AtspiRef> children;
	if (!AtspiGetChildren(aPath, children))
		return AtspiFail(AtspiFailureCode(EIO));
	for (const auto &child : children)
		aItems.push_back(child.name);
	return AtspiSucceed();
}

bool LinuxAtspiSelectionSelect(const char *aPath, int aZeroBasedIndex)
{
	if (!aPath || !*aPath || aZeroBasedIndex < -1)
		return AtspiFail(EINVAL);
	if (!LinuxAtspiAvailable())
		return false;
	int selected_count = 0;
	if (!AtspiSelectionCount(aPath, selected_count))
		return AtspiFail(ENOTSUP);
	std::string dest, path;
	SplitHandle(aPath, dest, path);
	const char *method = aZeroBasedIndex < 0 ? "ClearSelection" : "SelectChild";
	DBusMessage *message = dbus_message_new_method_call(dest.c_str(), path.c_str(),
		"org.a11y.atspi.Selection", method);
	if (!message)
		return false;
	if (aZeroBasedIndex >= 0 && !dbus_message_append_args(message,
		DBUS_TYPE_INT32, &aZeroBasedIndex, DBUS_TYPE_INVALID))
	{
		dbus_message_unref(message);
		return false;
	}
	DBusError err;
	dbus_error_init(&err);
	int timeout = AtspiBoundTimeout(3000);
	DBusMessage *reply = timeout > 0
		? AtspiPendingReply(sBus, message, timeout, &err) : nullptr;
	dbus_message_unref(message);
	dbus_bool_t result = FALSE;
	if (reply)
	{
		dbus_message_get_args(reply, nullptr, DBUS_TYPE_BOOLEAN, &result, DBUS_TYPE_INVALID);
		dbus_message_unref(reply);
	}
	int code = result != FALSE ? 0 : AtspiDbusErrorCode(err, EIO);
	dbus_error_free(&err);
	AtspiSetLastError(code);
	return result != FALSE;
}

bool LinuxAtspiSelectionGetSelected(const char *aPath, int &aZeroBasedIndex, std::string &aName)
{
	aZeroBasedIndex = -1;
	aName.clear();
	if (!aPath || !*aPath)
		return AtspiFail(EINVAL);
	if (!LinuxAtspiAvailable())
		return false;
	int selected_count = 0;
	if (!AtspiSelectionCount(aPath, selected_count))
		return AtspiFail(ENOTSUP);
	if (!selected_count)
		return AtspiSucceed();
	std::string dest, path;
	SplitHandle(aPath, dest, path);
	DBusMessage *message = dbus_message_new_method_call(dest.c_str(), path.c_str(),
		"org.a11y.atspi.Selection", "GetSelectedChild");
	if (!message)
		return AtspiFail(ENOMEM);
	int selected_index = 0;
	if (!dbus_message_append_args(message, DBUS_TYPE_INT32, &selected_index, DBUS_TYPE_INVALID))
	{
		dbus_message_unref(message);
		return AtspiFail(EINVAL);
	}
	DBusError err;
	dbus_error_init(&err);
	int timeout = AtspiBoundTimeout(3000);
	DBusMessage *reply = timeout > 0
		? AtspiPendingReply(sBus, message, timeout, &err) : nullptr;
	dbus_message_unref(message);
	if (!reply)
	{
		int code = AtspiDbusErrorCode(err, EIO);
		dbus_error_free(&err);
		return AtspiFail(code);
	}
	DBusMessageIter it;
	std::string child_dest, child_path;
	bool ok = dbus_message_iter_init(reply, &it) && ReadRef(&it, child_dest, child_path);
	dbus_message_unref(reply);
	dbus_error_free(&err);
	if (!ok)
		return AtspiFail(EPROTO);
	if (child_path.empty() || child_path == "/org/a11y/atspi/null")
		return AtspiSucceed();
	if (child_dest.empty()) child_dest = dest;
	if (const AtspiRef *cached = AtspiFindRef(child_dest, child_path))
		aName = cached->name;
	else
		aName = GetNameProp(child_dest.c_str(), child_path.c_str());
	DBusMessage *index_reply = AtspiCallTo(child_dest.c_str(), child_path.c_str(),
		IFACE, "GetIndexInParent");
	if (index_reply)
	{
		dbus_message_get_args(index_reply, nullptr, DBUS_TYPE_INT32,
			&aZeroBasedIndex, DBUS_TYPE_INVALID);
		dbus_message_unref(index_reply);
	}
	if (aZeroBasedIndex < 0)
	{
		std::vector<AtspiRef> children;
		if (AtspiGetChildren(aPath, children))
			for (size_t i = 0; i < children.size(); ++i)
				if (children[i].dest == child_dest && children[i].path == child_path)
				{
					aZeroBasedIndex = (int)i;
					break;
				}
	}
	AtspiSetLastError(aZeroBasedIndex >= 0 ? 0 : EPROTO);
	return aZeroBasedIndex >= 0;
}

static bool AtspiGetProperty(const std::string &aDest, const std::string &aPath,
	const char *aIface, const char *aProperty, DBusMessage **aReply)
{
	*aReply = nullptr;
	DBusMessage *message = dbus_message_new_method_call(aDest.c_str(), aPath.c_str(),
		"org.freedesktop.DBus.Properties", "Get");
	if (!message)
		return false;
	if (!dbus_message_append_args(message, DBUS_TYPE_STRING, &aIface,
		DBUS_TYPE_STRING, &aProperty, DBUS_TYPE_INVALID))
	{
		dbus_message_unref(message);
		return false;
	}
	DBusError err;
	dbus_error_init(&err);
	int timeout = AtspiBoundTimeout(3000);
	*aReply = timeout > 0
		? AtspiPendingReply(sBus, message, timeout, &err) : nullptr;
	dbus_message_unref(message);
	dbus_error_free(&err);
	return *aReply != nullptr;
}

bool LinuxAtspiGetValue(const char *aPath, double &aValue, std::string *aText)
{
	if (!aPath || !*aPath)
		return AtspiFail(EINVAL);
	if (!LinuxAtspiAvailable())
		return false;
	std::string dest, path;
	SplitHandle(aPath, dest, path);
	if (AtspiKnownWithoutInterface(dest, path, "org.a11y.atspi.Value"))
		return AtspiFail(ENOTSUP);
	DBusMessage *reply = nullptr;
	if (!AtspiGetProperty(dest, path, "org.a11y.atspi.Value", "CurrentValue", &reply))
		return AtspiFail(AtspiFailureCode(ENOTSUP));
	DBusMessageIter it, variant;
	bool ok = dbus_message_iter_init(reply, &it)
		&& dbus_message_iter_get_arg_type(&it) == DBUS_TYPE_VARIANT;
	if (ok)
	{
		dbus_message_iter_recurse(&it, &variant);
		ok = dbus_message_iter_get_arg_type(&variant) == DBUS_TYPE_DOUBLE;
		if (ok) dbus_message_iter_get_basic(&variant, &aValue);
	}
	dbus_message_unref(reply);
	if (!ok)
		return AtspiFail(EPROTO);
	if (aText)
	{
		aText->clear();
		if (AtspiGetProperty(dest, path, "org.a11y.atspi.Value", "Text", &reply))
		{
			if (dbus_message_iter_init(reply, &it)
				&& dbus_message_iter_get_arg_type(&it) == DBUS_TYPE_VARIANT)
			{
				dbus_message_iter_recurse(&it, &variant);
				if (dbus_message_iter_get_arg_type(&variant) == DBUS_TYPE_STRING)
				{
					const char *text = nullptr;
					dbus_message_iter_get_basic(&variant, &text);
					*aText = text ? text : "";
				}
			}
			dbus_message_unref(reply);
		}
		if (aText->empty())
		{
			char number[64];
			snprintf(number, sizeof(number), "%.15g", aValue);
			*aText = number;
		}
	}
	return AtspiSucceed();
}

bool LinuxAtspiSetValue(const char *aPath, double aValue)
{
	if (!aPath || !*aPath)
		return AtspiFail(EINVAL);
	if (!LinuxAtspiAvailable())
		return false;
	std::string dest, path;
	SplitHandle(aPath, dest, path);
	if (AtspiKnownWithoutInterface(dest, path, "org.a11y.atspi.Value"))
		return AtspiFail(ENOTSUP);
	DBusMessage *message = dbus_message_new_method_call(dest.c_str(), path.c_str(),
		"org.freedesktop.DBus.Properties", "Set");
	if (!message)
		return false;
	const char *iface = "org.a11y.atspi.Value";
	const char *property = "CurrentValue";
	DBusMessageIter it, variant;
	dbus_message_iter_init_append(message, &it);
	bool ok = dbus_message_iter_append_basic(&it, DBUS_TYPE_STRING, &iface)
		&& dbus_message_iter_append_basic(&it, DBUS_TYPE_STRING, &property)
		&& dbus_message_iter_open_container(&it, DBUS_TYPE_VARIANT, "d", &variant)
		&& dbus_message_iter_append_basic(&variant, DBUS_TYPE_DOUBLE, &aValue)
		&& dbus_message_iter_close_container(&it, &variant);
	if (!ok)
	{
		dbus_message_unref(message);
		return false;
	}
	DBusError err;
	dbus_error_init(&err);
	int timeout = AtspiBoundTimeout(3000);
	DBusMessage *reply = timeout > 0
		? AtspiPendingReply(sBus, message, timeout, &err) : nullptr;
	dbus_message_unref(message);
	ok = reply && dbus_message_get_type(reply) == DBUS_MESSAGE_TYPE_METHOD_RETURN;
	if (reply) dbus_message_unref(reply);
	int code = ok ? 0 : AtspiDbusErrorCode(err, EIO);
	dbus_error_free(&err);
	if (!ok)
		return AtspiFail(code);
	// Some bridges (notably Java ATK Wrapper 0.42.1) advertise CurrentValue as
	// readwrite and return METHOD_RETURN from Properties.Set, yet silently keep
	// the old value. Enforce the project-wide real-effect-or-error contract.
	uint64_t saved_deadline = sQueryDeadlineUs;
	uint64_t verify_deadline = AtspiNowUs() + 250000ULL;
	if (!sQueryDeadlineUs || verify_deadline < sQueryDeadlineUs)
		sQueryDeadlineUs = verify_deadline;
	for (int attempt = 0; attempt < 5; ++attempt)
	{
		double observed = 0.0;
		if (LinuxAtspiGetValue(aPath, observed, nullptr))
		{
			bool same = false;
			if (std::isnan(aValue))
				same = std::isnan(observed);
			else if (std::isinf(aValue))
				same = observed == aValue;
			else
			{
				double tolerance = std::max(1e-9, std::fabs(aValue) * 1e-9);
				same = std::fabs(observed - aValue) <= tolerance;
			}
			if (same)
			{
				sQueryDeadlineUs = saved_deadline;
				return AtspiSucceed();
			}
		}
		if (attempt < 4)
			MsgSleep(10, RETURN_AFTER_MESSAGES_SPECIAL_FILTER);
	}
	sQueryDeadlineUs = saved_deadline;
	return AtspiFail(EIO);
}
