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
#include "core_atspi_linux.h"
#include <dbus/dbus.h>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <ctime>
#include <string>
#include <vector>

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
};
std::vector<AtspiRef> sTable;

// ---------------------------------------------------------------------------
// Low-level helpers
// ---------------------------------------------------------------------------

// One-shot synchronous call on the at-spi bus; caller owns the reply.
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
	DBusMessage *rep = dbus_connection_send_with_reply_and_block(bus, msg, aTimeoutMs, &err);
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
	DBusMessage *rep = dbus_connection_send_with_reply_and_block(sBus, msg, aTimeoutMs, &err);
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
std::string GetNameProp(const char *aDest, const char *aPath)
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
	int timeout = AtspiBoundTimeout(2000);
	if (timeout <= 0)
	{
		dbus_message_unref(msg);
		dbus_error_free(&err);
		return std::string();
	}
	DBusMessage *rep = dbus_connection_send_with_reply_and_block(sBus, msg, timeout, &err);
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
// ((so)(so)(so)iiassusau).  We only need the main object reference and its
// cached Name; role/states remain in the reply for future enrichment.
static bool ReadCacheItemNew(DBusMessageIter *aItem, const char *aFallbackDest, AtspiRef &aOut)
{
	if (dbus_message_iter_get_arg_type(aItem) != DBUS_TYPE_STRUCT)
		return false;
	DBusMessageIter fields;
	dbus_message_iter_recurse(aItem, &fields);
	std::string obj_dest, obj_path;
	if (!ReadRef(&fields, obj_dest, obj_path) || obj_path.empty())
		return false;
	// main ref -> application ref -> parent ref -> index -> child count ->
	// interfaces -> name (six iterator advances).
	for (int i = 0; i < 6; ++i)
		if (!dbus_message_iter_next(&fields))
			return false;
	if (dbus_message_iter_get_arg_type(&fields) != DBUS_TYPE_STRING)
		return false; // Old Qt signature reaches a different type here.
	const char *name = nullptr;
	dbus_message_iter_get_basic(&fields, &name);
	aOut.name = name ? name : "";
	aOut.path = obj_path;
	aOut.dest = obj_dest.empty() ? (aFallbackDest ? aFallbackDest : "") : obj_dest;
	return !aOut.dest.empty();
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
	DBusMessageIter it;
	if (!dbus_message_iter_init(rep, &it)
		|| dbus_message_iter_get_arg_type(&it) != DBUS_TYPE_ARRAY)
	{
		dbus_message_unref(rep);
		return false;
	}
	DBusMessageIter arr;
	dbus_message_iter_recurse(&it, &arr);
	size_t before = sTable.size();
	int parsed = 0;
	bool valid = true;
	while (dbus_message_iter_get_arg_type(&arr) != DBUS_TYPE_INVALID)
	{
		if (AtspiBudgetExpired())
			break; // keep already parsed cache entries as a partial result
		AtspiRef e;
		if (!ReadCacheItemNew(&arr, aDest, e))
		{
			valid = false;
			break;
		}
		sTable.push_back(std::move(e));
		++parsed;
		dbus_message_iter_next(&arr);
	}
	dbus_message_unref(rep);
	if (!valid || parsed == 0)
	{
		sTable.resize(before);
		return false;
	}
	++sLastCacheApps;
	sLastCacheItems += parsed;
	return true;
}

void WalkChildren(const char *aDest, const char *aPath, int aDepth, int aMaxDepth);

// Read the desktop's application roots once, then load each application's
// cache in bulk.  The registry itself has no per-app cache.
static bool RefreshApplications(int aMaxDepth)
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
	DBusMessageIter arr;
	dbus_message_iter_recurse(&it, &arr);
	while (dbus_message_iter_get_arg_type(&arr) != DBUS_TYPE_INVALID)
	{
		if (AtspiBudgetExpired())
			break;
		std::string owner, path;
		if (ReadRef(&arr, owner, path) && !owner.empty() && !path.empty())
		{
			std::string disp = owner;
			if (disp[0] == ':')
			{
				std::string n = GetNameProp(owner.c_str(), path.c_str());
				if (!n.empty())
					disp = n;
			}
			sTable.push_back(AtspiRef{disp, path, owner});
			if (!LoadCacheForApp(owner.c_str()))
			{
				if (AtspiBudgetExpired())
					break;
				++sLastFallbackApps;
				WalkChildren(owner.c_str(), path.c_str(), 1, aMaxDepth);
			}
		}
		dbus_message_iter_next(&arr);
	}
	dbus_message_unref(rep);
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
	DBusMessage *rep = m ? dbus_connection_send_with_reply_and_block(sess, m, 2000, &err) : nullptr;
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
	return InitOnce() && sBus != nullptr;
}

