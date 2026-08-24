/* gtk_ok.c -- M5-C oracle: two independent GTK processes, each with a window
 * (title) containing an entry whose ACCESSIBLE NAME equals --name and whose
 * text equals --text.  Proves Control* WinTitle limiting selects the right
 * application subtree (same-named controls in different apps do not cross).
 *
 * Usage: gtk_ok --title TITLE --name NAME --text TEXT [--pidfile PATH]
 */
#include <gtk/gtk.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int main(int argc, char **argv)
{
	const char *title = "AppA";
	const char *name = "OK";
	const char *text = "HelloA";
	const char *pidfile = NULL;
	for (int i = 1; i + 1 < argc; ++i)
	{
		if (!strcmp(argv[i], "--title")) title = argv[++i];
		else if (!strcmp(argv[i], "--name")) name = argv[++i];
		else if (!strcmp(argv[i], "--text")) text = argv[++i];
		else if (!strcmp(argv[i], "--pidfile")) pidfile = argv[++i];
	}
	gtk_init(&argc, &argv);
	if (pidfile)
	{
		FILE *f = fopen(pidfile, "w");
		if (f) { fprintf(f, "%d", (int)getpid()); fclose(f); }
	}
	GtkWidget *win = gtk_window_new(GTK_WINDOW_TOPLEVEL);
	gtk_window_set_title(GTK_WINDOW(win), title);
	GtkWidget *entry = gtk_entry_new();
	atk_object_set_name(gtk_widget_get_accessible(entry), name);
	gtk_entry_set_text(GTK_ENTRY(entry), text);
	gtk_container_add(GTK_CONTAINER(win), entry);
	g_signal_connect(win, "destroy", G_CALLBACK(gtk_main_quit), NULL);
	gtk_widget_show_all(win);
	gtk_main();
	return 0;
}
