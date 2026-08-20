/* gtk-keytest.c -- GTK3 window that records key presses for Wayland/X11
 * end-to-end tests ("did the compositor actually deliver this key to the
 * focused application?").  Prints each key event to stdout AND appends to
 * /tmp/keytest.txt (one char per line; F-keys as "F12" etc.).
 *
 * Usage: gtk-keytest [OUTFILE]
 * Exits when the window is closed or after -ms seconds if given as argv2.
 */
#include <gtk/gtk.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

static FILE *g_out = NULL;
static const char *g_outpath = "/tmp/keytest.txt";
static GtkWidget *g_label = NULL;

static void record(const char *s)
{
	printf("KEY:%s\n", s);
	fflush(stdout);
	if (g_out)
	{
		fprintf(g_out, "%s\n", s);
		fflush(g_out);
	}
}

static gboolean on_key(GtkWidget *w, GdkEventKey *ev, gpointer data)
{
	char buf[64];
	guint k = ev->keyval;
	if (k >= 0x20 && k <= 0x7e)
		snprintf(buf, sizeof(buf), "%c", (int)k);
	else if ((gsize)k < 128)
	{
		const char *name = gdk_keyval_name((guint)k);
		snprintf(buf, sizeof(buf), "%s", name ? name : "?");
	}
	else
		snprintf(buf, sizeof(buf), "U+%04X", k);
	record(buf);
	return FALSE; /* also let the default handler run */
}

static gboolean on_delete(GtkWidget *w, GdkEventAny *ev, gpointer data)
{
	record("__window-closed__");
	exit(0);
	return TRUE;
}

static gboolean on_timeout(gpointer data)
{
	record("__timeout__");
	exit(0);
	return TRUE; /* unreachable */
}

int main(int argc, char **argv)
{
	if (argc > 1)
		g_outpath = argv[1];
	gtk_init(&argc, &argv);
	g_out = fopen(g_outpath, "w");
	if (!g_out)
		g_out = stdout;

	GtkWidget *win = gtk_window_new(GTK_WINDOW_TOPLEVEL);
	gtk_window_set_title(GTK_WINDOW(win), "AHK KeyTest");
	gtk_window_set_default_size(GTK_WINDOW(win), 300, 120);
	g_signal_connect(win, "key-press-event", G_CALLBACK(on_key), NULL);
	g_signal_connect(win, "delete-event", G_CALLBACK(on_delete), NULL);
	g_label = gtk_label_new("waiting...");
	gtk_container_add(GTK_CONTAINER(win), g_label);
	gtk_widget_show_all(win);
	if (argc > 2)
		g_timeout_add((guint)(atoi(argv[2]) * 1000), on_timeout, NULL);
	gtk_main();
	return 0;
}