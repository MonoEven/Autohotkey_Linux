/* gtk_atspi_test.c -- GTK3 test app for the AT-SPI Control* E2E (check0820).
 *
 * Window title "AHK AT-SPI Test"; label accessible name "Hello AT-SPI";
 * button "Push Me" (action name "click") -> label becomes "clicked-yes".
 *
 * Build on the GNOME 49 VM:
 *   gcc gtk_atspi_test.c -o /tmp/gtk-atspi-test $(pkg-config --cflags --libs gtk+-3.0)
 */
#include <gtk/gtk.h>

static GtkWidget *label;

static void on_click(GtkButton *b, gpointer d)
{
	(void)b; (void)d;
	gtk_label_set_text(GTK_LABEL(label), "clicked-yes");
}

int main(int argc, char **argv)
{
	gtk_init(&argc, &argv);
	GtkWidget *win = gtk_window_new(GTK_WINDOW_TOPLEVEL);
	gtk_window_set_title(GTK_WINDOW(win), "AHK AT-SPI Test");
	gtk_container_set_border_width(GTK_CONTAINER(win), 12);
	GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
	label = gtk_label_new("Hello AT-SPI");
	GtkWidget *btn = gtk_button_new_with_label("Push Me");
	gtk_widget_set_name(label, "hello-atspi");
	gtk_widget_set_name(btn, "push-me");
	g_signal_connect(btn, "clicked", G_CALLBACK(on_click), NULL);
	gtk_box_pack_start(GTK_BOX(box), label, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(box), btn, FALSE, FALSE, 0);
	gtk_container_add(GTK_CONTAINER(win), box);
	gtk_widget_show_all(win);
	g_signal_connect(win, "destroy", G_CALLBACK(gtk_main_quit), NULL);
	gtk_main();
	return 0;
}