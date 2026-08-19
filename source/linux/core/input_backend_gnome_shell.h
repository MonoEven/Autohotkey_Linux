#pragma once

#include "../../stdafx.h"

// GNOME Shell extension backend (GNOME 49 Wayland exclusive hotkeys).
//
// The companion "thin" extension (extension/ in the repo) owns
// grab_accelerator()+allowKeybinding() inside the compositor and exposes a
// minimal D-Bus broker on the session bus:
//
//   io.github.autohotkey.GlobalHotkeys1
//     Register(s id, s accelerator, u flags) -> b
//     RegisterMany(as ids, as accelerators, au flags) -> ab
//     Unregister(s id) -> b
//     UnregisterMany(as ids) -> b
//     ClearOwner() -> b          (owner = caller's unique bus name)
//     signal Activated(s id, u timestamp)    (directed to the owner)
//     signal Deactivated(s id, u timestamp)  (directed to the owner)
//
// This client talks to it over libdbus.  The extension never parses AHK,
// never executes commands and never forks; it only maps action id <-> AHK
// hotkey id, and it ungrasps everything on disable/unload (fail-open).
//
// Hardening (check0819): ids are owner-scoped ("<unique-bus-name>/<hotkey>",
// enforced by the extension's "<owner>/" prefix check); signals are filtered
// on sender/path/member and only Activated is consumed; ClearOwner takes no
// owner argument; registration uses the batch methods; the event queue is
// dynamic with overflow counting; per-call timeouts are bounded (3 s).

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
