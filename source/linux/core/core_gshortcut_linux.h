#pragma once

#include "../../stdafx.h"

// Engage/disengage the portal backend.  Called from the hotkey backend on
// every hotkey state change (LinuxHotkeyStateChanged).  Decides internally
// whether the portal path is applicable (Wayland without an X display, or
// AHK_FORCE_GLOBAL_SHORTCUTS=1 behind XWayland).
void LinuxGShortcutSync();

// Pump the portal D-Bus connection.  Must be called on the main thread from
// the event pump (MsgSleep / main loop), like LinuxDispatchHotkeys, so
// Activated signals can fire hotkeys in a new thread safely.
void LinuxGShortcutDispatch();

// Shut the portal session down (script exit).
void LinuxGShortcutShutdown();

// A human-readable failure reason (empty when the last operation succeeded).
const wchar_t *LinuxGShortcutLastError();

// True when the portal backend is currently bound (some shortcuts active).
bool LinuxGShortcutActive();
