// Independent xdg-desktop-portal GlobalShortcuts protocol probe.
// Owns org.freedesktop.portal.Desktop, accepts one CreateSession/BindShortcuts
// sequence and emits Activated on SIGUSR1. Used twice to prove runtime rebind.
#include <dbus/dbus.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static volatile sig_atomic_t g_activate;
static volatile sig_atomic_t g_stop;
static const char *g_ready;
static const char *g_bound;
static const char *g_log;
static char g_shortcut[128];
static char g_client[128];
static const char *g_session = "/org/freedesktop/portal/desktop/session/fake/ahk_gs1";
static int g_request_id;

static void on_signal(int sig) { if (sig == SIGUSR1) g_activate = 1; else g_stop = 1; }

static void log_line(const char *kind, const char *value)
{
    FILE *f = fopen(g_log, "a");
    if (!f) return;
    fprintf(f, "%s%s%s\n", kind, value ? "=" : "", value ? value : "");
    fclose(f);
}

static void append_empty_dict(DBusMessageIter *it)
{
    DBusMessageIter array;
    dbus_message_iter_open_container(it, DBUS_TYPE_ARRAY, "{sv}", &array);
    dbus_message_iter_close_container(it, &array);
}

static void send_response(DBusConnection *bus, const char *path, int create)
{
    // Delay until the client has consumed the method reply and installed its
    // path-specific Request.Response match rule.
    usleep(150000);
    DBusMessage *signal = dbus_message_new_signal(path,
        "org.freedesktop.portal.Request", "Response");
    DBusMessageIter it, array, entry, variant;
    if (g_client[0]) dbus_message_set_destination(signal, g_client);
    dbus_uint32_t code = 0;
    dbus_message_iter_init_append(signal, &it);
    dbus_message_iter_append_basic(&it, DBUS_TYPE_UINT32, &code);
    dbus_message_iter_open_container(&it, DBUS_TYPE_ARRAY, "{sv}", &array);
    if (create) {
        const char *key = "session_handle";
        dbus_message_iter_open_container(&array, DBUS_TYPE_DICT_ENTRY, NULL, &entry);
        dbus_message_iter_append_basic(&entry, DBUS_TYPE_STRING, &key);
        dbus_message_iter_open_container(&entry, DBUS_TYPE_VARIANT, "o", &variant);
        dbus_message_iter_append_basic(&variant, DBUS_TYPE_OBJECT_PATH, &g_session);
        dbus_message_iter_close_container(&entry, &variant);
        dbus_message_iter_close_container(&array, &entry);
    }
    dbus_message_iter_close_container(&it, &array);
    dbus_connection_send(bus, signal, NULL);
    dbus_message_unref(signal);
    dbus_connection_flush(bus);
}

static void extract_shortcut_id(DBusMessage *call)
{
    DBusMessageIter it, array, item;
    if (!dbus_message_iter_init(call, &it)) return;
    if (!dbus_message_iter_next(&it) || dbus_message_iter_get_arg_type(&it) != DBUS_TYPE_ARRAY)
        return;
    dbus_message_iter_recurse(&it, &array);
    if (dbus_message_iter_get_arg_type(&array) != DBUS_TYPE_STRUCT) return;
    dbus_message_iter_recurse(&array, &item);
    if (dbus_message_iter_get_arg_type(&item) != DBUS_TYPE_STRING) return;
    const char *id = NULL;
    dbus_message_iter_get_basic(&item, &id);
    if (id) snprintf(g_shortcut, sizeof(g_shortcut), "%s", id);
}

static void method_reply_then_response(DBusConnection *bus, DBusMessage *call, int create)
{
    char request[256];
    snprintf(request, sizeof(request),
        "/org/freedesktop/portal/desktop/request/fake/%s_%d",
        create ? "create" : "bind", ++g_request_id);
    DBusMessage *reply = dbus_message_new_method_return(call);
    const char *request_ptr = request;
    dbus_message_append_args(reply, DBUS_TYPE_OBJECT_PATH, &request_ptr, DBUS_TYPE_INVALID);
    dbus_connection_send(bus, reply, NULL);
    dbus_message_unref(reply);
    dbus_connection_flush(bus);
    send_response(bus, request, create);
}

