// AT-SPI accessibility backend (check0820 direction-B item 2).
//
// On a native-Wayland session X11 child-window enumeration is impossible
// (the compositor owns the window tree); the portable cross-app route is
// the AT-SPI accessibility bus (org.a11y.atspi on the at-spi D-Bus), which
// every GTK/Qt/Electron application with accessibility enabled exposes.
//
// This module is a MINIMAL AT-SPI client (libdbus):
//   - resolves the at-spi bus address via org.a11y.Bus.GetAddress,
//   - walks the desktop tree (root -> desktop frame -> app windows ->
//     widgets) using Accessible.GetChildren / GetChildAtIndex,
//   - reads Name/ChildCount properties and GetRoleName,
//   - reads component geometry (Component.GetExtents),
//   - triggers actions (Action.DoAction) and edits text
//     (EditableText.SetTextContents), the Control* primitives.
//
// It is used by the Wayland fallback path of Control*/Win* and can be
// exercised directly for diagnostics.

#pragma once

// True when the at-spi bus is reachable and the registry replied.
bool LinuxAtspiAvailable();

// Refresh the cached accessible-object table (walks the desktop tree once).
// Returns the number of accessible objects discovered.
int LinuxAtspiRefresh();

// Diagnostics: dump the tree (caption, role, path) into aDebug.
void LinuxAtspiDump(std::string &aDebug);

// Find a descendant whose accessible name equals aName (matched like
// ControlGetText listeners do: substring).  Returns >0 when found.
// aOutPath receives the D-Bus object path of the match.
bool LinuxAtspiFindByName(const char *aName, std::string &aOutPath);

// Text of an accessible object ('Text' interface GetText(0, -1)) -> aText.
bool LinuxAtspiGetText(const char *aPath, std::string &aText);

// Set the text of an editable object (EditableText.SetTextContents).
bool LinuxAtspiSetText(const char *aPath, const char *aText);

// Invoke an Action by index (0-based) or by name (Action.GetName(i));
// returns the DoAction result.
bool LinuxAtspiDoAction(const char *aPath, int aIndex, const char *aNameOrNull);