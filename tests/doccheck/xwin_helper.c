/* xwin_helper: create an X11 test window (with optional child "control"
 * windows) for the doc-check Win* / Control* / Monitor* suites.
 *
 * Usage: xwin_helper -title T -class C -x X -y Y -w W -h H
 *                    [-child NAME CLASS CX CY CW CH]...   (repeatable)
 *                    [-fill RRGGBB X Y W H]...            (repeatable)
 *                    [-evout FILE] [-hidden] [-focus] [-ms N]
 *
 * Sets WM_NAME/_NET_WM_NAME, WM_CLASS, _NET_WM_PID on the main window and
 * each -child window (child windows get WM_NAME + WM_CLASS too).  Each
 * -fill paints a solid-color rectangle on the main window (for the
 * PixelGetColor/PixelSearch assertions).  Key and button events received by
 * any of the windows are logged to FILE (default /tmp/ahk_dc_ev.txt) as:
 *   k:down:<keysym>:win:<hwnd>   k:up:<keysym>:win:<hwnd>
 *   b:down:<button>:win:<hwnd>   b:up:<button>:win:<hwnd>
 * Exits on WM_DELETE_WINDOW, on DestroyNotify, after -ms ms, or when stdin
 * closes.
 *
 * Note: X errors are silenced (the runner may pkill us; the default Xlib
 * error handler would print "X connection ... broken" to stderr, which
 * pollutes the doc-check output files that share our stderr).
 */
#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static int xerr_handler(Display *d, XErrorEvent *e)
{
    return 0;
}

#define MAX_CHILDREN 32
#define MAX_FILLS 32

struct child_spec
{
    const char *name;
    const char *cls;
    int x, y, w, h;
};

struct fill_spec
{
    unsigned long pixel;
    int x, y, w, h;
};

static int g_running = 1;
static FILE *g_ev = NULL;

static long now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

static void log_key(const char *kind, const char *name, Window win)
{
    if (g_ev)
    {
        fprintf(g_ev, "%s:%s:win:%lu\n", kind, name, (unsigned long)win);
        fflush(g_ev);
    }
}

static void log_button(const char *kind, unsigned int btn, Window win)
{
    if (g_ev)
    {
        fprintf(g_ev, "%s:%u:win:%lu\n", kind, btn, (unsigned long)win);
        fflush(g_ev);
    }
}

