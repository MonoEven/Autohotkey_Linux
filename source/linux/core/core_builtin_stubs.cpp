// Linux stubs for built-in functions/variables not yet ported.
// These allow the core interpreter objects to link; real implementations
// will replace them as the Linux port progresses.
#include "../../stdafx.h"
#include "../../script.h"
#include "../../globaldata.h"
#include <fstream>
#include <string>
#include <vector>

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
LINUX_BIV_STUB(BIV_LoopFileAttrib)
LINUX_BIV_STUB(BIV_LoopFileDir)
LINUX_BIV_STUB(BIV_LoopFileExt)
LINUX_BIV_STUB(BIV_LoopFileFullPath)
LINUX_BIV_STUB(BIV_LoopFileName)
LINUX_BIV_STUB(BIV_LoopFilePath)
LINUX_BIV_STUB(BIV_LoopFileShortPath)
LINUX_BIV_STUB(BIV_LoopFileSize)
LINUX_BIV_STUB(BIV_LoopFileTime)
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
	size_t n = mbstowcs(buf, user, 255);
	if (n == (size_t)-1)
		n = 0;
	buf[n] = L'\0';
	LPTSTR persistent = (LPTSTR)SimpleHeap::Alloc((n + 1) * sizeof(TCHAR));
	tmemcpy(persistent, buf, n + 1);
	aResultToken.SetValue(persistent, n);
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

// Loop-related built-in variables.  These use the real loop state in
// g (global_struct) so that Loop/For/Parse loops report A_Index correctly.
void BIV_LoopIndex(ResultToken &aResultToken, LPTSTR aVarName)
{
	(void)aVarName;
	aResultToken.SetValue((__int64)g->mLoopIteration);
}

void BIV_LoopIndex_Set(ResultToken &aResultToken, LPTSTR aVarName, ExprTokenType &aValue)
{
	(void)aResultToken; (void)aVarName;
	TCHAR buf[64];
	buf[0] = L'\0';
	size_t len = 0;
	LPTSTR s = TokenToString(aValue, buf, &len);
	if (s)
		g->mLoopIteration = _ttoi64(s);
}

void BIV_LoopField(ResultToken &aResultToken, LPTSTR aVarName)
{
	(void)aVarName;
	aResultToken.SetValue(g->mLoopField ? g->mLoopField : _T(""));
}

void BIV_LoopReadLine(ResultToken &aResultToken, LPTSTR aVarName)
{
	(void)aVarName;
	aResultToken.SetValue(g->mLoopReadFile && g->mLoopReadFile->mCurrentLine ? g->mLoopReadFile->mCurrentLine : _T(""));
}

// Minimal file built-in functions for the Linux console port.
static bool WideToNarrowPath(LPCTSTR aWide, char *aBuf, size_t aBufSize)
{
	if (!aWide || wcstombs(aBuf, aWide, aBufSize) == (size_t)-1)
		return false;
	return true;
}

static bool ParamToNarrowPath(ExprTokenType *aParam, char *aPath, size_t aPathSize, TCHAR *aWideBuf, size_t aWideSize)
{
	aWideBuf[0] = L'\0';
	size_t len = 0;
	LPTSTR wide = TokenToString(*aParam, aWideBuf, &len);
	if (!wide)
		wide = aWideBuf;
	return WideToNarrowPath(wide, aPath, aPathSize);
}

static void SetResultFromUtf8(ResultToken &aResultToken, const std::string &aUtf8)
{
	if (aUtf8.empty())
	{
		aResultToken.SetValue(_T(""));
		return;
	}
	std::wstring w;
	w.reserve(aUtf8.size());
	size_t i = 0;
	// Skip UTF-8 BOM if present.
	if (aUtf8.size() >= 3 && (unsigned char)aUtf8[0] == 0xEF && (unsigned char)aUtf8[1] == 0xBB && (unsigned char)aUtf8[2] == 0xBF)
		i = 3;
	while (i < aUtf8.size())
	{
		unsigned char c = (unsigned char)aUtf8[i];
		if (c < 0x80)
		{
			w += (wchar_t)c;
			++i;
		}
		else if ((c & 0xE0) == 0xC0 && i + 1 < aUtf8.size())
		{
			w += (wchar_t)(((c & 0x1F) << 6) | ((unsigned char)aUtf8[i + 1] & 0x3F));
			i += 2;
		}
		else if ((c & 0xF0) == 0xE0 && i + 2 < aUtf8.size())
		{
			w += (wchar_t)(((c & 0x0F) << 12) | (((unsigned char)aUtf8[i + 1] & 0x3F) << 6) | ((unsigned char)aUtf8[i + 2] & 0x3F));
			i += 3;
		}
		else if ((c & 0xF8) == 0xF0 && i + 3 < aUtf8.size())
		{
			w += (wchar_t)(((c & 0x07) << 18) | (((unsigned char)aUtf8[i + 1] & 0x3F) << 12) | (((unsigned char)aUtf8[i + 2] & 0x3F) << 6) | ((unsigned char)aUtf8[i + 3] & 0x3F));
			i += 4;
		}
		else
		{
			w += L'?';
			++i;
		}
	}
	LPTSTR persistent = SimpleHeap::Alloc((w.size() + 1) * sizeof(TCHAR));
	tmemcpy(persistent, w.c_str(), w.size() + 1);
	aResultToken.SetValue(persistent, w.size());
}