int LinuxAtspiRefresh()
{
	sTable.clear();
	sLastCacheApps = 0;
	sLastFallbackApps = 0;
	sLastCacheItems = 0;
	sLastBudgetExceeded = false;
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
	if (!RefreshApplications(max))
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
			fprintf(f, "# cache_apps=%d fallback_apps=%d cache_items=%d nodes=%zu budget_ms=%d budget_exceeded=%d\n",
				sLastCacheApps, sLastFallbackApps, sLastCacheItems, sTable.size(),
				sLastBudgetMs, sLastBudgetExceeded ? 1 : 0);
			for (const auto &e : sTable)
				fprintf(f, "%s\t%s\n", e.name.c_str(), e.path.c_str());
			fclose(f);
		}
	}
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
		return false;
	aOutPath.clear();
	std::string n(aName);
	std::string scope_dest;
	AtspiDumpMatches(aWindowTitle, aName);
	if (aWindowTitle && *aWindowTitle)
		if (!AtspiScopeForWindowTitle(aWindowTitle, scope_dest))
			return false; // the named window is not on the a11y tree
	auto in_scope = [&](const AtspiRef &e) {
		return scope_dest.empty() || e.dest == scope_dest;
	};
	for (const auto &e : sTable)
		if (!AtspiIsAppRoot(e) && in_scope(e) && e.name == n) { aOutPath = e.dest + "|" + e.path; return true; }
	for (const auto &e : sTable)
		if (!AtspiIsAppRoot(e) && in_scope(e) && e.name.find(n) != std::string::npos) { aOutPath = e.dest + "|" + e.path; return true; }
	return false;
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

bool LinuxAtspiGetText(const char *aPath, std::string &aText)
{
	if (!aPath || !*aPath || !LinuxAtspiAvailable())
		return false;
	std::string dest, path;
	SplitHandle(aPath, dest, path);
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
	DBusMessage *rep = dbus_connection_send_with_reply_and_block(sBus, msg, 3000, &err);
	dbus_message_unref(msg);
	if (!rep)
	{
		dbus_error_free(&err);
		return false;
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
	return ok;
}

bool LinuxAtspiSetText(const char *aPath, const char *aText)
{
	if (!aPath || !*aPath || !LinuxAtspiAvailable())
		return false;
	std::string dest, path;
	SplitHandle(aPath, dest, path);
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
	DBusMessage *rep = dbus_connection_send_with_reply_and_block(sBus, msg, 3000, &err);
	dbus_message_unref(msg);
	bool ok = rep != nullptr;
	if (rep)
		dbus_message_unref(rep);
	dbus_error_free(&err);
	return ok;
}

bool LinuxAtspiDoAction(const char *aPath, int aIndex, const char *aNameOrNull)
{
	if (!aPath || !*aPath || !LinuxAtspiAvailable())
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
		bool found = false;
		for (int i = 0; i < n && !found; ++i)
		{
			DBusMessage *m = dbus_message_new_method_call(dest.c_str(), path.c_str(), "org.a11y.atspi.Action", "GetName");
			if (!m)
				break;
			int arg = i;
			if (dbus_message_append_args(m, DBUS_TYPE_INT32, &arg, DBUS_TYPE_INVALID))
			{
				DBusMessage *r = dbus_connection_send_with_reply_and_block(sBus, m, 3000, nullptr);
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
			return false;
	}
	DBusMessage *m = dbus_message_new_method_call(dest.c_str(), path.c_str(), "org.a11y.atspi.Action", "DoAction");
	if (!m)
		return false;
	int arg = idx;
	dbus_message_append_args(m, DBUS_TYPE_INT32, &arg, DBUS_TYPE_INVALID);
	DBusError err;
	dbus_error_init(&err);
	DBusMessage *rep = dbus_connection_send_with_reply_and_block(sBus, m, 5000, &err);
	dbus_message_unref(m);
	bool ok = false;
	if (rep)
	{
		dbus_message_get_args(rep, nullptr, DBUS_TYPE_BOOLEAN, &ok, DBUS_TYPE_INVALID);
		dbus_message_unref(rep);
	}
	dbus_error_free(&err);
	return ok;
}