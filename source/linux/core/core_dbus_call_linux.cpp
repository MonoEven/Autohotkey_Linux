// Shared bounded D-Bus request/reply helper.
#include "../../stdafx.h"
#include "../../application.h"
#include "core_dbus_call_linux.h"
#include <ctime>

static uint64_t LinuxDbusNowUs()
{
	struct timespec ts;
	if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0)
		return 0;
	return (uint64_t)ts.tv_sec * 1000000ULL + (uint64_t)ts.tv_nsec / 1000ULL;
}

DBusMessage *LinuxDbusPendingReply(DBusConnection *aConnection,
	DBusMessage *aMessage, int aTimeoutMs, DBusError *aError)
{
	if (!aConnection || !aMessage || aTimeoutMs <= 0)
	{
		if (aError)
			dbus_set_error(aError, DBUS_ERROR_NO_REPLY,
				"D-Bus call budget expired");
		return nullptr;
	}
	DBusPendingCall *pending = nullptr;
	if (!dbus_connection_send_with_reply(aConnection, aMessage, &pending,
		aTimeoutMs) || !pending)
	{
		if (aError)
			dbus_set_error(aError, DBUS_ERROR_NO_MEMORY,
				"Unable to allocate D-Bus pending call");
		return nullptr;
	}
	// The pending-call API takes its own reference; callers retain their
	// original message reference and release it after this helper returns.
	dbus_connection_flush(aConnection);
	uint64_t deadline = LinuxDbusNowUs() + (uint64_t)aTimeoutMs * 1000ULL;
	for (;;)
	{
		uint64_t now = LinuxDbusNowUs();
		if (dbus_pending_call_get_completed(pending))
		{
			DBusMessage *reply = dbus_pending_call_steal_reply(pending);
			dbus_pending_call_unref(pending);
			return reply;
		}
		if (now && now >= deadline)
		{
			dbus_pending_call_cancel(pending);
			dbus_pending_call_unref(pending);
			if (aError)
				dbus_set_error(aError, DBUS_ERROR_NO_REPLY,
					"D-Bus call timed out after %d ms", aTimeoutMs);
			return nullptr;
		}
		int remain_ms = now && deadline > now
			? (int)((deadline - now + 999ULL) / 1000ULL) : 10;
		int slice_ms = remain_ms > 10 ? 10 : remain_ms;
		if (slice_ms < 1)
			slice_ms = 1;
		if (!dbus_connection_read_write_dispatch(aConnection, slice_ms))
		{
			dbus_pending_call_cancel(pending);
			dbus_pending_call_unref(pending);
			if (aError)
				dbus_set_error(aError, DBUS_ERROR_DISCONNECTED,
					"D-Bus connection disconnected");
			return nullptr;
		}
		// Keep script timers and queued callbacks moving while a backend waits.
		MsgSleep(0, RETURN_AFTER_MESSAGES_SPECIAL_FILTER);
	}
}
