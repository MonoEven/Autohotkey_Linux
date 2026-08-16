// Declarations of the Linux timer-module functions (implemented in
// core_timer_linux.cpp).
#pragma once

#include "../../abi.h"

BIF_DECL(BIF_Linux_SetTimer);
BIF_DECL(BIF_Linux_ToolTip);

// Fire due script timers (port of application.cpp CheckScriptTimers).
bool LinuxCheckScriptTimers();
// Main wait loop: fires timers until the script requests exit.
void LinuxRunMainLoop();