static void handle_call(DBusConnection *bus, DBusMessage *call)
{
    if (dbus_message_is_method_call(call,
        "org.freedesktop.portal.GlobalShortcuts", "CreateSession")) {
        const char *sender = dbus_message_get_sender(call);
        if (sender) snprintf(g_client, sizeof(g_client), "%s", sender);
        log_line("create", NULL);
        method_reply_then_response(bus, call, 1);
        return;
    }
    if (dbus_message_is_method_call(call,
        "org.freedesktop.portal.GlobalShortcuts", "BindShortcuts")) {
        extract_shortcut_id(call);
        log_line("bind", g_shortcut);
        method_reply_then_response(bus, call, 0);
        FILE *f = fopen(g_bound, "w");
        if (f) { fprintf(f, "%s\n", g_shortcut); fclose(f); }
        return;
    }
    if (dbus_message_is_method_call(call,
        "org.freedesktop.DBus.Properties", "Get")) {
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
}

static void emit_activated(DBusConnection *bus)
{
    if (!g_shortcut[0]) return;
    DBusMessage *signal = dbus_message_new_signal(g_session,
        "org.freedesktop.portal.GlobalShortcuts", "Activated");
    DBusMessageIter it;
    if (g_client[0]) dbus_message_set_destination(signal, g_client);
    dbus_uint64_t timestamp = 1;
    dbus_message_iter_init_append(signal, &it);
    dbus_message_iter_append_basic(&it, DBUS_TYPE_OBJECT_PATH, &g_session);
    const char *id = g_shortcut;
    dbus_message_iter_append_basic(&it, DBUS_TYPE_STRING, &id);
    dbus_message_iter_append_basic(&it, DBUS_TYPE_UINT64, &timestamp);
    append_empty_dict(&it);
    dbus_connection_send(bus, signal, NULL);
    dbus_message_unref(signal);
    dbus_connection_flush(bus);
    log_line("activate", g_shortcut);
}

int main(int argc, char **argv)
{
    if (argc != 4) {
        fprintf(stderr, "usage: %s READY BOUND LOG\n", argv[0]);
        return 2;
    }
    g_ready = argv[1]; g_bound = argv[2]; g_log = argv[3];
    signal(SIGUSR1, on_signal);
    signal(SIGTERM, on_signal);
    signal(SIGINT, on_signal);
    DBusError error;
    dbus_error_init(&error);
    DBusConnection *bus = dbus_bus_get(DBUS_BUS_SESSION, &error);
    if (!bus) return 3;
    int result = dbus_bus_request_name(bus, "org.freedesktop.portal.Desktop",
        DBUS_NAME_FLAG_DO_NOT_QUEUE, &error);
    if (result != DBUS_REQUEST_NAME_REPLY_PRIMARY_OWNER) return 4;
    FILE *ready = fopen(g_ready, "w");
    if (ready) { fprintf(ready, "%ld\n", (long)getpid()); fclose(ready); }
    log_line("ready", NULL);
    time_t deadline = time(NULL) + 45;
    while (!g_stop && time(NULL) < deadline) {
        dbus_connection_read_write(bus, 50);
        DBusMessage *message;
        while ((message = dbus_connection_pop_message(bus))) {
            handle_call(bus, message);
            dbus_message_unref(message);
        }
        if (g_activate) { g_activate = 0; emit_activated(bus); }
    }
    dbus_bus_release_name(bus, "org.freedesktop.portal.Desktop", NULL);
    dbus_connection_unref(bus);
    unlink(g_ready);
    return 0;
}
