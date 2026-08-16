/* xwin_helper: create an X11 test window for the doc-check Win* suite.
 *
 * Usage: xwin_helper -title T -class C -x X -y Y -w W -h H
 *                    [-hidden] [-focus] [-ms N]
 *
 * Sets WM_NAME/_NET_WM_NAME, WM_CLASS, _NET_WM_PID, maps the window (unless
 * -hidden), optionally takes input focus, and exits on WM_DELETE_WINDOW or
 * after -ms milliseconds (default 600000) or when stdin closes.
 */
#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <X11/Xutil.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static int g_running = 1;

int main(int argc, char **argv)
{
    const char *title = "DocCheck Window";
    const char *cls = "DocCheckClass";
    int x = 100, y = 100, w = 400, h = 300;
    int hidden = 0, focus = 0;
    long ms = 600000;

    for (int i = 1; i < argc; ++i)
    {
        if (!strcmp(argv[i], "-title") && i + 1 < argc) title = argv[++i];
        else if (!strcmp(argv[i], "-class") && i + 1 < argc) cls = argv[++i];
        else if (!strcmp(argv[i], "-x") && i + 1 < argc) x = atoi(argv[++i]);
        else if (!strcmp(argv[i], "-y") && i + 1 < argc) y = atoi(argv[++i]);
        else if (!strcmp(argv[i], "-w") && i + 1 < argc) w = atoi(argv[++i]);
        else if (!strcmp(argv[i], "-h") && i + 1 < argc) h = atoi(argv[++i]);
        else if (!strcmp(argv[i], "-ms") && i + 1 < argc) ms = atol(argv[++i]);
        else if (!strcmp(argv[i], "-hidden")) hidden = 1;
        else if (!strcmp(argv[i], "-focus")) focus = 1;
    }

    Display *d = XOpenDisplay(NULL);
    if (!d)
    {
        fprintf(stderr, "xwin_helper: no display\n");
        return 1;
    }
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

    // WM_DELETE_WINDOW so WinClose can ask us to exit.
    Atom wm_protocols = XInternAtom(d, "WM_PROTOCOLS", False);
    Atom wm_delete = XInternAtom(d, "WM_DELETE_WINDOW", False);
    XSetWMProtocols(d, win, &wm_delete, 1);
    (void)wm_protocols;

    XSelectInput(d, win, StructureNotifyMask | ExposureMask);
    if (!hidden)
        XMapWindow(d, win);
    if (focus)
        XSetInputFocus(d, win, RevertToParent, CurrentTime);
    XFlush(d);

    // Small loop so X server errors don't kill us mid-flight.
    long start = 0;
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    start = ts.tv_sec * 1000 + ts.tv_nsec / 1000000;

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
        }
        clock_gettime(CLOCK_MONOTONIC, &ts);
        long now = ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
        if (now - start > ms)
            g_running = 0;
        usleep(20000);
    }
    XDestroyWindow(d, win);
    XCloseDisplay(d);
    return 0;
}
