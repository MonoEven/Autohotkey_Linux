// Independent libeis receiver for M6 libei sender behavior tests.
#define _GNU_SOURCE
#include <libeis.h>
#include <xkbcommon/xkbcommon.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/input-event-codes.h>
#include <poll.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

static volatile sig_atomic_t quit;
static void on_signal(int sig) { (void)sig; quit = 1; }
static uint64_t monotonic_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000 + (uint64_t)ts.tv_nsec / 1000000;
}
enum { CAP_KBD=1, CAP_PTR=2, CAP_BUTTON=4, CAP_SCROLL=8, CAP_ALL=15 };

static int create_keymap_fd(size_t *out_size, uint32_t *out_caps_mask)
{
    struct xkb_context *ctx = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
    struct xkb_rule_names names = { .rules = "evdev", .model = "pc105",
        .layout = "us", .variant = "", .options = "" };
    struct xkb_keymap *map = ctx ? xkb_keymap_new_from_names(ctx, &names,
        XKB_KEYMAP_COMPILE_NO_FLAGS) : NULL;
    if (out_caps_mask) *out_caps_mask = 0;
    if (map && out_caps_mask)
    {
        xkb_mod_index_t index = xkb_keymap_mod_get_index(map, XKB_MOD_NAME_CAPS);
        if (index != XKB_MOD_INVALID && index < 32)
            *out_caps_mask = (uint32_t)1u << index;
    }
    char *text = map ? xkb_keymap_get_as_string(map, XKB_KEYMAP_FORMAT_TEXT_V1) : NULL;
    if (map) xkb_keymap_unref(map);
    if (ctx) xkb_context_unref(ctx);
    if (!text) return -1;
    size_t size = strlen(text) + 1;
    int fd = memfd_create("ahk-libei-keymap", MFD_CLOEXEC | MFD_ALLOW_SEALING);
    if (fd < 0 || ftruncate(fd, (off_t)size) != 0
        || write(fd, text, size) != (ssize_t)size)
    {
        int saved = errno;
        if (fd >= 0) close(fd);
        free(text); errno = saved; return -1;
    }
    lseek(fd, 0, SEEK_SET);
    free(text);
    *out_size = size;
    return fd;
}

static struct eis_device *add_device(struct eis_seat *seat,
    struct eis_event *bind, FILE *log, int allowed, const char *name,
    bool caps_locked)
{
    struct eis_device *device = eis_seat_new_device(seat);
    if (!device) return NULL;
    eis_device_configure_type(device, EIS_DEVICE_TYPE_VIRTUAL);
    eis_device_configure_name(device, name ? name : "AHK independent EIS oracle");
    bool keyboard = (allowed & CAP_KBD) && (bind
        ? eis_event_seat_has_capability(bind, EIS_DEVICE_CAP_KEYBOARD)
        : eis_seat_has_capability(seat, EIS_DEVICE_CAP_KEYBOARD));
    bool pointer = (allowed & CAP_PTR) && (bind
        ? eis_event_seat_has_capability(bind, EIS_DEVICE_CAP_POINTER)
        : eis_seat_has_capability(seat, EIS_DEVICE_CAP_POINTER));
    bool button = (allowed & CAP_BUTTON) && (bind
        ? eis_event_seat_has_capability(bind, EIS_DEVICE_CAP_BUTTON)
        : eis_seat_has_capability(seat, EIS_DEVICE_CAP_BUTTON));
    bool scroll = (allowed & CAP_SCROLL) && (bind
        ? eis_event_seat_has_capability(bind, EIS_DEVICE_CAP_SCROLL)
        : eis_seat_has_capability(seat, EIS_DEVICE_CAP_SCROLL));
    if (keyboard) eis_device_configure_capability(device, EIS_DEVICE_CAP_KEYBOARD);
    if (pointer) eis_device_configure_capability(device, EIS_DEVICE_CAP_POINTER);
    if (button) eis_device_configure_capability(device, EIS_DEVICE_CAP_BUTTON);
    if (scroll) eis_device_configure_capability(device, EIS_DEVICE_CAP_SCROLL);
    uint32_t caps_mask = 0;
    if (keyboard)
    {
        size_t size = 0;
        int fd = create_keymap_fd(&size, &caps_mask);
        if (fd >= 0)
        {
            struct eis_keymap *keymap = eis_device_new_keymap(device,
                EIS_KEYMAP_TYPE_XKB, fd, size);
            close(fd);
            if (keymap)
            {
                eis_keymap_add(keymap);
                eis_keymap_unref(keymap);
            }
        }
    }
    eis_device_add(device);
    eis_device_resume(device);
    if (keyboard && caps_locked && caps_mask)
    {
        eis_device_keyboard_send_xkb_modifiers(device, 0, 0, caps_mask, 0);
        fprintf(log, "EIS_MODIFIERS name=%s locked=%u group=0\n",
            name ? name : "combined", caps_mask);
    }
    fprintf(log, "EIS_DEVICE name=%s keyboard=%d pointer=%d button=%d scroll=%d text=0\n",
        name ? name : "combined", keyboard, pointer, button, scroll);
    fflush(log);
    return device;
}

