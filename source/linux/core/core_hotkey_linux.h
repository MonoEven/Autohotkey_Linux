// Declarations of the Linux hotkey-activation functions (implemented in
// core_hotkey_linux.cpp).
#pragma once

#include "../../abi.h"
#include <X11/Xlib.h>

BIF_DECL(BIF_Linux_Hotkey);

// (Re)establish XGrabKey grabs for all registered hotkeys.
void LinuxUpdateHotkeyGrabs(Display *d);
// Fire hotkeys for pending X key events (main loop and MsgSleep).
void LinuxDispatchHotkeys(Display *d);
