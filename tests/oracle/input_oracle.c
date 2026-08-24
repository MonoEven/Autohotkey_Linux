/* Independent X11 input oracle for AutoHotkey Linux tests.
 *
 * record-x11 OUT KEYSYM COUNT TIMEOUT_MS
 *   Records XI2.1 raw events as JSONL.  It does not use any AHK code or data.
 * inject-x11 KEYSYM HOLD_MS
 *   Injects one XTEST press/release pair from a separate process.
 */
#include <X11/Xlib.h>
#include <X11/keysym.h>
#include <X11/extensions/XInput2.h>
#include <X11/extensions/XTest.h>
#include <errno.h>
#include <poll.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static uint64_t monotonic_us(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000ULL + (uint64_t)ts.tv_nsec / 1000ULL;
}

static int is_xtest_device(Display *d, int id)
{
    int n = 0;
    XIDeviceInfo *info = XIQueryDevice(d, XIAllDevices, &n);
    int found = 0;
    if (info) {
        for (int i = 0; i < n; ++i) {
            if (info[i].deviceid == id && info[i].name
                && (strstr(info[i].name, "XTEST") || strstr(info[i].name, "XTest"))) {
                found = 1;
                break;
            }
        }
        XIFreeDeviceInfo(info);
    }
    return found;
}

static KeyCode resolve_key(Display *d, const char *name)
{
    KeySym sym = XStringToKeysym(name);
    if (sym == NoSymbol) {
        fprintf(stderr, "input-oracle: unknown keysym: %s\n", name);
        return 0;
    }
    KeyCode code = XKeysymToKeycode(d, sym);
    if (!code)
        fprintf(stderr, "input-oracle: keysym has no keycode: %s\n", name);
    return code;
}

static int record_x11(const char *out_path, const char *keysym_name,
                      int wanted, int timeout_ms)
{
    Display *d = XOpenDisplay(NULL);
    if (!d) {
        fprintf(stderr, "input-oracle: cannot open DISPLAY\n");
        return 2;
    }
    int xi_opcode = 0, event = 0, error = 0;
    if (!XQueryExtension(d, "XInputExtension", &xi_opcode, &event, &error)) {
        fprintf(stderr, "input-oracle: XI2 unavailable\n");
        XCloseDisplay(d);
        return 2;
    }
    int major = 2, minor = 1;
    if (XIQueryVersion(d, &major, &minor) != Success
        || major < 2 || (major == 2 && minor < 1)) {
        fprintf(stderr, "input-oracle: XI2.1 required (server %d.%d)\n", major, minor);
        XCloseDisplay(d);
        return 2;
    }
    KeyCode filter = resolve_key(d, keysym_name);
    if (!filter) {
        XCloseDisplay(d);
        return 2;
    }
    unsigned char mask[(XI_LASTEVENT + 7) / 8];
    memset(mask, 0, sizeof(mask));
    XISetMask(mask, XI_RawKeyPress);
    XISetMask(mask, XI_RawKeyRelease);
    XIEventMask event_mask;
    event_mask.deviceid = XIAllMasterDevices;
    event_mask.mask_len = sizeof(mask);
    event_mask.mask = mask;
    if (XISelectEvents(d, DefaultRootWindow(d), &event_mask, 1) != Success) {
        fprintf(stderr, "input-oracle: XISelectEvents failed\n");
        XCloseDisplay(d);
        return 2;
    }
    XSync(d, False);

    FILE *out = fopen(out_path, "w");
    if (!out) {
        fprintf(stderr, "input-oracle: %s: %s\n", out_path, strerror(errno));
        XCloseDisplay(d);
        return 2;
    }
    fprintf(out, "{\"schema\":1,\"type\":\"ready\",\"xi\":\"%d.%d\","
                 "\"keysym\":\"%s\",\"keycode\":%u}\n",
            major, minor, keysym_name, (unsigned)filter);
    fflush(out);

    uint64_t deadline = monotonic_us() + (uint64_t)timeout_ms * 1000ULL;
    int count = 0;
    while (count < wanted && monotonic_us() < deadline) {
        int remain = (int)((deadline - monotonic_us()) / 1000ULL);
        if (remain < 0) remain = 0;
        struct pollfd pfd = { ConnectionNumber(d), POLLIN, 0 };
        int prc = poll(&pfd, 1, remain > 100 ? 100 : remain);
        if (prc < 0 && errno != EINTR) break;
        while (XPending(d)) {
            XEvent ev;
            XNextEvent(d, &ev);
            if (ev.type != GenericEvent || ev.xcookie.extension != xi_opcode)
                continue;
            if (ev.xcookie.evtype != XI_RawKeyPress
                && ev.xcookie.evtype != XI_RawKeyRelease)
                continue;
            if (!XGetEventData(d, &ev.xcookie))
                continue;
            XIRawEvent *raw = (XIRawEvent *)ev.xcookie.data;
            if ((KeyCode)raw->detail == filter) {
                const char *phase = ev.xcookie.evtype == XI_RawKeyPress ? "down" : "up";
                int xtest = is_xtest_device(d, raw->sourceid);
                fprintf(out, "{\"schema\":1,\"type\":\"key\",\"seq\":%d,"
                             "\"monotonic_us\":%llu,\"x_time\":%lu,"
                             "\"phase\":\"%s\",\"keycode\":%u,"
                             "\"deviceid\":%d,\"sourceid\":%d,\"xtest\":%s}\n",
                        count, (unsigned long long)monotonic_us(), (unsigned long)raw->time,
                        phase, (unsigned)raw->detail, raw->deviceid, raw->sourceid,
                        xtest ? "true" : "false");
                fflush(out);
                ++count;
            }
            XFreeEventData(d, &ev.xcookie);
        }
    }
    fclose(out);
    XCloseDisplay(d);
    if (count != wanted) {
        fprintf(stderr, "input-oracle: wanted %d events, recorded %d\n", wanted, count);
        return 1;
    }
    return 0;
}

static int inject_x11(const char *keysym_name, int hold_ms)
{
    Display *d = XOpenDisplay(NULL);
    if (!d) {
        fprintf(stderr, "input-oracle: cannot open DISPLAY\n");
        return 2;
    }
    KeyCode code = resolve_key(d, keysym_name);
    if (!code) {
        XCloseDisplay(d);
        return 2;
    }
    if (!XTestFakeKeyEvent(d, code, True, CurrentTime)) {
        XCloseDisplay(d);
        return 2;
    }
    XFlush(d);
    usleep((useconds_t)(hold_ms < 0 ? 0 : hold_ms) * 1000U);
    XTestFakeKeyEvent(d, code, False, CurrentTime);
    XSync(d, False);
    XCloseDisplay(d);
    return 0;
}

static void usage(const char *argv0)
{
    fprintf(stderr, "usage:\n"
        "  %s record-x11 OUT KEYSYM COUNT TIMEOUT_MS\n"
        "  %s inject-x11 KEYSYM HOLD_MS\n", argv0, argv0);
}

int main(int argc, char **argv)
{
    if (argc == 6 && !strcmp(argv[1], "record-x11"))
        return record_x11(argv[2], argv[3], atoi(argv[4]), atoi(argv[5]));
    if (argc == 4 && !strcmp(argv[1], "inject-x11"))
        return inject_x11(argv[2], atoi(argv[3]));
    usage(argv[0]);
    return 2;
}