int main(int argc, char **argv)
{
    if (argc < 3)
    {
        fprintf(stderr, "usage: %s SOCKET LOG [--disconnect-after-frame N]\n", argv[0]);
        return 2;
    }
    int disconnect_after = 0, replace_after = 0, remove_after = 0;
    int split_delay_ms = 0, split_drag = 0;
#ifdef AHK_LIBEIS_HAS_SYNC
    int sync_delay_ms = 0;
#endif
    int device_mask = CAP_ALL, caps_locked = 0;
    for (int i = 3; i + 1 < argc; i += 2)
    {
        if (!strcmp(argv[i], "--disconnect-after-frame"))
            disconnect_after = atoi(argv[i + 1]);
        else if (!strcmp(argv[i], "--replace-after-frame"))
            replace_after = atoi(argv[i + 1]);
        else if (!strcmp(argv[i], "--remove-after-frame"))
            remove_after = atoi(argv[i + 1]);
        else if (!strcmp(argv[i], "--split-drag"))
            split_drag = atoi(argv[i + 1]);
        else if (!strcmp(argv[i], "--split-delay-ms"))
            split_delay_ms = atoi(argv[i + 1]);
        else if (!strcmp(argv[i], "--device-mask"))
            device_mask = atoi(argv[i + 1]);
        else if (!strcmp(argv[i], "--caps-locked"))
            caps_locked = atoi(argv[i + 1]);
#ifdef AHK_LIBEIS_HAS_SYNC
        else if (!strcmp(argv[i], "--sync-delay-ms"))
            sync_delay_ms = atoi(argv[i + 1]);
#endif
    }
    signal(SIGTERM, on_signal); signal(SIGINT, on_signal);
    unlink(argv[1]);
    FILE *log = fopen(argv[2], "w");
    if (!log) { perror("fopen"); return 1; }
    setvbuf(log, NULL, _IOLBF, 0);
    struct eis *eis = eis_new(NULL);
    if (!eis || eis_setup_backend_socket(eis, argv[1]) != 0)
    {
        fprintf(log, "EIS_ERROR setup errno=%d\n", errno);
        if (eis) eis_unref(eis);
        fclose(log); return 1;
    }
    fprintf(log, "EIS_READY socket=%s\n", argv[1]);
    struct eis_client *client = NULL;
    struct eis_seat *seat = NULL;
    struct eis_device *device = NULL, *device2 = NULL;
    uint64_t add_keyboard_at = 0;
    int frames = 0;
    while (!quit)
    {
        if (add_keyboard_at && monotonic_ms() >= add_keyboard_at && seat)
        {
            device2 = add_device(seat, NULL, log, CAP_KBD, "keyboard-delayed",
                caps_locked != 0);
            fprintf(log, "EIS_DELAYED_KEYBOARD_ADDED\n");
            add_keyboard_at = 0;
        }
        struct pollfd pfd = { eis_get_fd(eis), POLLIN, 0 };
        int pr = poll(&pfd, 1, 50);
        if (pr < 0 && errno == EINTR) continue;
        if (pr > 0) eis_dispatch(eis);
        struct eis_event *event;
        while ((event = eis_get_event(eis)))
        {
            enum eis_event_type type = eis_event_get_type(event);
            switch (type)
            {
            case EIS_EVENT_CLIENT_CONNECT:
                client = eis_client_ref(eis_event_get_client(event));
                if (!eis_client_is_sender(client))
                    eis_client_disconnect(client);
                else
                {
                    eis_client_connect(client);
                    seat = eis_client_new_seat(client, "default");
                    eis_seat_configure_capability(seat, EIS_DEVICE_CAP_KEYBOARD);
                    eis_seat_configure_capability(seat, EIS_DEVICE_CAP_POINTER);
                    eis_seat_configure_capability(seat, EIS_DEVICE_CAP_BUTTON);
                    eis_seat_configure_capability(seat, EIS_DEVICE_CAP_SCROLL);
                    eis_seat_add(seat);
                    fprintf(log, "EIS_CONNECT sender=1 name=%s\n",
                        eis_client_get_name(client));
                }
                break;
            case EIS_EVENT_SEAT_BIND:
                if (!device && !device2)
                {
                    if (split_drag)
                    {
                        device = add_device(eis_event_get_seat(event), event, log,
                            CAP_PTR, "pointer-only", false);
                        device2 = add_device(eis_event_get_seat(event), event, log,
                            CAP_BUTTON, "button-only", false);
                    }
                    else if (split_delay_ms > 0)
                    {
                        device = add_device(eis_event_get_seat(event), event, log,
                            CAP_PTR | CAP_BUTTON | CAP_SCROLL, "pointer-first", false);
                        add_keyboard_at = monotonic_ms() + (uint64_t)split_delay_ms;
                        fprintf(log, "EIS_DELAYED_KEYBOARD_PENDING delay_ms=%d\n",
                            split_delay_ms);
                    }
                    else
                        device = add_device(eis_event_get_seat(event), event, log,
                            device_mask, "combined", caps_locked != 0);
                }
                fprintf(log, "EIS_SEAT_BIND\n");
                break;
            case EIS_EVENT_DEVICE_START_EMULATING:
                /* libeis <1.4 exposes 0 from the accessor despite carrying the
                 * client sequence on wire; client-side trace is authoritative
                 * for M6a's session-local mapping on those releases. */
                fprintf(log, "EIS_START name=%s sequence=%u\n",
                    eis_device_get_name(eis_event_get_device(event)),
                    eis_event_emulating_get_sequence(event));
                break;
            case EIS_EVENT_DEVICE_STOP_EMULATING:
                fprintf(log, "EIS_STOP\n");
                break;
            case EIS_EVENT_KEYBOARD_KEY:
                fprintf(log, "EIS_KEY name=%s code=%u down=%d\n",
                    eis_device_get_name(eis_event_get_device(event)),
                    eis_event_keyboard_get_key(event),
                    eis_event_keyboard_get_key_is_press(event));
                break;
            case EIS_EVENT_BUTTON_BUTTON:
                fprintf(log, "EIS_BUTTON name=%s code=%u down=%d\n",
                    eis_device_get_name(eis_event_get_device(event)),
                    eis_event_button_get_button(event),
                    eis_event_button_get_is_press(event));
                break;
            case EIS_EVENT_POINTER_MOTION:
                fprintf(log, "EIS_MOTION name=%s dx=%.3f dy=%.3f\n",
                    eis_device_get_name(eis_event_get_device(event)),
                    eis_event_pointer_get_dx(event), eis_event_pointer_get_dy(event));
                break;
            case EIS_EVENT_SCROLL_DISCRETE:
                fprintf(log, "EIS_SCROLL name=%s x=%d y=%d\n",
                    eis_device_get_name(eis_event_get_device(event)),
                    eis_event_scroll_get_discrete_dx(event),
                    eis_event_scroll_get_discrete_dy(event));
                break;
            case EIS_EVENT_FRAME:
                ++frames;
                fprintf(log, "EIS_FRAME count=%d time=%llu\n", frames,
                    (unsigned long long)eis_event_get_time(event));
                if (remove_after > 0 && frames == remove_after && device)
                {
                    eis_device_remove(device);
                    eis_device_unref(device);
                    device = NULL;
                    fprintf(log, "EIS_REMOVE_ONLY\n");
                }
                if (replace_after > 0 && frames == replace_after && device)
                {
                    eis_device_remove(device);
                    eis_device_unref(device);
                    device = NULL;
                    fprintf(log, "EIS_REMOVE\n");
                    device = add_device(seat, NULL, log, CAP_ALL,
                        "combined-replacement", caps_locked != 0);
                    fprintf(log, "EIS_REPLACE\n");
                }
                if (disconnect_after > 0 && frames >= disconnect_after && client)
                {
                    fprintf(log, "EIS_FORCED_DISCONNECT\n");
                    eis_client_disconnect(client);
                }
                break;
            case EIS_EVENT_DEVICE_CLOSED:
            {
                struct eis_device *closed = eis_event_get_device(event);
                fprintf(log, "EIS_DEVICE_CLOSED name=%s\n",
                    eis_device_get_name(closed));
                if (closed == device) { eis_device_unref(device); device = NULL; }
                if (closed == device2) { eis_device_unref(device2); device2 = NULL; }
                break;
            }
#ifdef AHK_LIBEIS_HAS_SYNC
            case EIS_EVENT_SYNC:
                fprintf(log, "EIS_SYNC\n");
                if (sync_delay_ms > 0)
                    usleep((useconds_t)sync_delay_ms * 1000);
                break; // unref sends the matching pong after prior events are processed.
#endif
            case EIS_EVENT_CLIENT_DISCONNECT:
                fprintf(log, "EIS_DISCONNECT\n");
                quit = 1;
                break;
            default:
                fprintf(log, "EIS_EVENT type=%d\n", (int)type);
                break;
            }
            eis_event_unref(event);
        }
    }
    if (device) { eis_device_remove(device); eis_device_unref(device); }
    if (device2) { eis_device_remove(device2); eis_device_unref(device2); }
    if (seat) { eis_seat_remove(seat); eis_seat_unref(seat); }
    if (client) eis_client_unref(client);
    eis_unref(eis);
    unlink(argv[1]);
    fclose(log);
    return 0;
}
