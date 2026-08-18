// core_capture_linux.h
#pragma once
#include <X11/Xlib.h>
#include <set>

struct GrabSpec;

// True when typed-text capture is active (at least one enabled hotstring):
// the hotkey backend then additionally grabs every key so this engine can
// watch and filter the text stream.
bool LinuxCaptureActive();

// Add the all-keys passive-grab set to aDesired (implemented in
// core_hotkey_linux.cpp, which owns the modifier masks).
void LinuxCaptureAddSpecs(std::set<GrabSpec> &aDesired);

// Feed one grabbed key event.  Returns true when the event was consumed
// (held toward a match, matched, or forwarded by the engine); false when
// the normal hotkey flow should handle it.
bool LinuxCaptureKeyEvent(Display *d, XEvent &ev);

// Hotstring state changed: recompute the active flag.
void LinuxCaptureStateChanged();

// Keyboard map changed: the grab set must be rebuilt.
void LinuxCaptureMappingNotify();
