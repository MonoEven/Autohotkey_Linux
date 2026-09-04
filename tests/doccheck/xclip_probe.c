/* xclip_probe: independent X11 CLIPBOARD client for the doc-check
 * clipboard regression suite (check0820 搂2):
 *
 * Modes:
 *   --targets
 *       Ask the current CLIPBOARD owner for its TARGETS list and print
 *       them, one per line, then exit.  Empty (no owner) prints nothing.
 *   --get [--delay MS]
 *       Ask the owner for UTF8_STRING and print the received text to
 *       stdout.  The optional delay happens before the request.
 *   --set [--delay MS]
 *       Take ownership with the literal "probe-owner-data" text and keep
 *       serving SelectionRequest events for MS (default 5000), so an AHK
 *       reader can observe a slow/external owner.
 *   --set-mime --mime TARGET=HEX [--mime TARGET=HEX ...] [--delay MS]
 *       Take ownership as a byte-exact multi-MIME source.  TARGETS includes
 *       each configured MIME plus TARGETS, STRING and UTF8_STRING.
 *   --ask-target TARGET_NAME
 *       Request the given target and print "ok" or "none".
 *   --get-target TARGET_NAME
 *       Request the given target and print its bytes as uppercase hex.
 *
 *   Owner modes print "probe-owner-ready" once ownership is taken.
 *   -out FILE writes all output to FILE instead of stdout.
 *
 * Shared by the clipboard doc-checks as a process-independent X11 oracle.
 */
#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static Display *g_d;
static Window g_win;
static Atom g_clip, g_prop, g_utf8, g_targets;
static int g_done = 0;
static char g_recv[1 << 20]; /* 1 MiB receive buffer (large-text test). */
static size_t g_recv_len = 0;
static int g_recv_format = 0;
static int g_recv_ok = 0;
static FILE *g_out = NULL;

/* Names and bytes are parsed before XOpenDisplay.  Atoms are interned only
 * after the display is available. */
#define MULTI_MAX 16
#define MULTI_NAME_MAX 128
#define MULTI_DATA_MAX 4096
struct MultiRep
{
    char name[MULTI_NAME_MAX];
    Atom atom;
    unsigned char *data;
    size_t len;
};
static struct MultiRep g_multi[MULTI_MAX];
static int g_multi_count = 0;
static int g_multi_owner = 0;

static void out(const char *s)
{
    fputs(s, g_out ? g_out : stdout);
    fflush(g_out ? g_out : stdout);
}

static void ensure_atoms(void)
{
    g_clip = XInternAtom(g_d, "CLIPBOARD", False);
    g_prop = XInternAtom(g_d, "AHK_PROBE_CLIP", False);
    g_utf8 = XInternAtom(g_d, "UTF8_STRING", False);
    g_targets = XInternAtom(g_d, "TARGETS", False);
}

static int hex_nibble(unsigned char c)
{
    if (c >= '0' && c <= '9')
        return c - '0';
    c = (unsigned char)(c | 0x20);
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    return -1;
}

static int add_multi_rep(const char *spec)
{
    const char *eq;
    size_t name_len, hex_len, data_len;
    unsigned char *bytes;

    if (!spec || g_multi_count >= MULTI_MAX)
        return 0;
    eq = strchr(spec, '=');
    if (!eq || eq == spec)
        return 0;
    name_len = (size_t)(eq - spec);
    hex_len = strlen(eq + 1);
    if (name_len >= MULTI_NAME_MAX || (hex_len & 1) != 0
        || hex_len / 2 > MULTI_DATA_MAX)
        return 0;

    for (int i = 0; i < g_multi_count; ++i)
        if (strlen(g_multi[i].name) == name_len
            && !memcmp(g_multi[i].name, spec, name_len))
            return 0; /* Duplicate MIME names make TARGETS ambiguous. */

    data_len = hex_len / 2;
    bytes = (unsigned char *)malloc(data_len ? data_len : 1);
    if (!bytes)
        return 0;
    for (size_t i = 0; i < data_len; ++i)
    {
        int hi = hex_nibble((unsigned char)eq[1 + i * 2]);
        int lo = hex_nibble((unsigned char)eq[2 + i * 2]);
        if (hi < 0 || lo < 0)
        {
            free(bytes);
            return 0;
        }
        bytes[i] = (unsigned char)((hi << 4) | lo);
    }

    memcpy(g_multi[g_multi_count].name, spec, name_len);
    g_multi[g_multi_count].name[name_len] = '\0';
    g_multi[g_multi_count].atom = None;
    g_multi[g_multi_count].data = bytes;
    g_multi[g_multi_count].len = data_len;
    ++g_multi_count;
    return 1;
}

