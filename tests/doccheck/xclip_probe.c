/* xclip_probe: independent X11 CLIPBOARD client for the doc-check
 * clipboard regression suite (check0820 §2):
 *
 * Modes:
 *   --targets
 *       Ask the current CLIPBOARD owner for its TARGETS list and print
 *       them, one per line, then exit.  Empty (no owner) prints nothing.
 *   --get [DELAY_MS]
 *       Ask the owner for UTF8_STRING and print the received text to
 *       stdout.  With DELAY_MS (default 0), sleeps that long BEFORE
 *       requesting, simulating a slow consumer (the owner must keep
 *       serving until the data is pulled, not just until the write call
 *       returns).
 *   --set [DELAY_MS]
 *       Take ownership with the literal "probe-owner-data" text and keep
 *       serving SelectionRequest events for DELAY_MS (default 5000), so
 *       an AHK reader can observe a slow/external owner.  Prints
 *       "probe-owner-ready" once ownership is taken.
 *   --ask-target TARGET_NAME
 *       Request the given target (e.g. image/png) and print what the
 *       owner replies: "ok" if data was delivered, "none" if refused.
 *
 *   -out FILE  write all output to FILE instead of stdout.
 *
 * Shared by: assert_clipboard.ahk (AHK writes/reads, probe reads from a
 * foreign owner and vice versa) under Xvfb (run_check.sh --xvfb).
 */
#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static Display *g_d;
static Window g_win;
static Atom g_clip, g_prop, g_utf8, g_targets;
static int g_done = 0;
static char g_recv[1 << 20]; /* 1 MiB receive buffer (large-text test) */
static size_t g_recv_len = 0;
static int g_recv_ok = 0;
static FILE *g_out = NULL;

static void out(const char *s)
{
    fputs(s, g_out ? g_out : stdout);
    fflush(g_out ? g_out : stdout);
}

static void ensure_atoms(void)
{
    g_clip  = XInternAtom(g_d, "CLIPBOARD", False);
    g_prop  = XInternAtom(g_d, "AHK_PROBE_CLIP", False);
    g_utf8  = XInternAtom(g_d, "UTF8_STRING", False);
    g_targets = XInternAtom(g_d, "TARGETS", False);
}

static void serve_request(XSelectionRequestEvent *req)
{
    XSelectionEvent sel;
    sel.type = SelectionNotify;
    sel.display = req->display;
    sel.requestor = req->requestor;
    sel.selection = req->selection;
    sel.target = req->target;
    sel.time = req->time;
    sel.property = req->property;
    if (req->target == XA_STRING || req->target == g_utf8)
    {
        const char *data = "probe-owner-data";
        XChangeProperty(g_d, req->requestor, req->property, req->target, 8,
                        PropModeReplace, (unsigned char *)data, (int)strlen(data));
    }
    else
    {
        sel.property = None; /* refuse unknown targets */
    }
    XSendEvent(g_d, req->requestor, False, 0, (XEvent *)&sel);
    XFlush(g_d);
}

static void handle_event(XEvent *ev)
{
    if (ev->type == SelectionNotify && ev->xselection.requestor == g_win)
    {
        g_done = 1;
        if (ev->xselection.property == None)
            return; /* owner refused */
        Atom type; int format; unsigned long nitems, after;
        unsigned char *data = NULL;
        if (XGetWindowProperty(g_d, g_win, g_prop, 0, 0x7fffffffL, True,
                               AnyPropertyType, &type, &format, &nitems,
                               &after, &data) == Success && data)
        {
            /* Xlib returns format=8 data as bytes; format=32 data as an
             * array of unsigned long (each 8 bytes on LP64) with the value
             * in the low 4 bytes.  Keep the byte length so both cases can
             * be decoded. */
            size_t n = (format == 8) ? nitems : nitems * sizeof(unsigned long);
            if (n > sizeof(g_recv)) n = sizeof(g_recv);
            memcpy(g_recv, data, n);
            g_recv_len = n;
            g_recv_ok = 1;
            XFree(data);
        }
    }
    else if (ev->type == SelectionClear)
    {
        /* We lost the selection; nothing to do. */
    }
}

