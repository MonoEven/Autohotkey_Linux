// Declarations of the X11 input-module BIFs (implemented in
// core_input_linux.cpp) for use by the LMD function table.
#pragma once

#include "../../abi.h"

BIF_DECL(BIF_Linux_Send);
BIF_DECL(BIF_Linux_SendEvent);
BIF_DECL(BIF_Linux_SendInput);
BIF_DECL(BIF_Linux_SendPlay);
BIF_DECL(BIF_Linux_SendText);
BIF_DECL(BIF_Linux_MouseMove);
BIF_DECL(BIF_Linux_MouseClick);
BIF_DECL(BIF_Linux_MouseClickDrag);
BIF_DECL(BIF_Linux_MouseGetPos);
BIF_DECL(BIF_Linux_KeyWait);
BIF_DECL(BIF_Linux_BlockInput);
BIF_DECL(BIF_Linux_InstallKeybdHook);
BIF_DECL(BIF_Linux_InstallMouseHook);
BIF_DECL(BIF_Linux_SetCapsLockState);
BIF_DECL(BIF_Linux_SetNumLockState);
BIF_DECL(BIF_Linux_SetScrollLockState);
