// Declarations of the Linux hotkey-activation functions (implemented in
// core_hotkey_linux.cpp) and the typed-text capture interface.
#pragma once

#include "../../abi.h"
#include <X11/Xlib.h>
#include <set>

BIF_DECL(BIF_Linux_Hotkey);

// A passive grab to install/remove: exactly one of keycode/button is set.
struct GrabSpec
{
	KeyCode keycode;      // Key grab (button == 0).
	unsigned int button;  // Mouse grab (keycode == 0): X11 button 1..9.
	unsigned int modifiers;
	bool operator<(const GrabSpec &o) const
	{
		if (keycode != o.keycode)
			return keycode < o.keycode;
		if (button != o.button)
			return button < o.button;
		return modifiers < o.modifiers;
	}
};

// The hotkey backend uses its OWN X11 connection (never shared with the
// window/clipboard modules) so it cannot consume events belonging to them.
// Returns nullptr (once) when no display is available.
Display *LinuxHotkeyDisplay();

// Reconcile the desired grab set with the installed one: XGrabKey the
// missing combinations, XUngrabKey the obsolete ones (Hotkey Off / disabled
// variants / keyboard-map changes / capture-mode changes).  Reports
// BadAccess conflicts for BIF_Linux_Hotkey.  The reconcile is lazy: it runs
// when LinuxSetReconcileDirty() has been called (the dispatch loop checks
// the flag), so hotkey-state changes must call LinuxHotkeyStateChanged().
void LinuxReconcileHotkeyGrabs();

// Fire hotkeys for pending key events on the dedicated connection.
void LinuxDispatchHotkeys();

// Mark the grab set as possibly changed (next dispatch re-reconciles).
void LinuxSetReconcileDirty();

// Central "hotkey/hotstring/suspend state changed" hook: called from
// Hotkey::ManifestAllHotkeysHotstringsHooks() (hotkey.cpp).
void LinuxHotkeyStateChanged();

// Passthrough re-injection of a grabbed key event (XTEST) with a copy-
// suppression mark; LinuxInjectMarked is the variant for generated input
// (typed-text capture forwards and hotstring replacements).
void LinuxInjectKey(Display *d, XEvent &ev);
void LinuxInjectMarked(Display *d, unsigned int aKeycode, bool aIsPress);

// Add the all-keys capture grab set (typed-text capture; see
// core_capture_linux.cpp) to aDesired.  No-op when capture is inactive.
void LinuxCaptureAddSpecs(std::set<GrabSpec> &aDesired);
