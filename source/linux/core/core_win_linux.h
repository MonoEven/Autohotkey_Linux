// Declarations of the X11 window-module BIFs (implemented in core_win_linux.cpp)
// for use by the LMD function table in core_mdfunc_linux.cpp.
#pragma once

#include "../../abi.h"
#include <X11/Xlib.h>

BIF_DECL(BIF_Linux_WinGetTitle);
BIF_DECL(BIF_Linux_WinGetClass);
BIF_DECL(BIF_Linux_WinGetPID);
BIF_DECL(BIF_Linux_WinGetProcessName);
BIF_DECL(BIF_Linux_WinGetProcessPath);
BIF_DECL(BIF_Linux_WinGetID);
BIF_DECL(BIF_Linux_WinGetIDLast);
BIF_DECL(BIF_Linux_WinGetCount);
BIF_DECL(BIF_Linux_WinGetList);
BIF_DECL(BIF_Linux_WinGetPos);
BIF_DECL(BIF_Linux_WinGetClientPos);
BIF_DECL(BIF_Linux_WinGetMinMax);
BIF_DECL(BIF_Linux_WinGetStyle);
BIF_DECL(BIF_Linux_WinGetExStyle);
BIF_DECL(BIF_Linux_WinGetText);
BIF_DECL(BIF_Linux_WinGetTransparent);
BIF_DECL(BIF_Linux_WinGetTransColor);
BIF_DECL(BIF_Linux_WinGetControls);
BIF_DECL(BIF_Linux_WinGetControlsHwnd);
BIF_DECL(BIF_Linux_WinActivate);
BIF_DECL(BIF_Linux_WinActivateBottom);
BIF_DECL(BIF_Linux_WinClose);
BIF_DECL(BIF_Linux_WinKill);
BIF_DECL(BIF_Linux_WinMove);
BIF_DECL(BIF_Linux_WinRedraw);
BIF_DECL(BIF_Linux_WinHide);
BIF_DECL(BIF_Linux_WinShow);
BIF_DECL(BIF_Linux_WinMinimize);
BIF_DECL(BIF_Linux_WinMaximize);
BIF_DECL(BIF_Linux_WinRestore);
BIF_DECL(BIF_Linux_WinMoveTop);
BIF_DECL(BIF_Linux_WinMoveBottom);
BIF_DECL(BIF_Linux_WinMinimizeAll);
BIF_DECL(BIF_Linux_WinMinimizeAllUndo);
BIF_DECL(BIF_Linux_WinSetTitle);
BIF_DECL(BIF_Linux_WinSetAlwaysOnTop);
BIF_DECL(BIF_Linux_WinSetTransparent);
BIF_DECL(BIF_Linux_WinSetTransColor);
BIF_DECL(BIF_Linux_WinSetEnabled);
BIF_DECL(BIF_Linux_WinSetStyle);
BIF_DECL(BIF_Linux_WinSetExStyle);
BIF_DECL(BIF_Linux_WinWait);
BIF_DECL(BIF_Linux_WinWaitActive);
BIF_DECL(BIF_Linux_WinWaitNotActive);
BIF_DECL(BIF_Linux_WinWaitClose);
BIF_DECL(BIF_Linux_GroupAdd);
BIF_DECL(BIF_Linux_GroupActivate);
BIF_DECL(BIF_Linux_GroupClose);
BIF_DECL(BIF_Linux_GroupDeactivate);

// The current input-focus top-level window (0 if none).
Window LinuxX11ActiveWindow();
