// Linux stubs for built-in functions/variables not yet ported.
// These allow the core interpreter objects to link; real implementations
// will replace them as the Linux port progresses.
#include "../../stdafx.h"
#include "../../script.h"

#define LINUX_BIF_STUB(name) \
void name(BIF_DECL_PARAMS) { (void)aParam; (void)aParamCount; }

#define LINUX_BIV_STUB(name) \
void name(ResultToken &aResultToken, LPTSTR aVarName) { (void)aVarName; (void)aResultToken; }

#define LINUX_BIV_STUB_RW(name) \
LINUX_BIV_STUB(name) \
void name##_Set(ResultToken &aResultToken, LPTSTR aVarName, ExprTokenType &aValue) { (void)aVarName; (void)aValue; (void)aResultToken; }

LINUX_BIF_STUB(BIF_CaretGetPos)
LINUX_BIF_STUB(BIF_Click)
LINUX_BIF_STUB(BIF_ComObj)
LINUX_BIF_STUB(BIF_ComObjActive)
LINUX_BIF_STUB(BIF_ComObjConnect)
LINUX_BIF_STUB(BIF_ComObjFlags)
LINUX_BIF_STUB(BIF_ComObjGet)
LINUX_BIF_STUB(BIF_ComObjQuery)
LINUX_BIF_STUB(BIF_ComObjType)
LINUX_BIF_STUB(BIF_ComObjValue)
LINUX_BIF_STUB(BIF_DllCall)
LINUX_BIF_STUB(BIF_NumGet)
LINUX_BIF_STUB(BIF_NumPut)
LINUX_BIF_STUB(BIF_Reg)
LINUX_BIF_STUB(BIF_RegEx)
LINUX_BIF_STUB(BIF_RunWait)
LINUX_BIF_STUB(BIF_Sound)
LINUX_BIF_STUB(BIF_StrGetPut)
LINUX_BIF_STUB(BIF_StrPtr)
LINUX_BIF_STUB(BIF_WinExistActive)

