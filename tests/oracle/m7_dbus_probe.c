// M7 D-Bus recovery fault service (check_detail0901 §10.5).
// Owns org.freedesktop.AhkM7Probe and answers Echo according to the mode
// given via a file: silent (no reply), delay N ms, crash (reply then exit),
// echo.  The AHK COM layer must stay bounded and keep the runtime alive.
#include <dbus/dbus.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static volatile sig_atomic_t g_stop;
static const char *g_ready;
static const char *g_log;
static char g_mode[64];

static void on_signal(int sig) { if (sig == SIGTERM || sig == SIGINT) g_stop = 1; }

static void log_line(const char *kind, const char *value)
{
	FILE *f = fopen(g_log, "a");
	if (!f) return;
	fprintf(f, "%s%s%s\n", kind, value ? "=" : "", value ? value : "");
	fclose(f);
}

static void read_mode(void)
{
	FILE *f = fopen("/tmp/ahk_m7_mode", "r");
	g_mode[0] = '\0';
	if (f)
	{
		char buf[64];
		if (fgets(buf, sizeof(buf), f))
		{
			size_t n = strlen(buf);
			while (n && (buf[n-1] == '\n' || buf[n-1] == '\r')) buf[--n] = 0;
			snprintf(g_mode, sizeof(g_mode), "%s", buf);
		}
		fclose(f);
	}
}

static void handle_call(DBusConnection *bus, DBusMessage *call)
{
	if (!dbus_message_is_method_call(call, "org.freedesktop.AhkM7Probe", "Echo"))
	{
		if (dbus_message_is_method_call(call, "org.freedesktop.DBus.Properties", "Get"))
		{
			DBusMessage *reply = dbus_message_new_method_return(call);
			DBusMessageIter it, variant;
			dbus_uint32_t version = 1;
			dbus_message_iter_init_append(reply, &it);
			dbus_message_iter_open_container(&it, DBUS_TYPE_VARIANT, "u", &variant);
			dbus_message_iter_append_basic(&variant, DBUS_TYPE_UINT32, &version);
			dbus_message_iter_close_container(&it, &variant);
			dbus_connection_send(bus, reply, NULL);
			dbus_message_unref(reply);
			dbus_connection_flush(bus);
		}
		return;
	}
	read_mode();
	const char *text = NULL;
	DBusMessageIter it;
	if (dbus_message_iter_init(call, &it) &&
		dbus_message_iter_get_arg_type(&it) == DBUS_TYPE_STRING)
		dbus_message_iter_get_basic(&it, &text);
	log_line("echo", text);
	if (!strcmp(g_mode, "silent"))
		return; // Never reply: the client must time out, bounded.
	if (!strncmp(g_mode, "delay", 5))
	{
		int ms = atoi(g_mode + 5);
		if (ms > 0 && ms < 10000)
			usleep((useconds_t)ms * 1000);
	}
	DBusMessage *reply = dbus_message_new_method_return(call);
	const char *resp = text ? text : "";
	dbus_message_append_args(reply, DBUS_TYPE_STRING, &resp, DBUS_TYPE_INVALID);
	dbus_connection_send(bus, reply, NULL);
	dbus_message_unref(reply);
	dbus_connection_flush(bus);
	log_line("replied", resp);
	if (!strcmp(g_mode, "crash"))
	{
		log_line("exit", NULL);
		_exit(0); // Reply then die: NameOwnerChanged must follow.
	}
}

int main(int argc, char **argv)
{
	if (argc != 3)
	{
		fprintf(stderr, "usage: %s READY LOG\n", argv[0]);
		return 2;
	}
	g_ready = argv[1];
	g_log = argv[2];
	signal(SIGTERM, on_signal);
	signal(SIGINT, on_signal);
	DBusError error;
	dbus_error_init(&error);
	DBusConnection *bus = dbus_bus_get(DBUS_BUS_SESSION, &error);
	if (!bus) return 3;
	int result = dbus_bus_request_name(bus, "org.freedesktop.AhkM7Probe",
		DBUS_NAME_FLAG_DO_NOT_QUEUE, &error);
	if (result != DBUS_REQUEST_NAME_REPLY_PRIMARY_OWNER) return 4;
	FILE *ready = fopen(g_ready, "w");
	if (ready) { fprintf(ready, "%ld\n", (long)getpid()); fclose(ready); }
	log_line("ready", NULL);
	time_t deadline = time(NULL) + 120;
	while (!g_stop && time(NULL) < deadline)
	{
		dbus_connection_read_write(bus, 50);
		DBusMessage *message;
		while ((message = dbus_connection_pop_message(bus)))
		{
			handle_call(bus, message);
			dbus_message_unref(message);
		}
	}
	unlink(g_ready);
	return 0;
}
