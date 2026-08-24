// AT-SPI accessibility backend (check0820 direction-B item 2).
//
// On a native-Wayland session X11 child-window enumeration is impossible
// (the compositor owns the window tree); the portable cross-app route is
// the AT-SPI accessibility bus (org.a11y.atspi on the at-spi D-Bus), which
// every GTK/Qt/Electron application with accessibility enabled exposes.
//
// This module is a MINIMAL AT-SPI client (libdbus):
//   - resolves the at-spi bus address via org.a11y.Bus.GetAddress,
//   - bulk-loads each application's realized objects with Cache.GetItems
//     (new signature), falling back per app to Accessible.GetChildren,
//   - reads Name/ChildCount properties and GetRoleName when fallback/enrichment
//     is required,
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
// aWindowTitle (optional) limits the search to the application subtree that
// owns the window whose accessible name equals it, so same-named controls in
// different applications do not cross-match (M5-C WinTitle limiting).
bool LinuxAtspiFindByName(const char *aName, std::string &aOutPath
	, const char *aWindowTitle = nullptr);

// Text of an accessible object ('Text' interface GetText(0, -1)) -> aText.
bool LinuxAtspiGetText(const char *aPath, std::string &aText);

// Set the text of an editable object (EditableText.SetTextContents).
bool LinuxAtspiSetText(const char *aPath, const char *aText);

// Invoke an Action by index (0-based) or by name (Action.GetName(i));
// returns the DoAction result.
bool LinuxAtspiDoAction(const char *aPath, int aIndex, const char *aNameOrNull);

// Selection interface. Items are returned in Accessible child order;
// SelectChild uses a zero-based index, or -1 to ClearSelection. GetSelected
// returns true for a supported Selection object and sets aIndex=-1 when empty.
bool LinuxAtspiSelectionGetItems(const char *aPath, std::vector<std::string> &aItems);
bool LinuxAtspiSelectionSelect(const char *aPath, int aZeroBasedIndex);
bool LinuxAtspiSelectionGetSelected(const char *aPath, int &aZeroBasedIndex, std::string &aName);

// Value interface (org.a11y.atspi.Value CurrentValue readwrite double). The
// optional text is Value.Text when available, otherwise a stable numeric form.
bool LinuxAtspiGetValue(const char *aPath, double &aValue, std::string *aText = nullptr);
bool LinuxAtspiSetValue(const char *aPath, double aValue);