// AT-SPI accessibility backend (check0820 direction-B item 2).
//
// Minimal libdbus client for the AT-SPI bus.  GTK/Qt/Electron apps with
// accessibility enabled expose org.a11y.atspi.Accessible objects on the
// at-spi bus; this module walks the desktop tree and provides the
// primitives the Wayland Control* fallback needs:
//   - bus address: org.a11y.Bus.GetAddress (on the session bus) returns
//     the at-spi bus address; the registry lives there as
//     org.a11y.atspi.Registry with root /org/a11y/atspi/accessible/root.
//   - Accessible.GetChildren -> a(so) [name, object-path]
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
#include <string>
#include <vector>

namespace {

const char *REG = "org.a11y.atspi.Registry";
const char *IFACE = "org.a11y.atspi.Accessible";

DBusConnection *sBus = nullptr;
bool sTried = false;

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
	DBusMessage *rep = dbus_connection_send_with_reply_and_block(sBus, msg, 2000, &err);
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

// Recursive BFS over the accessible tree (bounded depth + node count).
// AT-SPI model: root's children are [unique-bus-name, /org/a11y/atspi/
// accessible/root] placeholders; the NAME is the D-Bus destination that
// hosts the app's objects (the path is shared with the registry).
void WalkChildren(const char *aDest, const char *aPath, int aDepth, int aMaxDepth)
{
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
	if (!LinuxAtspiAvailable())
		return 0;
	int max = 6; // Default depth reaches app windows and common controls.
	if (const char *extra = getenv("AHK_ATSPI_MAXDEPTH"))
		max = atoi(extra);
	if (max < 1)
		max = 1;
	WalkChildren(REG, "/org/a11y/atspi/accessible/root", 1, max);
	// Diagnostics: AHK_ATSPI_DUMP=<path> writes the tree (name<TAB>path).
	if (const char *dump = getenv("AHK_ATSPI_DUMP"))
	{
		FILE *f = fopen(dump, "w");
		if (f)
		{
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

bool LinuxAtspiFindByName(const char *aName, std::string &aOutPath)
{
	if (!aName || !*aName)
		return false;
	aOutPath.clear();
	std::string n(aName);
	for (const auto &e : sTable)
		if (e.name == n) { aOutPath = e.dest + "|" + e.path; return true; }
	for (const auto &e : sTable)
		if (e.name.find(n) != std::string::npos) { aOutPath = e.dest + "|" + e.path; return true; }
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