static void intern_multi_atoms(void)
{
    for (int i = 0; i < g_multi_count; ++i)
        g_multi[i].atom = XInternAtom(g_d, g_multi[i].name, False);
}

static const struct MultiRep *find_multi_atom(Atom atom)
{
    for (int i = 0; i < g_multi_count; ++i)
        if (g_multi[i].atom == atom)
            return &g_multi[i];
    return NULL;
}

static const struct MultiRep *find_multi_text(void)
{
    for (int i = 0; i < g_multi_count; ++i)
        if (!strcmp(g_multi[i].name, "text/plain;charset=utf-8"))
            return &g_multi[i];
    for (int i = 0; i < g_multi_count; ++i)
        if (!strcmp(g_multi[i].name, "text/plain"))
            return &g_multi[i];
    return NULL;
}

static void free_multi(void)
{
    for (int i = 0; i < g_multi_count; ++i)
        free(g_multi[i].data);
}

static void serve_request(XSelectionRequestEvent *req)
{
    XSelectionEvent sel;
    Atom property = req->property == None ? req->target : req->property;

    sel.type = SelectionNotify;
    sel.display = req->display;
    sel.requestor = req->requestor;
    sel.selection = req->selection;
    sel.target = req->target;
    sel.time = req->time;
    sel.property = property;

    if (req->target == g_targets)
    {
        Atom atoms[MULTI_MAX + 3];
        int count = 0;
        atoms[count++] = g_targets;
        atoms[count++] = XA_STRING;
        atoms[count++] = g_utf8;
        if (g_multi_owner)
        {
            for (int i = 0; i < g_multi_count; ++i)
            {
                int duplicate = 0;
                for (int j = 0; j < count; ++j)
                    if (atoms[j] == g_multi[i].atom)
                    {
                        duplicate = 1;
                        break;
                    }
                if (!duplicate)
                    atoms[count++] = g_multi[i].atom;
            }
        }
        XChangeProperty(g_d, req->requestor, property, XA_ATOM, 32,
                        PropModeReplace, (unsigned char *)atoms, count);
    }
    else if (req->target == XA_STRING || req->target == g_utf8)
    {
        const struct MultiRep *text = g_multi_owner ? find_multi_text() : NULL;
        const unsigned char *data = text ? text->data
            : (const unsigned char *)"probe-owner-data";
        size_t len = text ? text->len : strlen((const char *)data);
        XChangeProperty(g_d, req->requestor, property, req->target, 8,
                        PropModeReplace, data, (int)len);
    }
    else if (g_multi_owner)
    {
        const struct MultiRep *rep = find_multi_atom(req->target);
        if (rep)
            XChangeProperty(g_d, req->requestor, property, req->target, 8,
                            PropModeReplace, rep->data, (int)rep->len);
        else
            sel.property = None;
    }
    else
    {
        sel.property = None; /* Refuse unknown targets. */
    }
    XSendEvent(g_d, req->requestor, False, 0, (XEvent *)&sel);
    XFlush(g_d);
}

