// Linux IME integration: framework/engine status plus committed-text capture.
// IBus InputContext signals are unicast to the owning toolkit connection, so
// the listener uses the private IBus bus with an explicit eavesdrop match. The
// GNOME VM oracle proves this sees a second process's libpinyin preedit/commit.
// Fcitx5 uses the documented session-bus InputContext1 signal interface.
#include "core_ime_linux.h"
#include "core_win_linux.h"
#include "core_capture_linux.h"
#include "../../stdafx.h"
#include <dbus/dbus.h>
#include <X11/Xlib.h>
#include <X11/XKBlib.h>
#include <glob.h>
#include <sys/stat.h>
#include <cstdlib>
#include <cstring>
#include <cstdio>
#include <string>

namespace
{

DBusConnection *sImeBus = nullptr;
int sListenerFramework = LINUX_IME_NONE;
bool sListenerAttempted = false;
DWORD sLastAttemptTick = 0;
bool sPreeditActive = false;
std::string sEngine;
bool sFocusTrackingSeen = false;
std::string sFocusedContextPath;
std::string sLastCommit;
unsigned long sCommitCount = 0;
unsigned long sPreeditCount = 0;

void ImeDump(const char *aKind, const std::string &aText, bool aVisible)
{
	const char *path = getenv("AHK_IME_DUMP");
	if (!path || !*path)
		return;
	FILE *file = fopen(path, "a");
	if (!file)
		return;
	fprintf(file, "%s\tframework=%s\tvisible=%d\ttext=%s\n", aKind,
		sListenerFramework == LINUX_IME_IBUS ? "ibus" :
		sListenerFramework == LINUX_IME_FCITX5 ? "fcitx5" : "none",
		aVisible ? 1 : 0, aText.c_str());
	fclose(file);
}

bool NameHasOwner(const char *aName)
{
	DBusError err;
	dbus_error_init(&err);
	DBusConnection *connection = dbus_bus_get(DBUS_BUS_SESSION, &err);
	if (!connection)
	{
		dbus_error_free(&err);
		return false;
	}
	DBusMessage *message = dbus_message_new_method_call("org.freedesktop.DBus",
		"/org/freedesktop/DBus", "org.freedesktop.DBus", "NameHasOwner");
	if (!message)
	{
		dbus_connection_unref(connection);
		dbus_error_free(&err);
		return false;
	}
	dbus_message_append_args(message, DBUS_TYPE_STRING, &aName, DBUS_TYPE_INVALID);
	DBusMessage *reply = dbus_connection_send_with_reply_and_block(connection, message, 1000, &err);
	dbus_message_unref(message);
	dbus_bool_t owner = FALSE;
	if (reply)
	{
		dbus_message_get_args(reply, nullptr, DBUS_TYPE_BOOLEAN, &owner, DBUS_TYPE_INVALID);
		dbus_message_unref(reply);
	}
	dbus_connection_unref(connection);
	dbus_error_free(&err);
	return owner != FALSE;
}

std::string ReadAddressFile(const char *aPath)
{
	FILE *file = fopen(aPath, "r");
	if (!file)
		return std::string();
	char line[4096];
	std::string address;
	while (fgets(line, sizeof(line), file))
		if (!strncmp(line, "IBUS_ADDRESS=", 13))
		{
			char *value = line + 13;
			value[strcspn(value, "\r\n")] = 0;
			address = value;
			break;
		}
	fclose(file);
	return address;
}

std::string ResolveIbusAddress()
{
	if (const char *address = getenv("IBUS_ADDRESS"))
		if (*address)
			return address;
	std::string config;
	if (const char *xdg = getenv("XDG_CONFIG_HOME"))
		config = xdg;
	else if (const char *home = getenv("HOME"))
		config = std::string(home) + "/.config";
	if (config.empty())
		return std::string();
	glob_t files {};
	std::string pattern = config + "/ibus/bus/*-unix-*";
	if (glob(pattern.c_str(), 0, nullptr, &files) != 0)
		return std::string();
	std::string address;
	time_t newest = 0;
	for (size_t i = 0; i < files.gl_pathc; ++i)
	{
		struct stat st {};
		if (stat(files.gl_pathv[i], &st) != 0 || !S_ISREG(st.st_mode) || st.st_mtime < newest)
			continue;
		std::string candidate = ReadAddressFile(files.gl_pathv[i]);
		if (!candidate.empty())
		{
			address = candidate;
			newest = st.st_mtime;
		}
	}
	globfree(&files);
	return address;
}

DBusConnection *OpenPrivateBus(const std::string &aAddress)
{
	if (aAddress.empty())
		return nullptr;
	DBusError err;
	dbus_error_init(&err);
	DBusConnection *connection = dbus_connection_open_private(aAddress.c_str(), &err);
	if (!connection)
	{
		dbus_error_free(&err);
		return nullptr;
	}
	dbus_connection_set_exit_on_disconnect(connection, FALSE);
	if (!dbus_bus_register(connection, &err))
	{
		dbus_error_free(&err);
		dbus_connection_close(connection);
		dbus_connection_unref(connection);
		return nullptr;
	}
	return connection;
}

bool AddEavesdropMatch(DBusConnection *aConnection, const char *aInterface)
{
	if (!aConnection)
		return false;
	std::string match = "interface='";
	match += aInterface;
	match += "',eavesdrop='true'";
	DBusError err;
	dbus_error_init(&err);
	dbus_bus_add_match(aConnection, match.c_str(), &err);
	dbus_connection_flush(aConnection);
	bool ok = !dbus_error_is_set(&err);
	dbus_error_free(&err);
	return ok;
}

bool ParseIbusText(DBusMessageIter *aArgument, std::string &aText)
{
	aText.clear();
	if (!aArgument || dbus_message_iter_get_arg_type(aArgument) != DBUS_TYPE_VARIANT)
		return false;
	DBusMessageIter variant;
	dbus_message_iter_recurse(aArgument, &variant);
	if (dbus_message_iter_get_arg_type(&variant) != DBUS_TYPE_STRUCT)
		return false;
	DBusMessageIter fields;
	dbus_message_iter_recurse(&variant, &fields);
	const char *type = nullptr;
	if (dbus_message_iter_get_arg_type(&fields) != DBUS_TYPE_STRING)
		return false;
	dbus_message_iter_get_basic(&fields, &type);
	if (!type || strcmp(type, "IBusText") != 0)
		return false;
	if (!dbus_message_iter_next(&fields) || !dbus_message_iter_next(&fields)
		|| dbus_message_iter_get_arg_type(&fields) != DBUS_TYPE_STRING)
		return false;
	const char *text = nullptr;
	dbus_message_iter_get_basic(&fields, &text);
	aText = text ? text : "";
	return true;
}

bool FindSerializedName(DBusMessageIter *aValue, const char *aTypeName, std::string &aName)
{
	int type = dbus_message_iter_get_arg_type(aValue);
	if (type == DBUS_TYPE_VARIANT)
	{
		DBusMessageIter nested;
		dbus_message_iter_recurse(aValue, &nested);
		return FindSerializedName(&nested, aTypeName, aName);
	}
	if (type != DBUS_TYPE_STRUCT)
		return false;
	DBusMessageIter fields;
	dbus_message_iter_recurse(aValue, &fields);
	const char *kind = nullptr;
	if (dbus_message_iter_get_arg_type(&fields) != DBUS_TYPE_STRING)
		return false;
	dbus_message_iter_get_basic(&fields, &kind);
	if (!kind || strcmp(kind, aTypeName))
		return false;
	if (!dbus_message_iter_next(&fields) || !dbus_message_iter_next(&fields)
		|| dbus_message_iter_get_arg_type(&fields) != DBUS_TYPE_STRING)
		return false;
	const char *name = nullptr;
	dbus_message_iter_get_basic(&fields, &name);
	aName = name ? name : "";
	return !aName.empty();
}

std::string QueryIbusEngine(DBusConnection *aConnection)
{
	DBusMessage *message = dbus_message_new_method_call("org.freedesktop.IBus",
		"/org/freedesktop/IBus", "org.freedesktop.DBus.Properties", "Get");
	if (!message)
		return std::string();
	const char *iface = "org.freedesktop.IBus";
	const char *property = "GlobalEngine";
	dbus_message_append_args(message, DBUS_TYPE_STRING, &iface,
		DBUS_TYPE_STRING, &property, DBUS_TYPE_INVALID);
	DBusMessage *reply = dbus_connection_send_with_reply_and_block(aConnection, message, 1000, nullptr);
	dbus_message_unref(message);
	std::string engine;
	if (reply)
	{
		DBusMessageIter it;
		if (dbus_message_iter_init(reply, &it))
			FindSerializedName(&it, "IBusEngineDesc", engine);
		dbus_message_unref(reply);
	}
	return engine;
}

std::string QueryFcitxEngine(DBusConnection *aConnection)
{
	DBusMessage *message = dbus_message_new_method_call("org.fcitx.Fcitx5",
		"/controller", "org.fcitx.Fcitx.Controller1", "CurrentInputMethod");
	if (!message)
		return std::string();
	DBusMessage *reply = dbus_connection_send_with_reply_and_block(aConnection, message, 1000, nullptr);
	dbus_message_unref(message);
	std::string engine;
	if (reply)
	{
		const char *name = nullptr;
		if (dbus_message_get_args(reply, nullptr, DBUS_TYPE_STRING, &name, DBUS_TYPE_INVALID))
			engine = name ? name : "";
		dbus_message_unref(reply);
	}
	return engine;
}

std::string ParseFcitxPreedit(DBusMessageIter *aArgument)
{
	std::string text;
	if (!aArgument || dbus_message_iter_get_arg_type(aArgument) != DBUS_TYPE_ARRAY)
		return text;
	DBusMessageIter array;
	dbus_message_iter_recurse(aArgument, &array);
	while (dbus_message_iter_get_arg_type(&array) == DBUS_TYPE_STRUCT)
	{
		DBusMessageIter fields;
		dbus_message_iter_recurse(&array, &fields);
		if (dbus_message_iter_get_arg_type(&fields) == DBUS_TYPE_STRING)
		{
			const char *part = nullptr;
			dbus_message_iter_get_basic(&fields, &part);
			if (part) text += part;
		}
		dbus_message_iter_next(&array);
	}
	return text;
}

void HandleCommit(const std::string &aText, AhkInputOrigin aOrigin)
{
	if (aText.empty())
		return;
	sPreeditActive = false;
	sLastCommit = aText;
	++sCommitCount;
	ImeDump("commit", aText, false);
	LinuxCaptureImeCommit(aText.c_str(), aOrigin);
}

void HandlePreedit(const std::string &aText, bool aVisible)
{
	sPreeditActive = aVisible && !aText.empty();
	++sPreeditCount;
	ImeDump("preedit", aText, sPreeditActive);
	LinuxCaptureImePreedit(aText.c_str(), sPreeditActive);
}

bool HandleFocusMessage(DBusMessage *aMessage, const char *aInterface)
{
	if (!aMessage || !aInterface || !dbus_message_has_interface(aMessage, aInterface)
		|| dbus_message_get_type(aMessage) != DBUS_MESSAGE_TYPE_METHOD_CALL)
		return false;
	const char *member = dbus_message_get_member(aMessage);
	const char *path = dbus_message_get_path(aMessage);
	if (!member || !path)
		return false;
	if (!strcmp(member, "FocusIn"))
	{
		sFocusTrackingSeen = true;
		sFocusedContextPath = path;
		ImeDump("focus-in", sFocusedContextPath, true);
		return true;
	}
	if (!strcmp(member, "FocusOut"))
	{
		sFocusTrackingSeen = true;
		if (sFocusedContextPath == path)
			sFocusedContextPath.clear();
		ImeDump("focus-out", path, false);
		return true;
	}
	return false;
}

bool MessageMatchesFocusedContext(DBusMessage *aMessage)
{
	if (!sFocusTrackingSeen)
		return true; // Protocol peers may not expose focus method calls.
	if (sFocusedContextPath.empty())
		return false;
	const char *path = dbus_message_get_path(aMessage);
	return path && sFocusedContextPath == path;
}

void HandleIbusMessage(DBusMessage *aMessage)
{
	if (HandleFocusMessage(aMessage, "org.freedesktop.IBus.InputContext"))
		return;
	if (!MessageMatchesFocusedContext(aMessage))
		return;
	if (dbus_message_is_signal(aMessage, "org.freedesktop.IBus.InputContext", "CommitText"))
	{
		DBusMessageIter it;
		std::string text;
		if (dbus_message_iter_init(aMessage, &it) && ParseIbusText(&it, text))
			HandleCommit(text, AhkInputOrigin::IBUS);
	}
	else if (dbus_message_is_signal(aMessage, "org.freedesktop.IBus.InputContext", "UpdatePreeditText")
		|| dbus_message_is_signal(aMessage, "org.freedesktop.IBus.InputContext", "UpdatePreeditTextWithMode"))
	{
		DBusMessageIter it;
		std::string text;
		bool visible = false;
		if (dbus_message_iter_init(aMessage, &it) && ParseIbusText(&it, text)
			&& dbus_message_iter_next(&it) && dbus_message_iter_next(&it)
			&& dbus_message_iter_get_arg_type(&it) == DBUS_TYPE_BOOLEAN)
		{
			dbus_bool_t flag = FALSE;
			dbus_message_iter_get_basic(&it, &flag);
			visible = flag != FALSE;
		}
		HandlePreedit(text, visible);
	}
	else if (dbus_message_is_signal(aMessage, "org.freedesktop.IBus.InputContext", "HidePreeditText"))
		HandlePreedit(std::string(), false);
}

void HandleFcitxMessage(DBusMessage *aMessage)
{
	if (HandleFocusMessage(aMessage, "org.fcitx.Fcitx.InputContext1"))
		return;
	if (!MessageMatchesFocusedContext(aMessage))
		return;
	if (dbus_message_is_signal(aMessage, "org.fcitx.Fcitx.InputContext1", "CommitString"))
	{
		const char *text = nullptr;
		if (dbus_message_get_args(aMessage, nullptr, DBUS_TYPE_STRING, &text, DBUS_TYPE_INVALID))
			HandleCommit(text ? text : "", AhkInputOrigin::FCITX5);
	}
	else if (dbus_message_is_signal(aMessage, "org.fcitx.Fcitx.InputContext1", "UpdateFormattedPreedit")
		|| dbus_message_is_signal(aMessage, "org.fcitx.Fcitx.InputContext1", "UpdateClientSideUI"))
	{
		DBusMessageIter it;
		std::string text;
		if (dbus_message_iter_init(aMessage, &it))
			text = ParseFcitxPreedit(&it);
		HandlePreedit(text, !text.empty());
	}
	else if (dbus_message_is_signal(aMessage, "org.fcitx.Fcitx.InputContext1", "CurrentIM"))
	{
		DBusMessageIter it;
		if (dbus_message_iter_init(aMessage, &it)
			&& dbus_message_iter_get_arg_type(&it) == DBUS_TYPE_STRING)
		{
			const char *name = nullptr;
			dbus_message_iter_get_basic(&it, &name);
			sEngine = name ? name : "";
		}
	}
}

void CloseListener()
{
	if (sImeBus)
	{
		dbus_connection_close(sImeBus);
		dbus_connection_unref(sImeBus);
		sImeBus = nullptr;
	}
	sListenerFramework = LINUX_IME_NONE;
	sEngine.clear();
	sFocusTrackingSeen = false;
	sFocusedContextPath.clear();
	sPreeditActive = false;
	LinuxCaptureImePreedit("", false);
}

} // namespace

