#pragma once

// Linux GTK3 GUI backend - public integration hooks.
//
// Declares the small set of functions the Linux core (message pump, main
// wait loop, headless guards) needs from the GTK3 GUI module without pulling
// <gtk/gtk.h> into every translation unit.  The heavy GuiType/GuiControlType
// implementation lives in script_gui_linux.cpp; the UserMenu GTK backend
// lives in script_menu_linux.cpp.

namespace ahk_gtk
{
	// True once GTK has been initialised successfully (i.e. a display
	// connection - X11 or Wayland - was available).  When false all Gui/Menu
	// creation fails with a clear error instead of crashing, so scripts run
	// unchanged on headless systems.
	bool GtkAvailable();

	// Pump the GTK/GLib event sources AND deliver queued GUI/menu events
	// (button clicks, window close, menu item activation...).  Safe to call
	// frequently; a no-op when GTK is unavailable.
	void GtkPump();

	// True when at least one Gui window is currently visible.  Used by the
	// main wait loop to keep a non-persistent GUI script alive while a window
	// is showing, and to exit as soon as the last window closes.
	bool GuiWindowsVisible();
}
