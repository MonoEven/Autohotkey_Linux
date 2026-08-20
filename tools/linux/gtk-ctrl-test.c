/* gtk-ctrl-test.c -- GTK3 window with an Entry/Button/Label to exercise
 * the AT-SPI ControlGetText/ControlSetText fallback (check0820).  Windows
 * are named by the window title; entries/buttons/labels get accessible
 * names from their labels.
 *
 * Usage: gtk-ctrl-test [title]
 * The window shows: a GtkEntry (name "entry field"), a GtkButton ("Click
 * Me"), and a GtkLabel ("Hello AT-SPI").
 */
#include <gtk/gtk.h>

static void on_btn(GtkButton *b, gpointer d)
{
	(void)b;
	gtk_label_set_text(GTK_LABEL(d), "clicked");
}

int main(int argc, char **argv)
{
	const char *title = argc > 1 ? argv[1] : "AHK AT-SPI Test";
	gtk_init(&argc, &argv);
	GtkWidget *win = gtk_window_new(GTK_WINDOW_TOPLEVEL);
	gtk_window_set_title(GTK_WINDOW(win), title);
	gtk_window_set_default_size(GTK_WINDOW(win), 400, 200);

	GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
	gtk_container_set_border_width(GTK_CONTAINER(box), 12);

	GtkWidget *entry = gtk_entry_new();
	gtk_widget_set_name(entry, "entry field");
	gtk_entry_set_text(GTK_ENTRY(entry), "initial text");
	gtk_box_pack_start(GTK_BOX(box), entry, FALSE, FALSE, 0);

	GtkWidget *label = gtk_label_new("Hello AT-SPI");
	gtk_widget_set_name(label, "status label");
	gtk_box_pack_start(GTK_BOX(box), label, FALSE, FALSE, 0);

	GtkWidget *btn = gtk_button_new_with_label("Click Me");
	gtk_widget_set_name(btn, "click button");
	g_signal_connect(btn, "clicked", G_CALLBACK(on_btn), label);
	gtk_box_pack_start(GTK_BOX(box), btn, FALSE, FALSE, 0);

	gtk_container_add(GTK_CONTAINER(win), box);
	gtk_widget_show_all(win);
	gtk_main();
	return 0;
}