int LinuxImeXkbGroup()
{
	Display *display = LinuxX11Display();
	if (!display)
		return -1;
	XkbStateRec state;
	if (XkbGetState(display, XkbUseCoreKbd, &state) == Success)
		return (int)state.group;
	return -1;
}

int LinuxImeFramework()
{
	if (NameHasOwner("org.freedesktop.IBus"))
		return LINUX_IME_IBUS;
	if (NameHasOwner("org.fcitx.Fcitx5.Controller1")
		|| NameHasOwner("org.fcitx.Fcitx5")
		|| NameHasOwner("org.fcitx.Fcitx.Controller1"))
		return LINUX_IME_FCITX5;
	return LINUX_IME_NONE;
}

bool LinuxImeStartListener()
{
	if (sImeBus)
		return true;
	DWORD now = GetTickCount();
	if (sListenerAttempted && now - sLastAttemptTick < 2000)
		return false;
	sListenerAttempted = true;
	sLastAttemptTick = now;
	int framework = LinuxImeFramework();
	std::string address;
	const char *iface = nullptr;
	if (framework == LINUX_IME_IBUS)
	{
		address = ResolveIbusAddress();
		iface = "org.freedesktop.IBus.InputContext";
	}
	else if (framework == LINUX_IME_FCITX5)
	{
		const char *session = getenv("DBUS_SESSION_BUS_ADDRESS");
		if (session) address = session;
		iface = "org.fcitx.Fcitx.InputContext1";
	}
	if (address.empty() || !iface)
		return false;
	DBusConnection *connection = OpenPrivateBus(address);
	if (!connection || !AddEavesdropMatch(connection, iface))
	{
		if (connection)
		{
			dbus_connection_close(connection);
			dbus_connection_unref(connection);
		}
		return false;
	}
	sImeBus = connection;
	sListenerFramework = framework;
	sEngine = framework == LINUX_IME_IBUS
		? QueryIbusEngine(connection) : QueryFcitxEngine(connection);
	ImeDump("listener", sEngine, false);
	return true;
}

