/* x11_ewmh.c -- M5-A oracle: fake WM client windows for EWMH enumeration.
 *
 * Creates two top-level windows A ("EWMH-A") and B ("EWMH-B") with _NET_WM_NAME.
 * By default it also sets the root _NET_CLIENT_LIST to [A] exactly as a WM
 * would, so a consumer using EWMH sees only A.  With --no-client-list the
 * property is left unset (no-WM mode -> fallback enumeration sees both).
 *
 * Prints "A=<id> B=<id>\n" on startup and stays until SIGTERM.
 */
#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static volatile sig_atomic_t g_run = 1;
static void on_sig(int s) { (void)s; g_run = 0; }

static Window make_window(Display *d, int x, const char *name)
{
	Window root = DefaultRootWindow(d);
	Window w = XCreateSimpleWindow(d, root, x, 20, 200, 120, 0,
		BlackPixel(d, DefaultScreen(d)), WhitePixel(d, DefaultScreen(d)));
	Atom net_wm_name = XInternAtom(d, "_NET_WM_NAME", False);
	Atom utf8 = XInternAtom(d, "UTF8_STRING", False);
	XChangeProperty(d, w, net_wm_name, utf8, 8, PropModeReplace,
		(const unsigned char *)name, (int)strlen(name));
	XMapWindow(d, w);
	XFlush(d);
	return w;
}

int main(int argc, char **argv)
{
	int no_client_list = 0;
	for (int i = 1; i < argc; ++i)
		if (!strcmp(argv[i], "--no-client-list"))
			no_client_list = 1;
	Display *d = XOpenDisplay(NULL);
	if (!d) { fprintf(stderr, "x11_ewmh: no display\n"); return 1; }
	Window a = make_window(d, 20, "EWMH-A");
	Window b = make_window(d, 260, "EWMH-B");
	if (!no_client_list)
	{
		Window root = DefaultRootWindow(d);
		Atom prop = XInternAtom(d, "_NET_CLIENT_LIST", False);
		Window list[1] = { a };
		XChangeProperty(d, root, prop, XA_WINDOW, 32, PropModeReplace,
			(const unsigned char *)list, 1);
	}
	printf("A=%lu B=%lu\n", (unsigned long)a, (unsigned long)b);
	fflush(stdout);
	signal(SIGTERM, on_sig);
	signal(SIGINT, on_sig);
	while (g_run)
	{
		XFlush(d);
		usleep(100000);
	}
	XCloseDisplay(d);
	return 0;
}
