// Shared bounded D-Bus request/reply helper for Linux backends.
#pragma once

#include <dbus/dbus.h>

// Sends a method call asynchronously, pumps the connection in short slices,
// and cancels the local pending call on deadline/disconnect.  The caller owns
// aMessage and must unref it after this function returns.
DBusMessage *LinuxDbusPendingReply(DBusConnection *aConnection,
	DBusMessage *aMessage, int aTimeoutMs, DBusError *aError);
