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

// Keysym -> Unicode character (ASCII/Latin-1 direct, Unicode keysym range
// 0x01000000.. by subtraction, \r/\t/\b for the named keys).  0 = not text.
// Used by the hotstring capture and InputHook character streams so CJK and
// accented text typed through Unicode keysyms (IME-composed keys, borrowed
// keycodes from the Send engine) can be matched and notified.
wchar_t LinuxCharFromKeySym(KeySym aKs);

// Fire the queued InputHook notifications (OnChar/OnKeyDown/OnKeyUp).  The
// capture engine only QUEUES notifications while feeding raw X events; the
// script callbacks are invoked from the main-loop dispatch (this function,
// called by LinuxDispatchInputHook) because invoking them from the native
// capture dispatch would re-enter the interpreter.  A no-op when the input
// ended before the dispatch ran (its object may already be released).
void LinuxCaptureDispatchInputNotifies();
