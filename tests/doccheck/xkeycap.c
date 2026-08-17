/* xkeycap: capture XTEST-generated keyboard/mouse events for the doc-check
 * input suite.
 *
 * Usage: xkeycap [-out FILE] [-ms N]
 *
 * Creates a window, grabs the keyboard and pointer, logs each key/button
 * event to FILE (default /tmp/ahk_dc_keycap.txt) as:
 *   k:down:<keysym-name>:mods:<state-hex>
 *   k:up:<keysym-name>:mods:<state-hex>
 *   b:down:<button>:mods:<state-hex>
 *   b:up:<button>:mods:<state-hex>
 * and exits after -ms milliseconds (default 120000) or on WM_DELETE_WINDOW.
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

static int g_running = 1;
static FILE *g_out = NULL;

static long now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

int main(int argc, char **argv)
{
    const char *outfile = "/tmp/ahk_dc_keycap.txt";
    long ms = 120000;
    for (int i = 1; i < argc; ++i)
    {
        if (!strcmp(argv[i], "-out") && i + 1 < argc) outfile = argv[++i];
        else if (!strcmp(argv[i], "-ms") && i + 1 < argc) ms = atol(argv[++i]);
    }

    Display *d = XOpenDisplay(NULL);
    if (!d)
    {
        fprintf(stderr, "xkeycap: no display\n");
        return 1;
    }
    int screen = DefaultScreen(d);
    Window root = RootWindow(d, screen);
    Window win = XCreateSimpleWindow(d, root, 0, 0, 300, 200, 0,
                                     BlackPixel(d, screen), WhitePixel(d, screen));
    XStoreName(d, win, "KeyCap Capture");
    XSelectInput(d, win, KeyPressMask | KeyReleaseMask | ButtonPressMask
                        | ButtonReleaseMask | StructureNotifyMask);

    Atom wm_delete = XInternAtom(d, "WM_DELETE_WINDOW", False);
    XSetWMProtocols(d, win, &wm_delete, 1);

    XMapWindow(d, win);
    /* Xvfb has no window manager: the window must be mapped before
     * XSetInputFocus, otherwise the focus request fails with BadMatch and
     * key events go nowhere.  Wait for the MapNotify. */
    XFlush(d);
    {
        int mapped = 0;
        for (int i = 0; i < 100 && !mapped; ++i)
        {
            while (XPending(d) > 0)
            {
                XEvent ev;
                XNextEvent(d, &ev);
                if (ev.type == MapNotify && ev.xmap.window == win)
                    mapped = 1;
            }
            usleep(10000);
        }
    }
    XSetInputFocus(d, win, RevertToParent, CurrentTime);
    /* No grabs here: key events follow the input focus and button events go
     * to the window under the pointer, so xkeycap receives them while the
     * pointer is over its window.  Grabbing would prevent BlockInput's own
     * XGrabKeyboard from succeeding (grabs cannot be stolen). */
    XFlush(d);

    g_out = fopen(outfile, "w");
    if (!g_out)
        g_out = stderr;
    long start = now_ms();

    while (g_running)
    {
        while (XPending(d) > 0)
        {
            XEvent ev;
            XNextEvent(d, &ev);
            switch (ev.type)
            {
            case KeyPress:
            case KeyRelease:
            {
                // Index 0 = unshifted keysym; index 1 = shifted.  Use the
                // column matching the event's shift state so "A" (shift+a)
                // resolves to the "A" keysym.
                int idx = (ev.xkey.state & ShiftMask) ? 1 : 0;
                KeySym ks = XLookupKeysym(&ev.xkey, idx);
                const char *name = ks ? XKeysymToString(ks) : "?";
                fprintf(g_out, "k:%s:%s:mods:%x:t:%ld\n",
                        ev.type == KeyPress ? "down" : "up",
                        name ? name : "?", (unsigned)ev.xkey.state, now_ms() - start);
                fflush(g_out);
                break;
            }
            case ButtonPress:
            case ButtonRelease:
                fprintf(g_out, "b:%s:%u:mods:%x:t:%ld\n",
                        ev.type == ButtonPress ? "down" : "up",
                        (unsigned)ev.xbutton.button, (unsigned)ev.xbutton.state,
                        now_ms() - start);
                fflush(g_out);
                break;
            case ClientMessage:
                if ((Atom)ev.xclient.data.l[0] == wm_delete)
                    g_running = 0;
                break;
            case DestroyNotify:
                g_running = 0;
                break;
            }
        }
        if (now_ms() - start > ms)
            g_running = 0;
        usleep(10000);
    }
    if (g_out != stderr)
        fclose(g_out);
    XDestroyWindow(d, win);
    XCloseDisplay(d);
    return 0;
}
