// X11 GUI helpers for the Linux port.
//
// These provide real on-screen dialogs when an X11 display is available
// (WSLg, XFCE, GNOME X11, XWayland, ...) and degrade to console I/O when
// running headless (no DISPLAY / no X server).

#pragma once

#include "../../stdafx.h"

// Returns true if an X11 display can be opened (DISPLAY set and reachable).
bool LinuxHasDisplay();

// Shows a message box.  Returns the ID of the pressed button (IDOK/IDYES/
// IDNO/IDCANCEL/IDRETRY/IDABORT/IDIGNORE) or IDTIMEOUT after aTimeout
// seconds (0 = no timeout).  Falls back to the console when headless.
int LinuxMessageBox(HWND aOwner, LPCTSTR aText, LPCTSTR aTitle, UINT aType, double aTimeout = 0);

// Shows an input dialog (X11) or reads a line from stdin (headless).
// Returns true if the user confirmed, false on cancel.  The entered text is
// stored in aBuf (null-terminated, at most aBufSize-1 chars).
bool LinuxInputBox(LPCTSTR aPrompt, LPCTSTR aTitle, LPCTSTR aDefault, LPTSTR aBuf, int aBufSize);