static void handle_event(XEvent *ev)
{
    if (ev->type == SelectionNotify && ev->xselection.requestor == g_win)
    {
        Atom type;
        int format;
        unsigned long nitems, after;
        unsigned char *data = NULL;

        g_done = 1;
        if (ev->xselection.property == None)
            return; /* Owner refused. */
        if (XGetWindowProperty(g_d, g_win, g_prop, 0, 0x7fffffffL, True,
                AnyPropertyType, &type, &format, &nitems, &after, &data)
            == Success && data)
        {
            /* Xlib expands format=32 values to unsigned long on LP64;
             * nitems remains an element count. */
            size_t n = format == 32 ? nitems * sizeof(unsigned long)
                : format == 16 ? nitems * 2 : nitems;
            if (n > sizeof(g_recv))
                n = sizeof(g_recv);
            memcpy(g_recv, data, n);
            g_recv_len = n;
            g_recv_format = format;
            g_recv_ok = 1;
            XFree(data);
        }
    }
}

static void loop_until(int ms, int serve)
{
    int polled = 0;
    while (!g_done && (ms <= 0 || polled < ms))
    {
        while (XPending(g_d) > 0)
        {
            XEvent ev;
            XNextEvent(g_d, &ev);
            if (ev.type == SelectionRequest && serve)
                serve_request(&ev.xselectionrequest);
            else
                handle_event(&ev);
        }
        usleep(10000);
        polled += 10;
    }
}

static void request_target(Atom target)
{
    g_done = 0;
    g_recv_len = 0;
    g_recv_format = 0;
    g_recv_ok = 0;
    XConvertSelection(g_d, g_clip, target, g_prop, g_win, CurrentTime);
    XFlush(g_d);
    loop_until(5000, 1);
}

static void print_targets(void)
{
    request_target(g_targets);
    if (!g_recv_ok || g_recv_format != 32
        || g_recv_len % sizeof(unsigned long) != 0)
        return; /* No owner, refusal, or malformed TARGETS response. */

    size_t count = g_recv_len / sizeof(unsigned long);
    for (size_t i = 0; i < count; ++i)
    {
        unsigned long atom_value;
        char *name;
        char line[128];
        memcpy(&atom_value, g_recv + i * sizeof(unsigned long),
               sizeof(atom_value));
        name = XGetAtomName(g_d, (Atom)(atom_value & 0xFFFFFFFFUL));
        snprintf(line, sizeof(line), "%s\n", name ? name : "?");
        out(line);
        if (name)
            XFree(name);
    }
}

static void print_target_hex(void)
{
    FILE *dest = g_out ? g_out : stdout;
    if (!g_recv_ok || g_recv_format != 8)
    {
        out("none\n");
        return;
    }
    for (size_t i = 0; i < g_recv_len; ++i)
        fprintf(dest, "%02X", (unsigned char)g_recv[i]);
    fputc('\n', dest);
    fflush(dest);
}

static void usage(void)
{
    fprintf(stderr,
        "usage: xclip_probe {--targets|--get [--delay MS]|"
        "--set [--delay MS] [--serve-delay MS]|"
        "--set-mime --mime T=HEX [--mime T=HEX ...] [--delay MS]|"
        "--ask-target NAME|--get-target NAME} [-out FILE]\n");
}

