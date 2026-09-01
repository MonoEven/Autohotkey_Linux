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
// BadAccess conflicts for BIF_Linux_Hotkey. Failed grabs never enter the
// installed set; they are retried by the dispatch loop so a combination
// recovers when its former owner exits. Pass true only for the synchronous
// BIF registration call which must surface the immediate conflict.
void LinuxReconcileHotkeyGrabs(bool aReportConflict = false);

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

// Self-injection tracking (check_detail0821 §2-B + §2-C, check0901 P0-2/P1-3):
// record a key this process injected (Send/SendEvent/SendInput/SendPlay/
// SendText) with its SendLevel and transport class (SendInput = hook-unloaded,
// SendPlay = journal).  LinuxHandleKeyEvent re-injects SendInput/SendPlay
// copies so the focused window still receives them (X11 passive grabs
// redirect the injected event here) without firing own hotkeys; the rest are
// level-gated by #InputLevel / InputHook MinSendLevel via input_semantics.h.
void LinuxSelfTrack(unsigned int aKeycode, bool aIsPress, int aLevel
	, bool aIsSendInput, bool aIsSendPlay = false);
void LinuxSelfClear();
// Raw XI2 sees the event in parallel with normal grabs, so it consumes a
// dedicated copy of each self mark and cannot steal provenance from hotkeys.
bool LinuxSelfLookupRaw(unsigned int aKeycode, bool aIsPress
	, int &aLevel, bool &aIsSendInput, bool &aIsSendPlay);

// XTEST device detection + raw-event source tap (check_detail0821 §2.2-A / §3).
// The XTEST devices carry the "XTEST Device" property; raw events they produce
// have sourceid == their device id (valid from XI 2.1).  LinuxXTestTapClassify
// consumes the most recent raw record for {keycode, phase} and returns
// 1 = XTEST (injected), 0 = PHYSICAL (real press), -1 = unknown (no record).
void LinuxXI2EnumXTest(Display *d);
bool LinuxIsXTestDevice(int aSourceId);
void LinuxXTestTapRecord(unsigned int aKeycode, bool aIsPress, bool aIsXTest);
int LinuxXTestTapClassify(unsigned int aKeycode, bool aIsPress);
int LinuxXTestPrimaryDeviceId();
bool LinuxXI2SourceIdActive();
// One-shot XI 2.1 + XTEST-device probe for --diag (no raw-event subscription).
bool LinuxXI2Probe(Display *d);