LINUX_BIV_STUB(BIV_AhkPath)
LINUX_BIV_STUB(BIV_AhkVersion)
LINUX_BIV_STUB_RW(BIV_AllowMainWindow)
LINUX_BIV_STUB_RW(BIV_Clipboard)
LINUX_BIV_STUB(BIV_ComSpec)
LINUX_BIV_STUB_RW(BIV_CoordMode)
LINUX_BIV_STUB(BIV_Cursor)
LINUX_BIV_STUB(BIV_DateTime)
LINUX_BIV_STUB_RW(BIV_DefaultMouseSpeed)
LINUX_BIV_STUB_RW(BIV_DetectHiddenText)
LINUX_BIV_STUB_RW(BIV_DetectHiddenWindows)
LINUX_BIV_STUB(BIV_EndChar)
LINUX_BIV_STUB_RW(BIV_EventInfo)
LINUX_BIV_STUB_RW(BIV_FileEncoding)
LINUX_BIV_STUB_RW(BIV_Hotkey)
LINUX_BIV_STUB(BIV_IconFile)
LINUX_BIV_STUB_RW(BIV_IconHidden)
LINUX_BIV_STUB(BIV_IconNumber)
LINUX_BIV_STUB_RW(BIV_IconTip)
LINUX_BIV_STUB(BIV_InitialWorkingDir)
LINUX_BIV_STUB(BIV_Is64bitOS)
LINUX_BIV_STUB(BIV_IsAdmin)
LINUX_BIV_STUB(BIV_IsCompiled)
LINUX_BIV_STUB(BIV_IsCritical)
LINUX_BIV_STUB(BIV_IsPaused)
LINUX_BIV_STUB(BIV_IsSuspended)
LINUX_BIV_STUB(BIV_Language)
LINUX_BIV_STUB_RW(BIV_LastError)
LINUX_BIV_STUB(BIV_LineFile)
LINUX_BIV_STUB(BIV_LineNumber)
LINUX_BIV_STUB_RW(BIV_ListLines)
LINUX_BIV_STUB(BIV_LoopField)
LINUX_BIV_STUB(BIV_LoopFileAttrib)
LINUX_BIV_STUB(BIV_LoopFileDir)
LINUX_BIV_STUB(BIV_LoopFileExt)
LINUX_BIV_STUB(BIV_LoopFileFullPath)
LINUX_BIV_STUB(BIV_LoopFileName)
LINUX_BIV_STUB(BIV_LoopFilePath)
LINUX_BIV_STUB(BIV_LoopFileShortPath)
LINUX_BIV_STUB(BIV_LoopFileSize)
LINUX_BIV_STUB(BIV_LoopFileTime)
LINUX_BIV_STUB_RW(BIV_LoopIndex)
LINUX_BIV_STUB(BIV_LoopReadLine)
LINUX_BIV_STUB(BIV_LoopRegKey)
LINUX_BIV_STUB(BIV_LoopRegName)
LINUX_BIV_STUB(BIV_LoopRegTimeModified)
LINUX_BIV_STUB(BIV_LoopRegType)
LINUX_BIV_STUB_RW(BIV_MenuMaskKey)
LINUX_BIV_STUB(BIV_MMM_DDD)
LINUX_BIV_STUB(BIV_MyDocuments)
LINUX_BIV_STUB(BIV_Now)
LINUX_BIV_STUB(BIV_OSVersion)
LINUX_BIV_STUB(BIV_PriorHotkey)
LINUX_BIV_STUB(BIV_PriorKey)
LINUX_BIV_STUB(BIV_PtrSize)
LINUX_BIV_STUB_RW(BIV_RegView)
LINUX_BIV_STUB(BIV_ScreenDPI)
LINUX_BIV_STUB(BIV_ScreenWidth_Height)
LINUX_BIV_STUB(BIV_ScriptDir)
LINUX_BIV_STUB(BIV_ScriptFullPath)
LINUX_BIV_STUB(BIV_ScriptHwnd)
LINUX_BIV_STUB_RW(BIV_ScriptName)
LINUX_BIV_STUB_RW(BIV_SendLevel)
LINUX_BIV_STUB_RW(BIV_SendMode)
LINUX_BIV_STUB(BIV_Space_Tab)
LINUX_BIV_STUB(BIV_SpecialFolderPath)
LINUX_BIV_STUB_RW(BIV_StoreCapsLockMode)
LINUX_BIV_STUB(BIV_Temp)
LINUX_BIV_STUB(BIV_ThisFunc)
LINUX_BIV_STUB(BIV_ThisHotkey)
LINUX_BIV_STUB(BIV_TickCount)
LINUX_BIV_STUB(BIV_TimeIdle)
LINUX_BIV_STUB(BIV_TimeSincePriorHotkey)
LINUX_BIV_STUB(BIV_TimeSinceThisHotkey)
LINUX_BIV_STUB_RW(BIV_TitleMatchMode)
LINUX_BIV_STUB(BIV_TitleMatchModeSpeed)
LINUX_BIV_STUB(BIV_TrayMenu)
LINUX_BIV_STUB(BIV_UserName_ComputerName)
LINUX_BIV_STUB(BIV_WinDir)
LINUX_BIV_STUB_RW(BIV_WorkingDir)
LINUX_BIV_STUB_RW(BIV_xDelay)

// Minimal MsgBox built-in for the Linux console port.  It prints the first
// argument and returns "OK" so scripts can continue.
BIF_DECL(BIF_MsgBox)
{
	TCHAR buf[4096];
	buf[0] = L'\0';
	LPTSTR text = buf;
	if (aParamCount > 0)
	{
		size_t len = 0;
		LPTSTR str = TokenToString(*aParam[0], buf, &len);
		if (str)
			text = str;
	}
	std::printf("%ls\n", text ? text : L"");
	aResultToken.SetValue(_T("OK"));
}

// Keep the preprocessor namespace clean.
#undef LINUX_BIF_STUB
#undef LINUX_BIV_STUB
#undef LINUX_BIV_STUB_RW