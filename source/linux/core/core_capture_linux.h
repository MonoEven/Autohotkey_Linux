// core_capture_linux.h
#pragma once
#include <X11/Xlib.h>
#include <set>
#include "input_event.h"

struct GrabSpec;

// Capture state/mode. Hotstrings and visible InputHook use XI2.1 raw events;
// only an InputHook which asks for suppression retains the compatibility
// passive-grab lane.
bool LinuxCaptureActive();
bool LinuxCaptureNeedsGrabs();
bool LinuxCaptureUsesRaw();
bool LinuxCaptureKeycodeNeedsGrab(Display *d, KeyCode aKeycode);
void LinuxCaptureGrabKeycodes(Display *d, std::set<KeyCode> &aOut);

// Add the all-keys passive-grab set to aDesired (implemented in
// core_hotkey_linux.cpp, which owns the modifier masks).
void LinuxCaptureAddSpecs(std::set<GrabSpec> &aDesired);

// Feed one grabbed key event.  aSelfLevel is the SendLevel of a self-injected
// event (>= 0) or -1 for physical/other-client input; the InputHook MinSendLevel
// (I option) filters self-injected events by it (check_detail0821 §2-C).
// Returns true when the event was consumed (held toward a match, matched, or
// forwarded by the engine); false when the normal hotkey flow should handle it.
bool LinuxCaptureKeyEvent(Display *d, XEvent &ev, int aSelfLevel = -1);

// Feed one XI2 raw key event (physical or injected). aCoreState is an X11
// core modifier/group state synthesized at event time. Raw observation never
// suppresses the original event; Hotstring replacement uses backspacing.
void LinuxCaptureRawKeyEvent(Display *d, KeyCode aKeycode, bool aIsPress,
	Time aTime, unsigned int aCoreState, int aSelfLevel, bool aIsSendInput,
	AhkInputSource aSource, uint32_t aDeviceId);

// Hotstring state changed: recompute the active flag.
void LinuxCaptureStateChanged();

// Keyboard map changed: the grab set must be rebuilt.
void LinuxCaptureMappingNotify();
void LinuxCaptureKeymapChanged();

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