int main(int argc, char **argv)
{
    int delay_ms = 0;
    int delay_set = 0;
    int serve_delay_ms = 0;
    const char *mode = NULL;
    const char *target_name = NULL;
    const char *outfile = NULL;

    for (int i = 1; i < argc; ++i)
    {
        if (!strcmp(argv[i], "--targets") || !strcmp(argv[i], "--get")
            || !strcmp(argv[i], "--set") || !strcmp(argv[i], "--ask-target")
            || !strcmp(argv[i], "--get-target")
            || !strcmp(argv[i], "--set-mime"))
        {
            mode = argv[i];
        }
        else if (!strcmp(argv[i], "--delay") && i + 1 < argc)
        {
            delay_ms = atoi(argv[++i]);
            delay_set = 1;
        }
        else if (!strcmp(argv[i], "--serve-delay") && i + 1 < argc)
        {
            serve_delay_ms = atoi(argv[++i]);
        }
        else if (!strcmp(argv[i], "--mime") && i + 1 < argc)
        {
            const char *spec = argv[++i];
            if (!add_multi_rep(spec))
            {
                fprintf(stderr, "xclip_probe: invalid --mime value: %s\n", spec);
                free_multi();
                return 2;
            }
        }
        else if (!strcmp(argv[i], "-out") && i + 1 < argc)
        {
            outfile = argv[++i];
        }
        else if (mode && (!strcmp(mode, "--ask-target")
                 || !strcmp(mode, "--get-target")) && !target_name)
        {
            target_name = argv[i];
        }
        else
        {
            fprintf(stderr, "xclip_probe: unknown or incomplete argument: %s\n",
                    argv[i]);
            free_multi();
            return 2;
        }
    }

    if (!mode
        || ((!strcmp(mode, "--ask-target") || !strcmp(mode, "--get-target"))
            && !target_name)
        || (!strcmp(mode, "--set-mime") && g_multi_count == 0)
        || (strcmp(mode, "--set-mime") && g_multi_count != 0)
        || delay_ms < 0 || serve_delay_ms < 0)
    {
        usage();
        free_multi();
        return 2;
    }

    if (outfile)
    {
        g_out = fopen(outfile, "w");
        if (!g_out)
        {
            fprintf(stderr, "xclip_probe: cannot open output file: %s\n", outfile);
            free_multi();
            return 1;
        }
    }

    g_d = XOpenDisplay(NULL);
    if (!g_d)
    {
        fprintf(stderr, "xclip_probe: no display\n");
        if (g_out)
            fclose(g_out);
        free_multi();
        return 1;
    }
    g_win = XCreateSimpleWindow(g_d, DefaultRootWindow(g_d), -10, -10, 1, 1,
                                0, 0, 0);
    ensure_atoms();

    if (!strcmp(mode, "--set") || !strcmp(mode, "--set-mime"))
    {
        int owner_ms = delay_set ? delay_ms : 5000;
        int elapsed_ms = 0;
        if (!strcmp(mode, "--set-mime"))
        {
            g_multi_owner = 1;
            intern_multi_atoms();
        }
        XSetSelectionOwner(g_d, g_clip, g_win, CurrentTime);
        XSync(g_d, False);
        if (XGetSelectionOwner(g_d, g_clip) != g_win)
        {
            fprintf(stderr, "xclip_probe: failed to own CLIPBOARD\n");
            if (g_out)
                fclose(g_out);
            XCloseDisplay(g_d);
            free_multi();
            return 1;
        }

        out("probe-owner-ready\n");
        while (elapsed_ms < owner_ms)
        {
            while (XPending(g_d) > 0)
            {
                XEvent ev;
                XNextEvent(g_d, &ev);
                if (ev.type == SelectionRequest)
                {
                    if (serve_delay_ms > 0)
                        usleep((useconds_t)serve_delay_ms * 1000);
                    serve_request(&ev.xselectionrequest);
                }
                else
                {
                    handle_event(&ev);
                }
            }
            usleep(10000);
            elapsed_ms += 10;
        }

        if (g_out)
            fclose(g_out);
        XCloseDisplay(g_d);
        free_multi();
        return 0;
    }

    /* Reader modes. */
    if (delay_ms > 0)
        usleep((useconds_t)delay_ms * 1000);
    if (!strcmp(mode, "--targets"))
    {
        print_targets();
    }
    else if (!strcmp(mode, "--get"))
    {
        request_target(g_utf8);
        if (g_recv_ok && g_recv_format == 8)
        {
            fwrite(g_recv, 1, g_recv_len, g_out ? g_out : stdout);
            out("\n");
        }
    }
    else if (!strcmp(mode, "--ask-target")
             || !strcmp(mode, "--get-target"))
    {
        Atom target = XInternAtom(g_d, target_name, False);
        request_target(target);
        if (!strcmp(mode, "--ask-target"))
            out(g_recv_ok ? "ok\n" : "none\n");
        else
            print_target_hex();
    }

    if (g_out)
        fclose(g_out);
    XCloseDisplay(g_d);
    free_multi();
    return 0;
}
