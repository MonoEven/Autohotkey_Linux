// Linux stubs for built-in functions/variables not yet ported.
// These allow the core interpreter objects to link; real implementations
// will replace them as the Linux port progresses.
#include "../../stdafx.h"
#include "../../script.h"
#include "../../globaldata.h"

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
LINUX_BIV_STUB_RW(BIV_AllowMainWindow)
LINUX_BIV_STUB_RW(BIV_Clipboard)
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
LINUX_BIV_STUB(BIV_PriorHotkey)
LINUX_BIV_STUB(BIV_PriorKey)
LINUX_BIV_STUB_RW(BIV_RegView)
LINUX_BIV_STUB(BIV_ScreenDPI)
LINUX_BIV_STUB(BIV_ScreenWidth_Height)
LINUX_BIV_STUB(BIV_ScriptHwnd)
LINUX_BIV_STUB_RW(BIV_SendLevel)
LINUX_BIV_STUB_RW(BIV_SendMode)
LINUX_BIV_STUB(BIV_SpecialFolderPath)
LINUX_BIV_STUB_RW(BIV_StoreCapsLockMode)
LINUX_BIV_STUB(BIV_ThisFunc)
LINUX_BIV_STUB(BIV_ThisHotkey)
LINUX_BIV_STUB(BIV_TimeIdle)
LINUX_BIV_STUB(BIV_TimeSincePriorHotkey)
LINUX_BIV_STUB(BIV_TimeSinceThisHotkey)
LINUX_BIV_STUB_RW(BIV_TitleMatchMode)
LINUX_BIV_STUB(BIV_TitleMatchModeSpeed)
LINUX_BIV_STUB(BIV_TrayMenu)
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

// A small set of built-in variables implemented for the Linux console port.
BIV_DECL_R(BIV_AhkVersion) { aResultToken.SetValue(_T("2.0.26")); }
BIV_DECL_R(BIV_ComSpec) { aResultToken.SetValue(_T("/bin/sh")); }
BIV_DECL_R(BIV_InitialWorkingDir) { aResultToken.SetValue(g_WorkingDirOrig ? g_WorkingDirOrig : _T("")); }
BIV_DECL_R(BIV_Is64bitOS) { aResultToken.SetValue((__int64)(sizeof(void*) == 8 ? 1 : 0)); }
BIV_DECL_R(BIV_IsAdmin) { aResultToken.SetValue((__int64)0); }
BIV_DECL_R(BIV_IsCompiled) { aResultToken.SetValue((__int64)0); }
BIV_DECL_R(BIV_OSVersion) { aResultToken.SetValue(_T("Linux")); }
BIV_DECL_R(BIV_PtrSize) { aResultToken.SetValue((__int64)sizeof(void*)); }
BIV_DECL_R(BIV_ScriptDir) { aResultToken.SetValue(g_script.mFileDir ? g_script.mFileDir : _T("")); }
BIV_DECL_R(BIV_ScriptFullPath) { aResultToken.SetValue(g_script.mFileSpec ? g_script.mFileSpec : _T("")); }
void BIV_ScriptName(ResultToken &aResultToken, LPTSTR aVarName) { (void)aVarName; aResultToken.SetValue(g_script.mFileName ? g_script.mFileName : _T("")); }
void BIV_ScriptName_Set(ResultToken &aResultToken, LPTSTR aVarName, ExprTokenType &aValue) { (void)aResultToken; (void)aVarName; (void)aValue; }
BIV_DECL_R(BIV_Space_Tab) { aResultToken.SetValue(_tcsicmp(aVarName, _T("Tab")) == 0 ? _T("\t") : _T(" ")); }
BIV_DECL_R(BIV_Temp) { aResultToken.SetValue(_T("/tmp")); }
BIV_DECL_R(BIV_TickCount) { aResultToken.SetValue((__int64)GetTickCount()); }
BIV_DECL_R(BIV_UserName_ComputerName)
{
	const char *user = std::getenv("USER");
	if (!user)
		user = "user";
	wchar_t buf[256];
	mbstowcs(buf, user, 255);
	buf[255] = L'\0';
	aResultToken.SetValue(buf);
}
BIV_DECL_R(BIV_WinDir) { aResultToken.SetValue(_T("/usr")); }
void BIV_WorkingDir(ResultToken &aResultToken, LPTSTR aVarName) { (void)aVarName; aResultToken.SetValue(g_WorkingDir.GetString()); }
void BIV_WorkingDir_Set(ResultToken &aResultToken, LPTSTR aVarName, ExprTokenType &aValue)
{
	(void)aResultToken; (void)aVarName;
	TCHAR buf[4096];
	buf[0] = L'\0';
	size_t len = 0;
	LPTSTR s = TokenToString(aValue, buf, &len);
	if (s)
		SetWorkingDir(s);
}

// Keep the preprocessor namespace clean.
#undef LINUX_BIF_STUB
#undef LINUX_BIV_STUB
#undef LINUX_BIV_STUB_RW