int main(int argc, char **argv)
{
    const char *title = "DocCheck Window";
    const char *cls = "DocCheckClass";
    const char *evout = "/tmp/ahk_dc_ev.txt";
    int x = 100, y = 100, w = 400, h = 300;
    int hidden = 0, focus = 0;
    long ms = 600000;
    struct child_spec kids[MAX_CHILDREN];
    int nkids = 0;
    struct fill_spec fills[MAX_FILLS];
    int nfills = 0;

    for (int i = 1; i < argc; ++i)
    {
        if (!strcmp(argv[i], "-title") && i + 1 < argc) title = argv[++i];
        else if (!strcmp(argv[i], "-class") && i + 1 < argc) cls = argv[++i];
        else if (!strcmp(argv[i], "-x") && i + 1 < argc) x = atoi(argv[++i]);
        else if (!strcmp(argv[i], "-y") && i + 1 < argc) y = atoi(argv[++i]);
        else if (!strcmp(argv[i], "-w") && i + 1 < argc) w = atoi(argv[++i]);
        else if (!strcmp(argv[i], "-h") && i + 1 < argc) h = atoi(argv[++i]);
        else if (!strcmp(argv[i], "-ms") && i + 1 < argc) ms = atol(argv[++i]);
        else if (!strcmp(argv[i], "-evout") && i + 1 < argc) evout = argv[++i];
        else if (!strcmp(argv[i], "-hidden")) hidden = 1;
        else if (!strcmp(argv[i], "-focus")) focus = 1;
        else if (!strcmp(argv[i], "-child") && i + 5 < argc && nkids < MAX_CHILDREN)
        {
            struct child_spec *k = &kids[nkids++];
            k->name = argv[++i];
            k->cls = argv[++i];
            k->x = atoi(argv[++i]);
            k->y = atoi(argv[++i]);
            k->w = atoi(argv[++i]);
            k->h = atoi(argv[++i]);
        }
        else if (!strcmp(argv[i], "-fill") && i + 5 < argc && nfills < MAX_FILLS)
        {
            struct fill_spec *f = &fills[nfills++];
            f->pixel = strtoul(argv[++i], NULL, 16);
            f->x = atoi(argv[++i]);
            f->y = atoi(argv[++i]);
            f->w = atoi(argv[++i]);
            f->h = atoi(argv[++i]);
        }
    }

    Display *d = XOpenDisplay(NULL);
    if (!d)
    {
        fprintf(stderr, "xwin_helper: no display\n");
        return 1;
    }
    XSetErrorHandler(xerr_handler); // Silence "X connection broken" on pkill.
    int screen = DefaultScreen(d);
    Window root = RootWindow(d, screen);
    Window win = XCreateSimpleWindow(d, root, x, y, w, h, 0,
                                     BlackPixel(d, screen), WhitePixel(d, screen));

    XStoreName(d, win, title);
    Atom utf8 = XInternAtom(d, "UTF8_STRING", False);
    XChangeProperty(d, win, XInternAtom(d, "_NET_WM_NAME", False), utf8, 8,
                    PropModeReplace, (unsigned char *)title, (int)strlen(title));

    XClassHint hint;
    hint.res_name = (char *)cls;
    hint.res_class = (char *)cls;
    XSetClassHint(d, win, &hint);

    long pid = (long)getpid();
    XChangeProperty(d, win, XInternAtom(d, "_NET_WM_PID", False), XA_CARDINAL, 32,
                    PropModeReplace, (unsigned char *)&pid, 1);

    Atom wm_protocols = XInternAtom(d, "WM_PROTOCOLS", False);
    Atom wm_delete = XInternAtom(d, "WM_DELETE_WINDOW", False);
    XSetWMProtocols(d, win, &wm_delete, 1);
    (void)wm_protocols;

    XSelectInput(d, win, StructureNotifyMask | ExposureMask | KeyPressMask
                 | KeyReleaseMask | ButtonPressMask | ButtonReleaseMask);
    if (!hidden)
        XMapWindow(d, win);
    if (focus)
        XSetInputFocus(d, win, RevertToParent, CurrentTime);
    XFlush(d);

    // Child "control" windows.
    Window kids_win[MAX_CHILDREN];
    for (int i = 0; i < nkids; ++i)
    {
        struct child_spec *k = &kids[i];
        Window cw = XCreateSimpleWindow(d, win, k->x, k->y, k->w, k->h, 1,
                                        BlackPixel(d, screen), WhitePixel(d, screen));
        XStoreName(d, cw, k->name);
        XChangeProperty(d, cw, XInternAtom(d, "_NET_WM_NAME", False), utf8, 8,
                        PropModeReplace, (unsigned char *)k->name, (int)strlen(k->name));
        XClassHint ch;
        ch.res_name = (char *)k->cls;
        ch.res_class = (char *)k->cls;
        XSetClassHint(d, cw, &ch);
        XSelectInput(d, cw, KeyPressMask | KeyReleaseMask | ButtonPressMask
                     | ButtonReleaseMask | StructureNotifyMask);
        XMapWindow(d, cw);
        kids_win[i] = cw;
    }
    XFlush(d);

    // Solid-color rectangles on the main window (PixelGetColor/PixelSearch).
    // Re-painted on Expose/ConfigureNotify: a WM (sway) may resize the
    // window after mapping, and XWayland does not preserve buffer contents
    // across resizes.
    GC gc = XCreateGC(d, win, 0, NULL);
    (void)gc;
    for (int i = 0; i < nfills; ++i)
    {
        struct fill_spec *f = &fills[i];
        XColor color;
        color.red = (unsigned short)(((f->pixel >> 16) & 0xFF) * 0x101);
        color.green = (unsigned short)(((f->pixel >> 8) & 0xFF) * 0x101);
        color.blue = (unsigned short)((f->pixel & 0xFF) * 0x101);
        color.flags = DoRed | DoGreen | DoBlue;
        if (XAllocColor(d, DefaultColormap(d, screen), &color))
            f->pixel = color.pixel; // Remember the allocated pixel.
    }
    if (nfills)
        XFlush(d);

    g_ev = fopen(evout, "w");
    if (!g_ev)
        g_ev = NULL;

    long start = now_ms();
    while (g_running)
    {
        while (XPending(d) > 0)
        {
            XEvent ev;
            XNextEvent(d, &ev);
            if (ev.type == ClientMessage
                && (Atom)ev.xclient.data.l[0] == wm_delete)
                g_running = 0;
            if (ev.type == DestroyNotify)
                g_running = 0;
            if (ev.type == Expose)
            {
                // Redraw the fills (XWayland loses buffer contents when a
                // WM resizes the window; see above).
                if (nfills && ev.xexpose.count == 0)
                {
                    for (int i = 0; i < nfills; ++i)
                    {
                        struct fill_spec *f = &fills[i];
                        XSetForeground(d, gc, f->pixel);
                        XFillRectangle(d, win, gc, f->x, f->y,
                                       (unsigned)f->w, (unsigned)f->h);
                    }
                    XFlush(d);
                }
            }
            if (ev.type == KeyPress || ev.type == KeyRelease)
            {
                KeySym ks = XLookupKeysym(&ev.xkey, (ev.xkey.state & ShiftMask) ? 1 : 0);
                const char *name = ks ? XKeysymToString(ks) : "?";
                log_key(ev.type == KeyPress ? "k:down" : "k:up",
                        name ? name : "?", ev.xkey.window);
            }
            if (ev.type == ButtonPress || ev.type == ButtonRelease)
            {
                log_button(ev.type == ButtonPress ? "b:down" : "b:up",
                           ev.xbutton.button, ev.xbutton.window);
            }
        }
        if (now_ms() - start > ms)
            g_running = 0;
        usleep(20000);
    }
    if (g_ev)
        fclose(g_ev);
    XDestroyWindow(d, win);
    XCloseDisplay(d);
    return 0;
}
