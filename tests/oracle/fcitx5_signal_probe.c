// Minimal independent Fcitx5 D-Bus protocol producer for CI.
#include <dbus/dbus.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static volatile sig_atomic_t g_emit = 0;
static volatile sig_atomic_t g_stop = 0;
static void on_signal(int sig) { if (sig == SIGUSR1) g_emit = 1; else g_stop = 1; }

static void send_preedit(DBusConnection *bus, const char *text)
{
    DBusMessage *msg = dbus_message_new_signal("/inputcontext_1",
        "org.fcitx.Fcitx.InputContext1", "UpdateFormattedPreedit");
    DBusMessageIter it, array, item;
    dbus_message_iter_init_append(msg, &it);
    dbus_message_iter_open_container(&it, DBUS_TYPE_ARRAY, "(si)", &array);
    if (text && *text) {
        dbus_int32_t format = 0;
        dbus_message_iter_open_container(&array, DBUS_TYPE_STRUCT, NULL, &item);
        dbus_message_iter_append_basic(&item, DBUS_TYPE_STRING, &text);
        dbus_message_iter_append_basic(&item, DBUS_TYPE_INT32, &format);
        dbus_message_iter_close_container(&array, &item);
    }
    dbus_message_iter_close_container(&it, &array);
    dbus_int32_t cursor = text ? (dbus_int32_t)strlen(text) : 0;
    dbus_message_iter_append_basic(&it, DBUS_TYPE_INT32, &cursor);
    dbus_connection_send(bus, msg, NULL);
    dbus_message_unref(msg);
}

static void emit_commit(DBusConnection *bus)
{
    send_preedit(bus, "nihao");
    DBusMessage *msg = dbus_message_new_signal("/inputcontext_1",
        "org.fcitx.Fcitx.InputContext1", "CommitString");
    const char *text = "你好";
    dbus_message_append_args(msg, DBUS_TYPE_STRING, &text, DBUS_TYPE_INVALID);
    dbus_connection_send(bus, msg, NULL);
    dbus_message_unref(msg);
    send_preedit(bus, "");
    dbus_connection_flush(bus);
}

static void handle_call(DBusConnection *bus, DBusMessage *call)
{
    if (!dbus_message_is_method_call(call, "org.fcitx.Fcitx.Controller1", "CurrentInputMethod"))
        return;
    DBusMessage *reply = dbus_message_new_method_return(call);
    const char *engine = "pinyin";
    dbus_message_append_args(reply, DBUS_TYPE_STRING, &engine, DBUS_TYPE_INVALID);
    dbus_connection_send(bus, reply, NULL);
    dbus_message_unref(reply);
    dbus_connection_flush(bus);
}

int main(int argc, char **argv)
{
    const char *ready = argc > 1 ? argv[1] : "/tmp/fcitx5-probe-ready";
    signal(SIGUSR1, on_signal);
    signal(SIGTERM, on_signal);
    signal(SIGINT, on_signal);
    DBusError err;
    dbus_error_init(&err);
    DBusConnection *bus = dbus_bus_get(DBUS_BUS_SESSION, &err);
    if (!bus) return 2;
    int result = dbus_bus_request_name(bus, "org.fcitx.Fcitx5",
        DBUS_NAME_FLAG_DO_NOT_QUEUE, &err);
    if (result != DBUS_REQUEST_NAME_REPLY_PRIMARY_OWNER) return 3;
    FILE *file = fopen(ready, "w");
    if (file) { fprintf(file, "%ld\n", (long)getpid()); fclose(file); }
    time_t deadline = time(NULL) + 30;
    while (!g_stop && time(NULL) < deadline) {
        dbus_connection_read_write(bus, 50);
        DBusMessage *message;
        while ((message = dbus_connection_pop_message(bus))) {
            handle_call(bus, message);
            dbus_message_unref(message);
        }
        if (g_emit) { g_emit = 0; emit_commit(bus); }
    }
    dbus_bus_release_name(bus, "org.fcitx.Fcitx5", NULL);
    dbus_connection_unref(bus);
    unlink(ready);
    return 0;
}