// Copy a wide string into SimpleHeap memory and return it via the token, so
// callers never receive a pointer into a stack buffer.
static void LinuxSetPersistentString(ResultToken &aResultToken, LPTSTR aString)
{
	if (!aString)
	{
		aResultToken.SetValue(_T(""));
		return;
	}
	size_t n = wcslen(aString);
	LPTSTR persistent = (LPTSTR)SimpleHeap::Alloc((n + 1) * sizeof(TCHAR));
	tmemcpy(persistent, aString, n + 1);
	aResultToken.SetValue(persistent, n);
}

BIF_DECL(BIF_DirCreate)
{
	if (aParamCount < 1)
		return;
	char path[4096];
	TCHAR wbuf[4096];
	if (!ParamToNarrowPath(aParam[0], path, sizeof(path), wbuf, sizeof(wbuf)))
		return;
	std::filesystem::create_directories(path);
}

BIF_DECL(BIF_DirDelete)
{
	if (aParamCount < 1)
		return;
	char path[4096];
	TCHAR wbuf[4096];
	if (!ParamToNarrowPath(aParam[0], path, sizeof(path), wbuf, sizeof(wbuf)))
		return;
	bool recurse = aParamCount > 1 && TokenToBOOL(*aParam[1]);
	try
	{
		if (recurse)
			std::filesystem::remove_all(path);
		else
			std::filesystem::remove(path);
	}
	catch (...)
	{
	}
}

BIF_DECL(BIF_DirExist)
{
	if (aParamCount < 1)
	{
		aResultToken.SetValue(_T(""));
		return;
	}
	char path[4096];
	TCHAR wbuf[4096];
	LPTSTR wide = TokenToString(*aParam[0], wbuf, nullptr);
	if (!wide)
		wide = wbuf;
	if (!WideToNarrowPath(wide, path, sizeof(path)))
	{
		aResultToken.SetValue(_T(""));
		return;
	}
	LinuxSetPersistentString(aResultToken, std::filesystem::is_directory(path) ? wide : _T(""));
}

BIF_DECL(BIF_FileAppend)
{
	if (aParamCount < 2)
		return;
	char path[4096];
	TCHAR wbuf[4096];
	if (!ParamToNarrowPath(aParam[1], path, sizeof(path), wbuf, sizeof(wbuf)))
		return;
	TCHAR text_buf[4096];
	text_buf[0] = L'\0';
	size_t len = 0;
	LPTSTR text = TokenToString(*aParam[0], text_buf, &len);
	if (!text)
		text = text_buf;
	std::ofstream ofs(path, std::ios::app | std::ios::binary);
	if (ofs)
	{
		char mb[4096];
		size_t n = wcstombs(mb, text, sizeof(mb));
		if (n != (size_t)-1)
			ofs.write(mb, n);
	}
}

BIF_DECL(BIF_FileDelete)
{
	if (aParamCount < 1)
		return;
	char path[4096];
	TCHAR wbuf[4096];
	if (!ParamToNarrowPath(aParam[0], path, sizeof(path), wbuf, sizeof(wbuf)))
		return;
	std::filesystem::remove(path);
}

BIF_DECL(BIF_FileExist)
{
	if (aParamCount < 1)
	{
		aResultToken.SetValue(_T(""));
		return;
	}
	char path[4096];
	TCHAR wbuf[4096];
	LPTSTR wide = TokenToString(*aParam[0], wbuf, nullptr);
	if (!wide)
		wide = wbuf;
	if (!WideToNarrowPath(wide, path, sizeof(path)))
	{
		aResultToken.SetValue(_T(""));
		return;
	}
	LinuxSetPersistentString(aResultToken, std::filesystem::is_regular_file(path) ? wide : _T(""));
}

BIF_DECL(BIF_FileRead)
{
	if (aParamCount < 1)
	{
		aResultToken.SetValue(_T(""));
		return;
	}
	char path[4096];
	TCHAR wbuf[4096];
	if (!ParamToNarrowPath(aParam[0], path, sizeof(path), wbuf, sizeof(wbuf)))
	{
		aResultToken.SetValue(_T(""));
		return;
	}
	FILE *f = fopen(path, "rb");
	if (!f)
	{
		aResultToken.SetValue(_T(""));
		return;
	}
	fseek(f, 0, SEEK_END);
	long size = ftell(f);
	fseek(f, 0, SEEK_SET);
	std::string content;
	if (size > 0)
	{
		content.resize((size_t)size);
		size_t got = fread(&content[0], 1, (size_t)size, f);
		content.resize(got);
	}
	fclose(f);
	SetResultFromUtf8(aResultToken, content);
}
// Keep the preprocessor namespace clean.
#undef LINUX_BIF_STUB
#undef LINUX_BIV_STUB
#undef LINUX_BIV_STUB_RW