void LinuxImeShutdown()
{
	CloseListener();
	sListenerAttempted = false;
	sLastAttemptTick = 0;
}

void LinuxImeDispatch()
{
	if (!sImeBus)
	{
		if (LinuxCaptureActive())
			LinuxImeStartListener();
		return;
	}
	if (!dbus_connection_read_write(sImeBus, 0))
	{
		CloseListener();
		return;
	}
	while (DBusMessage *message = dbus_connection_pop_message(sImeBus))
	{
		if (sListenerFramework == LINUX_IME_IBUS)
			HandleIbusMessage(message);
		else if (sListenerFramework == LINUX_IME_FCITX5)
			HandleFcitxMessage(message);
		dbus_message_unref(message);
	}
}

const char *LinuxImeEngine()
{
	return sEngine.c_str();
}

bool LinuxImePreeditActive()
{
	return sPreeditActive || LinuxCaptureImePreeditActive();
}

bool LinuxImeListening()
{
	return sImeBus != nullptr;
}

bool LinuxImeCommitCaptureActive()
{
	if (!sImeBus || sListenerFramework == LINUX_IME_NONE)
		return false;
	if (sListenerFramework == LINUX_IME_IBUS)
	{
		if (!sFocusTrackingSeen || sFocusedContextPath.empty())
			return false;
		if (sEngine.compare(0, 4, "xkb:") == 0)
			return false;
	}
	if (sListenerFramework == LINUX_IME_FCITX5
		&& (sEngine.compare(0, 9, "keyboard-") == 0 || sEngine == "keyboard"))
		return false;
	return true;
}

const char *LinuxImeListenerScope()
{
	if (sImeBus)
		return "eavesdrop";
	return LinuxImeFramework() == LINUX_IME_NONE ? "none" : "state-only";
}

unsigned long LinuxImeCommitCount()
{
	return sCommitCount;
}

unsigned long LinuxImePreeditCount()
{
	return sPreeditCount;
}

const char *LinuxImeLastCommit()
{
	return sLastCommit.c_str();
}
