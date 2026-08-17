// Linux implementation of CaretGetPos.
//
// Windows reads the caret through GUITHREADINFO (hwndCaret + rcCaret).  X11
// has no cross-client caret protocol; but when the focused window belongs to
// this process's GTK3 GUI backend, the caret of the focused GtkEntry can be
// queried directly (client-side).  For any other window the position cannot
// be determined, and the docs permit returning 0 with blank output vars.
//
//   CaretFound := CaretGetPos(&OutputVarX, &OutputVarY)
//   - returns 1 when a caret position was found (relative to the active
//     window's client area, honoring CoordMode caret), 0 otherwise.
//
// The X display access lives in the GUI backend (which owns the GDK
// display); this file only marshals arguments and results.

#include "../../stdafx.h"
#include "../../script.h"
#include "../../globaldata.h"
#include "../../script_func_impl.h"

// Provided by the GTK GUI backend (script_gui_linux.cpp): returns the caret
// (insertion point) of the focused GtkEntry/GtkTextView of the focused
// top-level GTK window, in screen coordinates.  Returns true if a caret was
// found (false if no display, no focused GTK window, or no caret widget).
extern "C" bool AhkGtkCaretGetPos(int &aScreenX, int &aScreenY);

// C++ linkage, from util.h.
void CoordToScreen(int &aX, int &aY, int aWhichMode);

BIF_DECL(BIF_CaretGetPos)
{
	Var *varX = ParamIndexToOutputVar(0);
	Var *varY = ParamIndexToOutputVar(1);

	int sx = 0, sy = 0;
	bool found = AhkGtkCaretGetPos(sx, sy);
	if (!found)
	{
		if (varX) varX->Assign();
		if (varY) varY->Assign();
		_f_return_i(FALSE);
	}

	// Convert to the mode-relative origin (CoordMode caret; default is the
	// active window's client area; the GUI backend already returns
	// coordinates relative to the client area of the focused window).
	int origin_x = 0, origin_y = 0;
	CoordToScreen(origin_x, origin_y, COORD_MODE_CARET);
	sx -= origin_x;
	sy -= origin_y;

	if (varX) varX->Assign(sx);
	if (varY) varY->Assign(sy);
	_f_return_i(TRUE);
}
