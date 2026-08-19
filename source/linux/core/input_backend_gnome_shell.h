#pragma once

#include "../../stdafx.h"

// GNOME Shell extension backend (GNOME 49 Wayland exclusive hotkeys).
//
// The companion "thin" extension (extension/ in the repo) owns
// grab_accelerator()+allowKeybinding() inside the compositor and exposes a
// minimal D-Bus broker on the session bus:
//
//   io.github.autohotkey.GlobalHotkeys1
//     Register(u id, s accelerator, u flags) -> b
//     Unregister(u id) -> b
//     ClearOwner(s owner)
//     signal Activated(u id, u timestamp)
//     signal Deactivated(u id, u timestamp)
//
// This client talks to it over libdbus.  The extension never parses AHK,
// never executes commands and never forks; it only maps action id <-> AHK
// hotkey id, and it ungrasps everything on disable/unload (fail-open).

// True when the extension's well-known name is on the session bus.
// Pure probe - never opens a connection of its own when it can avoid it.
bool LinuxGnomeShellAvailable();

// Engage/disengage the backend.  Called from the input-backend router
// (input_backend.cpp) on every hotkey state change.  Keeps the desired
// registrations in sync with the extension (add/remove only).
void LinuxGnomeShellSync();

// Pump the D-Bus connection (Activated/Deactivated -> fire hotkeys).
// Must be called on the main thread from the event pump, like the portal
// backend's dispatch.
void LinuxGnomeShellDispatch();

// Shut the session down (script exit).
void LinuxGnomeShellShutdown();

// Human-readable failure reason (empty when the last operation succeeded).
const wchar_t *LinuxGnomeShellLastError();

// True when the backend is connected and at least one shortcut is registered.
bool LinuxGnomeShellActive();
