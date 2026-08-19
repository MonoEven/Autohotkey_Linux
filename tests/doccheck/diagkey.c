/* diagkey: print raw keycode + state + keysym columns for each KeyPress,
 * for diagnosing Unicode keysym transmission (round-34). */
#include <X11/Xlib.h>
#include <X11/keysym.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
int main(void) {
    Display *d = XOpenDisplay(NULL);
    if (!d) { fprintf(stderr, "no display\n"); return 1; }
    setvbuf(stdout, NULL, _IOLBF, 0); // Line-buffer: see key events as they arrive.
    int screen = DefaultScreen(d);
    Window win = XCreateSimpleWindow(d, RootWindow(d, screen), 0, 0, 200, 120, 0,
                                     BlackPixel(d, screen), WhitePixel(d, screen));
    XStoreName(d, win, "DiagKey");
    XSelectInput(d, win, KeyPressMask | KeyReleaseMask | StructureNotifyMask);
    XMapWindow(d, win);
    XFlush(d);
    usleep(300000);
    XSetInputFocus(d, win, RevertToParent, CurrentTime);
    XFlush(d);
    int min_kc, max_kc;
    XDisplayKeycodes(d, &min_kc, &max_kc);
    printf("keycodes range %d..%d\n", min_kc, max_kc);
    for (int i = 0; i < 400; ++i) {
        while (XPending(d)) {
            XEvent ev;
            XNextEvent(d, &ev);
            if (ev.type == MappingNotify) {
                XRefreshKeyboardMapping(&ev.xmapping);
                printf("MappingNotify: request=%d\n", ev.xmapping.request);
                continue;
            }
            if (ev.type == KeyPress) {
                KeySym k0 = XLookupKeysym(&ev.xkey, 0);
                KeySym k1 = XLookupKeysym(&ev.xkey, 1);
                printf("KeyPress keycode=%d state=%04x level0=%s level1=%s\n",
                       ev.xkey.keycode, (unsigned)ev.xkey.state,
                       k0 ? XKeysymToString(k0) : "?",
                       k1 ? XKeysymToString(k1) : "?");
            }
        }
        usleep(20000);
    }
    return 0;
}
