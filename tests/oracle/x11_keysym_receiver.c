/* Independent focused X11 window which decodes one non-modifier KeyPress
 * with XkbLookupKeySym and writes JSONL. Used to verify layout-aware Send. */
#include <X11/Xlib.h>
#include <X11/XKBlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>
#include <errno.h>
#include <poll.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static uint64_t mono_us(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000ULL + (uint64_t)ts.tv_nsec / 1000ULL;
}

static int modifier(KeySym sym)
{
    return IsModifierKey(sym) || sym == XK_ISO_Level3_Shift;
}

int main(int argc, char **argv)
{
    if (argc != 4) {
        fprintf(stderr, "usage: %s OUT EXPECTED_KEYSYM TIMEOUT_MS\n", argv[0]);
        return 2;
    }
    const char *out_path = argv[1];
    KeySym expected = XStringToKeysym(argv[2]);
    int timeout_ms = atoi(argv[3]);
    if (expected == NoSymbol || timeout_ms <= 0)
        return 2;
    Display *d = XOpenDisplay(NULL);
    if (!d) return 2;
    int screen = DefaultScreen(d);
    Window w = XCreateSimpleWindow(d, RootWindow(d, screen), 40, 40, 320, 100,
                                   0, BlackPixel(d, screen), WhitePixel(d, screen));
    XStoreName(d, w, "AHK External Keysym Oracle");
    XSelectInput(d, w, KeyPressMask | KeyReleaseMask | StructureNotifyMask);
    XMapWindow(d, w);
    XSync(d, False);
    XSetInputFocus(d, w, RevertToParent, CurrentTime);
    XSync(d, False);
    FILE *out = fopen(out_path, "w");
    if (!out) { XDestroyWindow(d, w); XCloseDisplay(d); return 2; }
    fprintf(out, "{\"schema\":1,\"type\":\"ready\",\"window\":%lu,"
                 "\"expected\":\"%s\"}\n", (unsigned long)w, argv[2]);
    fflush(out);
    uint64_t deadline = mono_us() + (uint64_t)timeout_ms * 1000ULL;
    int matched = 0;
    while (mono_us() < deadline && !matched) {
        struct pollfd pfd = { ConnectionNumber(d), POLLIN, 0 };
        poll(&pfd, 1, 100);
        while (XPending(d)) {
            XEvent ev;
            XNextEvent(d, &ev);
            if (ev.type != KeyPress) continue;
            unsigned int consumed = 0;
            KeySym sym = NoSymbol;
            if (!XkbLookupKeySym(d, ev.xkey.keycode, ev.xkey.state, &consumed, &sym))
                continue;
            if (modifier(sym)) continue;
            const char *name = XKeysymToString(sym);
            fprintf(out, "{\"schema\":1,\"type\":\"key\","
                         "\"monotonic_us\":%llu,\"keycode\":%u,"
                         "\"state\":%u,\"consumed\":%u,\"keysym\":\"%s\"}\n",
                    (unsigned long long)mono_us(), ev.xkey.keycode, ev.xkey.state,
                    consumed, name ? name : "");
            fflush(out);
            matched = sym == expected;
            if (!matched) break;
        }
    }
    fclose(out);
    XDestroyWindow(d, w);
    XCloseDisplay(d);
    return matched ? 0 : 1;
}
