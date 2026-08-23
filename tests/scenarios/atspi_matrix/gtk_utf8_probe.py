#!/usr/bin/env python3
"""Independent GTK3/AT-SPI UTF-8 oracle for the scenario runner."""
import gi

gi.require_version("Gtk", "3.0")
from gi.repository import GLib, Gtk

window = Gtk.Window(title="AHK AT-SPI UTF-8 probe")
entry = Gtk.Entry()
entry.set_text("你好-AT-SPI")
entry.get_accessible().set_name("AHK-UTF8-ENTRY")
window.add(entry)
window.connect("destroy", Gtk.main_quit)
window.show_all()
GLib.timeout_add_seconds(30, Gtk.main_quit)
Gtk.main()
