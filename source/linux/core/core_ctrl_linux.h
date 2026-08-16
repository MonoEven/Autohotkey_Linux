// Declarations of the X11 control-module BIFs (implemented in
// core_ctrl_linux.cpp) for use by the LMD function table.
#pragma once

#include "../../abi.h"
#include <X11/Xlib.h>

BIF_DECL(BIF_Linux_ControlAddItem);
BIF_DECL(BIF_Linux_ControlChooseIndex);
BIF_DECL(BIF_Linux_ControlChooseString);
BIF_DECL(BIF_Linux_ControlClick);
BIF_DECL(BIF_Linux_ControlDeleteItem);
BIF_DECL(BIF_Linux_ControlFindItem);
BIF_DECL(BIF_Linux_ControlFocus);
BIF_DECL(BIF_Linux_ControlGetChecked);
BIF_DECL(BIF_Linux_ControlGetChoice);
BIF_DECL(BIF_Linux_ControlGetClassNN);
BIF_DECL(BIF_Linux_ControlGetEnabled);
BIF_DECL(BIF_Linux_ControlGetExStyle);
BIF_DECL(BIF_Linux_ControlGetFocus);
BIF_DECL(BIF_Linux_ControlGetHwnd);
BIF_DECL(BIF_Linux_ControlGetIndex);
BIF_DECL(BIF_Linux_ControlGetItems);
BIF_DECL(BIF_Linux_ControlGetPos);
BIF_DECL(BIF_Linux_ControlGetStyle);
BIF_DECL(BIF_Linux_ControlGetText);
BIF_DECL(BIF_Linux_ControlGetVisible);
BIF_DECL(BIF_Linux_ControlHide);
BIF_DECL(BIF_Linux_ControlHideDropDown);
BIF_DECL(BIF_Linux_ControlMove);
BIF_DECL(BIF_Linux_ControlSend);
BIF_DECL(BIF_Linux_ControlSendText);
BIF_DECL(BIF_Linux_ControlSetChecked);
BIF_DECL(BIF_Linux_ControlSetEnabled);
BIF_DECL(BIF_Linux_ControlSetExStyle);
BIF_DECL(BIF_Linux_ControlSetStyle);
BIF_DECL(BIF_Linux_ControlSetText);
BIF_DECL(BIF_Linux_ControlShow);
BIF_DECL(BIF_Linux_ControlShowDropDown);

// WinGetControls/WinGetControlsHwnd move here from core_win_linux.cpp.
BIF_DECL(BIF_Linux_WinGetControls);
BIF_DECL(BIF_Linux_WinGetControlsHwnd);

// Edit* / EditPaste / ListViewGetContent (virtual edit/list state).
BIF_DECL(BIF_Linux_Edit);
BIF_DECL(BIF_Linux_EditGetCurrentCol);
BIF_DECL(BIF_Linux_EditGetCurrentLine);
BIF_DECL(BIF_Linux_EditGetLine);
BIF_DECL(BIF_Linux_EditGetLineCount);
BIF_DECL(BIF_Linux_EditGetSelectedText);
BIF_DECL(BIF_Linux_EditPaste);
BIF_DECL(BIF_Linux_ListViewGetContent);

// First descendant of aParent whose class contains aClassPart
// (case-insensitive); 0 if none.  Used by StatusBarGetText/Wait.
Window LinuxFindDescendantByClass(Display *d, Window aParent, const wchar_t *aClassPart);

// Target-window/control resolution shared with the message BIFs
// (SendMessage/PostMessage): resolves per docs "Control Identifiers" and
// raises a TargetError on failure (returns false).
bool LinuxCtrlTargetEx(ResultToken &aResultToken, ExprTokenType *aParam[], int aParamCount
	, ScriptThreadSettings &aSettings, int aControlIdx, int aTitleIdx, Window &aTarget, Window &aControl);
