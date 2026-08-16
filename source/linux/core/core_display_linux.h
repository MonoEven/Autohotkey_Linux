// Declarations of the X11 misc-module BIFs (implemented in
// core_display_linux.cpp) for use by the LMD function table.
#pragma once

#include "../../abi.h"

BIF_DECL(BIF_Linux_StatusBarGetText);
BIF_DECL(BIF_Linux_StatusBarWait);
BIF_DECL(BIF_Linux_ListVars);
BIF_DECL(BIF_Linux_ListHotkeys);
BIF_DECL(BIF_Linux_KeyHistory);
BIF_DECL(BIF_Linux_FileCreateShortcut);
BIF_DECL(BIF_Linux_FileGetShortcut);

// Title matching per SetTitleMatchMode (shared with StatusBarWait).
bool LinuxWinTitleMatches(ScriptThreadSettings &aSettings, const wchar_t *aTitle, const wchar_t *aNeedle);
