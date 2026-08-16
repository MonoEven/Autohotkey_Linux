// Diagnose XGetImage on sway's XWayland root window.
// Usage: xgimg_diag [x|y|w|h]
#include <X11/Xlib.h>
#include <stdio.h>
#include <stdlib.h>

static int xerr_count = 0;
static int xerr_code = 0;
static int xerr_op = 0;
static int handle_xerror(Display *d, XErrorEvent *e) {
    xerr_count++;
    xerr_code = e->error_code;
    xerr_op = e->request_code;
    return 0;
}

int main(int argc, char **argv) {
    int x = 0, y = 0, w = 200, h = 150;
    if (argc > 4) { x = atoi(argv[1]); y = atoi(argv[2]); w = atoi(argv[3]); h = atoi(argv[4]); }
    Display *d = XOpenDisplay(NULL);
    if (!d) { printf("no display\n"); return 1; }
    XSetErrorHandler(handle_xerror);
    Window root = DefaultRootWindow(d);
    printf("display ok, root=0x%lx depth=%d\n", root, DefaultDepth(d, DefaultScreen(d)));
    XImage *img = XGetImage(d, root, x, y, (unsigned)w, (unsigned)h, AllPlanes, ZPixmap);
    printf("XGetImage -> %s (xerr=%d code=%d op=%d)\n",
           img ? "OK" : "NULL", xerr_count, xerr_code, xerr_op);
    if (img) {
        printf("img: w=%d h=%d bpp=%d bytes_per_line=%ld\n",
               img->width, img->height, img->bits_per_pixel, img->bytes_per_line);
        printf("pixel(0,0)=%lu\n", XGetPixel(img, 0, 0));
        XDestroyImage(img);
    }
    // Also try with 0,0 root relative - maybe the issue is coordinates?
    XSync(d, False);
    XCloseDisplay(d);
    return 0;
}
