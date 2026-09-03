// Minimal independent Fcitx5 D-Bus protocol producer for CI.
// Modes:
//   fcitx5_signal_probe READY [CORPUS]
//     Without CORPUS: one 你好 commit on SIGUSR1 (original behavior).
//     With CORPUS: each SIGUSR1 sends the next commit from the corpus
//     file (one entry per line, C-style backslash escapes decoded:
//     \xHH bytes, \n, \t, \\).  The final line may be tagged with a
//     leading "!" meaning intentionally-invalid UTF-8 bytes (sent raw;
//     libdbus may refuse the send — the oracle handles both outcomes).
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

static const char *g_ready;
static const char *g_corpus;
static char g_lines[64][512];
static int g_line_len[64];
static int g_line_invalid[64];
static int g_line_count = 0;
static int g_line_next = 0;

static int hexval(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

/* Minimal UTF-8 validity check (producer-side guard).  libdbus aborts the
 * process when marshaling a malformed string on some builds, so the producer
 * must pre-validate before constructing the message. */
static int is_valid_utf8(const unsigned char *s, int len)
{
    for (int i = 0; i < len;)
    {
        unsigned char c = s[i];
        if (c < 0x80) { i++; continue; }
        int n; unsigned int cp;
        if ((c & 0xE0) == 0xC0) { n = 1; cp = c & 0x1F; }
        else if ((c & 0xF0) == 0xE0) { n = 2; cp = c & 0x0F; }
        else if ((c & 0xF8) == 0xF0) { n = 3; cp = c & 0x07; }
        else return 0;
        if (i + n >= len) return 0;
        for (int k = 1; k <= n; ++k)
            if ((s[i + k] & 0xC0) != 0x80) return 0;
        cp = (cp << (6 * n));
        for (int k = 1; k <= n; ++k)
            cp |= (unsigned int)(s[i + k] & 0x3F) << (6 * (n - k));
        if (n == 1 && cp < 0x80) return 0;
        if (n == 2 && cp < 0x800) return 0;
        if (n == 3 && cp < 0x10000) return 0;
        if (cp > 0x10FFFF) return 0;
        if (cp >= 0xD800 && cp <= 0xDFFF) return 0;
        i += n + 1;
    }
    return 1;
}

/* Loads the whole corpus into memory once so the send loop cannot be
 * perturbed by stdio state. */
/* Decodes one corpus line (without the trailing newline) into raw bytes.
 * Returns the byte count; *out_invalid is set when the line started with
 * '!' (intentionally invalid UTF-8). */
static int decode_line(const char *line, unsigned char *out, size_t cap, int *out_invalid)
{
    size_t n = strlen(line);
    size_t r = 0, w = 0;
    *out_invalid = 0;
    if (r < n && line[r] == '!') { *out_invalid = 1; r++; }
    while (r < n && w + 4 < cap)
    {
        char c = line[r++];
        if (c == '\\' && r < n)
        {
            char e = line[r++];
            if (e == 'x' && r + 1 < n)
            {
                int hi = hexval(line[r]), lo = hexval(line[r + 1]);
                if (hi >= 0 && lo >= 0) { out[w++] = (unsigned char)(hi * 16 + lo); r += 2; continue; }
                out[w++] = (unsigned char)e;
            }
            else if (e == 'n') out[w++] = '\n';
            else if (e == 't') out[w++] = '\t';
            else if (e == '\\') out[w++] = '\\';
            else out[w++] = (unsigned char)e;
        }
        else
            out[w++] = (unsigned char)c;
    }
    return (int)w;
}

static void load_corpus(void)
{
    if (!g_corpus) return;
    FILE *f = fopen(g_corpus, "r");
    if (!f) return;
    char line[512];
    while (g_line_count < 64 && fgets(line, sizeof(line), f))
    {
        size_t n = strlen(line);
        while (n && (line[n - 1] == '\n' || line[n - 1] == '\r')) line[--n] = 0;
        if (!n) continue;
        int invalid = 0;
        int len = decode_line(line,
            (unsigned char *)g_lines[g_line_count],
            sizeof(g_lines[g_line_count]), &invalid);
        g_line_len[g_line_count] = len;
        g_line_invalid[g_line_count] = invalid;
        g_line_count++;
    }
    fclose(f);
}

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

/* Sends one commit.  Returns 1 sent, 0 refused (invalid UTF-8), -1 internal
 * error. */
static int send_commit(DBusConnection *bus, const unsigned char *bytes, int len,
    int invalid)
{
    DBusMessage *msg = dbus_message_new_signal("/inputcontext_1",
        "org.fcitx.Fcitx.InputContext1", "CommitString");
    /* libdbus requires NUL-terminated strings; copy and terminate. */
    char buf[512];
    if (len > (int)sizeof(buf) - 1) len = (int)sizeof(buf) - 1;
    memcpy(buf, bytes, (size_t)len);
    buf[len] = '\0';
    const char *text = buf;
    if (invalid || !is_valid_utf8((const unsigned char *)buf, len))
    {
        dbus_message_unref(msg);
        return 0; /* libdbus would abort marshaling this string. */
    }
    if (!dbus_message_append_args(msg, DBUS_TYPE_STRING, &text, DBUS_TYPE_INVALID))
    {
        dbus_message_unref(msg);
        return 0; /* libdbus refused the invalid UTF-8 string. */
    }
    dbus_bool_t ok = dbus_connection_send(bus, msg, NULL);
    dbus_message_unref(msg);
    if (!ok) return -1;
    dbus_connection_flush(bus);
    /* libdbus may accept the append of a malformed string but fail the
     * connection during marshal/flush; surface that as a refusal so the
     * oracle can distinguish "libdbus defended" from "delivered". */
    if (!dbus_connection_get_is_connected(bus)) return 0;
    return 1;
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

static void emit_next(DBusConnection *bus, FILE *log)
{
    if (g_line_next >= g_line_count)
    {
        fprintf(log, "corpus-exhausted sent=%d\n", g_line_next);
        fflush(log);
        return;
    }
    int idx = g_line_next++;
    int invalid = g_line_invalid[idx];
    int len = g_line_len[idx];
    send_preedit(bus, invalid ? "" : "corpus");
    int r = send_commit(bus, (const unsigned char *)g_lines[idx], len, invalid);
    send_preedit(bus, "");
    dbus_connection_flush(bus);
    fprintf(log, "corpus-commit idx=%d idx-sent=%d invalid=%d bytes=%d\n",
        idx, r, invalid, len);
    fflush(log);
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
    g_ready = argc > 1 ? argv[1] : "/tmp/fcitx5-probe-ready";
    g_corpus = argc > 2 ? argv[2] : NULL;
    const char *log_path = argc > 3 ? argv[3] : "/tmp/fcitx5-probe.log";
    FILE *log = fopen(log_path, "a");
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
    FILE *file = fopen(g_ready, "w");
    if (file) { fprintf(file, "%ld\n", (long)getpid()); fclose(file); }
    if (g_corpus)
        load_corpus();
    time_t deadline = time(NULL) + 60;
    int last_sent_second = -1;
    while (!g_stop && time(NULL) < deadline) {
        dbus_connection_read_write(bus, 50);
        DBusMessage *message;
        while ((message = dbus_connection_pop_message(bus))) {
            handle_call(bus, message);
            dbus_message_unref(message);
        }
        if (g_corpus)
        {
            /* corpus mode: auto-send one entry per second. */
            int sec = (int)time(NULL);
            if (sec != last_sent_second)
            {
                last_sent_second = sec;
                emit_next(bus, log);
            }
        }
        else if (g_emit)
        {
            g_emit = 0;
            emit_commit(bus);
        }
    }
    if (log) fclose(log);
    dbus_bus_release_name(bus, "org.fcitx.Fcitx5", NULL);
    dbus_connection_unref(bus);
    unlink(g_ready);
    return 0;
}