static void loop_until(int ms, int serve)
{
    time_t deadline = time(NULL) + ms / 1000;
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
        if (ms > 0 && time(NULL) > deadline + 2)
            break;
        usleep(10000);
        polled += 10;
    }
}

static void request_target(Atom target)
{
    XConvertSelection(g_d, g_clip, target, g_prop, g_win, CurrentTime);
    XFlush(g_d);
    g_done = 0;
    loop_until(5000, 1);
}

static void print_targets(void)
{
    request_target(g_targets);
    if (!g_recv_ok)
        return; /* no owner or refusal */
    /* format=32 reply: array of unsigned long (8 bytes each on LP64),
     * values in the low 4 bytes */
    if (g_recv_len % sizeof(unsigned long) != 0)
        return;
    unsigned long *atoms = (unsigned long *)g_recv;
    size_t n = g_recv_len / sizeof(unsigned long);
    for (size_t i = 0; i < n; ++i)
    {
        char *nm = XGetAtomName(g_d, (Atom)(atoms[i] & 0xFFFFFFFFUL));
        char line[128];
        snprintf(line, sizeof(line), "%s\n", nm ? nm : "?");
        out(line);
        if (nm) XFree(nm);
    }
}

int main(int argc, char **argv)
{
    int delay_ms = 0;
    const char *mode = NULL;
    const char *target_name = NULL;
    const char *outfile = NULL;
    for (int i = 1; i < argc; ++i)
    {
        if (!strcmp(argv[i], "--targets") || !strcmp(argv[i], "--get")
            || !strcmp(argv[i], "--set") || !strcmp(argv[i], "--ask-target"))
            mode = argv[i];
        else if (!strcmp(argv[i], "--delay"))
        {
            if (i + 1 < argc) delay_ms = atoi(argv[++i]);
        }
        else if (!strcmp(argv[i], "-out") && i + 1 < argc)
            outfile = argv[++i];
        else if (mode && !strcmp(mode, "--ask-target") && !target_name)
            target_name = argv[i];
    }
    if (!mode || (!strcmp(mode, "--ask-target") && !target_name))
    {
        fprintf(stderr, "usage: xclip_probe {--targets|--get [--delay MS]|--set [--delay MS]|--ask-target NAME} [-out FILE]\n");
        return 2;
    }
    if (outfile)
    {
        g_out = fopen(outfile, "w");
        if (!g_out)
            g_out = stdout;
    }

    g_d = XOpenDisplay(NULL);
    if (!g_d)
    {
        fprintf(stderr, "xclip_probe: no display\n");
        return 1;
    }
    g_win = XCreateSimpleWindow(g_d, DefaultRootWindow(g_d), -10, -10, 1, 1,
                                0, 0, 0);
    ensure_atoms();

    if (!strcmp(mode, "--set"))
    {
        XSetSelectionOwner(g_d, g_clip, g_win, CurrentTime);
        XFlush(g_d);
        out("probe-owner-ready\n");
        time_t end = time(NULL) + delay_ms / 1000 + 1;
        while (time(NULL) < end)
        {
            while (XPending(g_d) > 0)
            {
                XEvent ev;
                XNextEvent(g_d, &ev);
                if (ev.type == SelectionRequest)
                    serve_request(&ev.xselectionrequest);
            }
            usleep(10000);
        }
        XCloseDisplay(g_d);
        return 0;
    }

    /* reader modes */
    if (delay_ms > 0)
        usleep((useconds_t)delay_ms * 1000);
    if (!strcmp(mode, "--targets"))
        print_targets();
    else if (!strcmp(mode, "--get"))
    {
        request_target(g_utf8);
        if (g_recv_ok)
        {
            fwrite(g_recv, 1, g_recv_len, g_out ? g_out : stdout);
            out("\n");
        }
    }
    else if (!strcmp(mode, "--ask-target"))
    {
        Atom t = XInternAtom(g_d, target_name, False);
        request_target(t);
        out(g_recv_ok ? "ok\n" : "none\n");
    }
    if (g_out && g_out != stdout)
        fclose(g_out);
    XCloseDisplay(g_d);
    return 0;
}