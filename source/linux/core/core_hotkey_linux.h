// Declarations of the Linux hotkey-activation functions (implemented in
// core_hotkey_linux.cpp).
#pragma once

#include "../../abi.h"
#include <X11/Xlib.h>

BIF_DECL(BIF_Linux_Hotkey);

// The hotkey backend uses its OWN X11 connection (never shared with the
// window/clipboard modules) so it cannot consume events belonging to them.
// Returns nullptr (once) when no display is available.
Display *LinuxHotkeyDisplay();

// Reconcile the desired grab set with the installed one: XGrabKey the
// missing combinations, XUngrabKey the obsolete ones (Hotkey Off / disabled
// variants / keyboard-map changes).  Reports BadAccess conflicts for
// BIF_Linux_Hotkey.  Cheap (diff-based), called after every registration,
// before the main loop and on each dispatch.
void LinuxReconcileHotkeyGrabs();

// Fire hotkeys for pending key events on the dedicated connection.
void LinuxDispatchHotkeys();