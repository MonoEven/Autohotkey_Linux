// Linux stubs for built-in functions/variables not yet ported.
// These allow the core interpreter objects to link; real implementations
// will replace them as the Linux port progresses.
#include "../../stdafx.h"
#include "../../script.h"
#include "../../globaldata.h"
#include "../../script_func_impl.h"
#include "../../window.h" // For MsgBox() and dialog size constants.
#include <fstream>
#include <string>
#include <vector>

// Real implementations from window.cpp / script2.cpp (declared here because
// they have no header declaration on the Windows build either).
ResultType MsgBoxParseOptions(LPCTSTR aOptions, int &aType, double &aTimeout, HWND &aOwner);
LPTSTR MsgBoxResultString(int aResult);
void ScriptSleep(int aDelay);

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
LINUX_BIF_STUB(BIF_Reg)
LINUX_BIF_STUB(BIF_Sound)

// WinExist/WinActive: no X11 window backend yet, so no window ever matches.
// Per the docs both return an empty string when no window is found.
BIF_DECL(BIF_WinExistActive)
{
	(void)aParam;
	(void)aParamCount;
	aResultToken.SetValue(_T(""));
}

LINUX_BIV_STUB(BIV_AhkPath)
LINUX_BIV_STUB_RW(BIV_AllowMainWindow)
LINUX_BIV_STUB_RW(BIV_CoordMode)
LINUX_BIV_STUB(BIV_Cursor)
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
// --- Loop Files built-in variables (mirrors lib/vars.cpp implementations) ---

static void LinuxFixLoopFilePath(LPTSTR aBuf, LPTSTR aPattern)
{
	int count = 0;
	if (*aPattern == '.')
	{
		if (!aPattern[1])
			count = 1; // aBuf "x\y\y" should be "x\y" for "x\y\.".
		else if (aPattern[1] == '.' && !aPattern[2])
			count = 2; // aBuf "x\y\x" should be "x" for "x\y\..".
	}
	for (; count > 0; --count)
	{
		LPTSTR end = _tcsrchr(aBuf, '\\');
		if (!end)
			end = _tcsrchr(aBuf, '/');
		if (end)
			*end = '\0';
	}
}

static void LinuxReturnLoopFilePath(ResultToken &aResultToken, LPTSTR aPattern, LPTSTR aPrefix, size_t aPrefixLen, LPTSTR aSuffix, size_t aSuffixLen)
{
	if (!TokenSetResult(aResultToken, nullptr, aPrefixLen + aSuffixLen))
		return;
	aResultToken.symbol = SYM_STRING;
	LPTSTR buf = aResultToken.marker;
	tmemcpy(buf, aPrefix, aPrefixLen);
	tmemcpy(buf + aPrefixLen, aSuffix, aSuffixLen + 1); // +1 for \0.
	LinuxFixLoopFilePath(buf, aPattern);
	aResultToken.marker_length = -1;
}

void BIV_LoopFileName(ResultToken &aResultToken, LPTSTR aVarName)
{
	LPTSTR filename = _T(""); // Set default.
	if (g->mLoopFile)
	{
		if (ctoupper(aVarName[10]) != 'S' || !*(filename = g->mLoopFile->cAlternateFileName))
			filename = g->mLoopFile->cFileName;
	}
	aResultToken.SetValue(filename);
}

void BIV_LoopFileExt(ResultToken &aResultToken, LPTSTR aVarName)
{
	(void)aVarName;
	LPTSTR file_ext = _T(""); // Set default.
	if (g->mLoopFile)
	{
		if (file_ext = _tcsrchr(g->mLoopFile->cFileName, '.'))
			++file_ext;
		else
			file_ext = _T("");
	}
	aResultToken.SetValue(file_ext);
}

void BIV_LoopFileDir(ResultToken &aResultToken, LPTSTR aVarName)
{
	(void)aVarName;
	if (!g->mLoopFile)
	{
		aResultToken.SetValue(_T(""));
		return;
	}
	LoopFilesStruct &lfs = *g->mLoopFile;
	LPTSTR dir_end = lfs.file_path + lfs.dir_length; // Start of the filename.
	size_t suffix_length = dir_end - lfs.file_path_suffix; // Directory names\ added since the loop started.
	size_t total_length = lfs.orig_dir_length + suffix_length;
	if (total_length)
		--total_length; // Omit the trailing slash.
	if (!TokenSetResult(aResultToken, nullptr, total_length))
		return;
	aResultToken.symbol = SYM_STRING;
	LPTSTR buf = aResultToken.marker;
	tmemcpy(buf, lfs.orig_dir, lfs.orig_dir_length);
	tmemcpy(buf + lfs.orig_dir_length, lfs.file_path_suffix, suffix_length);
	buf[total_length] = '\0';
}

void BIV_LoopFilePath(ResultToken &aResultToken, LPTSTR aVarName)
{
	(void)aVarName;
	if (!g->mLoopFile)
	{
		aResultToken.SetValue(_T(""));
		return;
	}
	LoopFilesStruct &lfs = *g->mLoopFile;
	LinuxReturnLoopFilePath(aResultToken, lfs.pattern
		, lfs.orig_dir, lfs.orig_dir_length
		, lfs.file_path_suffix, lfs.file_path_length - (lfs.file_path_suffix - lfs.file_path));
}

void BIV_LoopFileFullPath(ResultToken &aResultToken, LPTSTR aVarName)
{
	(void)aVarName;
	if (!g->mLoopFile)
	{
		aResultToken.SetValue(_T(""));
		return;
	}
	LoopFilesStruct &lfs = *g->mLoopFile;
	LinuxReturnLoopFilePath(aResultToken, lfs.pattern
		, lfs.long_dir, lfs.long_dir_length
		, lfs.file_path_suffix, lfs.file_path_length - (lfs.file_path_suffix - lfs.file_path));
}

void BIV_LoopFileShortPath(ResultToken &aResultToken, LPTSTR aVarName)
{
	(void)aVarName;
	if (!g->mLoopFile)
	{
		aResultToken.SetValue(_T(""));
		return;
	}
	LoopFilesStruct &lfs = *g->mLoopFile;
	LPTSTR name = *lfs.cAlternateFileName ? lfs.cAlternateFileName : lfs.cFileName;
	LinuxReturnLoopFilePath(aResultToken, lfs.pattern
		, lfs.short_path, lfs.short_path_length
		, name, _tcslen(name));
}

void BIV_LoopFileTime(ResultToken &aResultToken, LPTSTR aVarName)
{
	LPTSTR target_buf = aResultToken.buf;
	*target_buf = '\0'; // Set default.
	if (g->mLoopFile)
	{
		FILETIME ft;
		switch (ctoupper(aVarName[14])) // A_LoopFileTime[A]ccessed / [C]reated / [M]odified
		{
		case 'M': ft = g->mLoopFile->ftLastWriteTime; break;
		case 'C': ft = g->mLoopFile->ftCreationTime; break;
		default: ft = g->mLoopFile->ftLastAccessTime;
		}
		FileTimeToYYYYMMDD(target_buf, ft, true);
	}
	aResultToken.SetValue(target_buf);
}

void BIV_LoopFileAttrib(ResultToken &aResultToken, LPTSTR aVarName)
{
	(void)aVarName;
	LPTSTR target_buf = aResultToken.buf;
	*target_buf = '\0'; // Set default.
	if (g->mLoopFile)
		FileAttribToStr(target_buf, g->mLoopFile->dwFileAttributes);
	aResultToken.SetValue(target_buf);
}

void BIV_LoopFileSize(ResultToken &aResultToken, LPTSTR aVarName)
{
	if (g->mLoopFile)
	{
		ULARGE_INTEGER ul;
		ul.HighPart = g->mLoopFile->nFileSizeHigh;
		ul.LowPart = g->mLoopFile->nFileSizeLow;
		int divider;
		switch (ctoupper(aVarName[14])) // A_LoopFileSize[K/M]B
		{
		case 'K': divider = 1024; break;
		case 'M': divider = 1024 * 1024; break;
		default: divider = 0;
		}
		aResultToken.SetValue((__int64)(divider ? ((unsigned __int64)ul.QuadPart / divider) : ul.QuadPart));
		return;
	}
	aResultToken.SetValue(_T(""));
}
LINUX_BIV_STUB(BIV_LoopRegKey)
LINUX_BIV_STUB(BIV_LoopRegName)
LINUX_BIV_STUB(BIV_LoopRegTimeModified)
LINUX_BIV_STUB(BIV_LoopRegType)
LINUX_BIV_STUB_RW(BIV_MenuMaskKey)
LINUX_BIV_STUB(BIV_MyDocuments)
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

// MsgBox built-in.  Uses the real MsgBox machinery from window.cpp/script2.cpp
// (options/title/timeout parsing, button result strings); the compat
// MessageBox underneath shows a real X11 dialog when a display is available
// and falls back to printing to the console when headless.
BIF_DECL(BIF_MsgBox)
{
	TCHAR text_buf[MSGBOX_TEXT_SIZE];
	text_buf[0] = L'\0';
	LPTSTR text = aParamCount > 0 ? TokenToString(*aParam[0], text_buf, nullptr) : nullptr;
	TCHAR title_buf[DIALOG_TITLE_SIZE];
	title_buf[0] = L'\0';
	LPTSTR title = aParamCount > 1 ? TokenToString(*aParam[1], title_buf, nullptr) : nullptr;
	TCHAR options_buf[256];
	options_buf[0] = L'\0';
	LPTSTR options = aParamCount > 2 ? TokenToString(*aParam[2], options_buf, nullptr) : nullptr;
	int type = 0;
	double timeout = 0;
	HWND owner = nullptr;
	if (options && !MsgBoxParseOptions(options, type, timeout, owner))
	{
		aResultToken.Error(_T("Invalid MsgBox options."));
		return;
	}
	int result = MsgBox(text, type, title, timeout, owner);
	LPTSTR result_string = MsgBoxResultString(result);
	aResultToken.SetValue(result_string ? result_string : _T(""));
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

// --- date/time built-in variables ---
// Mirrors GetDateTimeBIV() in lib/vars.cpp using the real local-time state.
static void LinuxGetDateTimeBIV(LPTSTR aBuf, LPTSTR aVarName)
{
	if (!aBuf)
		return;
	aVarName += 2; // Skip past "A_".

	// Refresh the cached time only if it's been a certain number of
	// milliseconds since the last fetch, keeping the A_* time variables in
	// sync with one another when used consecutively.
	static DWORD sLastUpdate = 0;
	static SYSTEMTIME sST = {0};
	BOOL is_msec = !_tcsicmp(aVarName, _T("MSec"));
	DWORD now_tick = GetTickCount();
	if (is_msec || now_tick - sLastUpdate > 50 || !sST.wYear)
	{
		GetLocalTime(&sST);
		sLastUpdate = now_tick;
	}

	if (is_msec)
	{
		_stprintf(aBuf, _T("%03d"), sST.wMilliseconds);
		return;
	}

	TCHAR second_letter = ctoupper(aVarName[1]);
	switch (ctoupper(aVarName[0]))
	{
	case 'Y':
		switch (second_letter)
		{
		case 'D': // A_YDay
			_stprintf(aBuf, _T("%d"), GetYDay(sST.wMonth, sST.wDay, IS_LEAP_YEAR(sST.wYear)));
			break;
		case 'W': // A_YWeek
			GetISOWeekNumber(aBuf, sST.wYear
				, GetYDay(sST.wMonth, sST.wDay, IS_LEAP_YEAR(sST.wYear))
				, sST.wDayOfWeek);
			break;
		default: // A_Year / A_YYYY
			_stprintf(aBuf, _T("%d"), sST.wYear);
			break;
		}
		break;
	case 'M':
		switch (second_letter)
		{
		case 'D': // A_MDay (synonymous with A_DD)
			_stprintf(aBuf, _T("%02d"), sST.wDay);
			break;
		case 'I': // A_Min
			_stprintf(aBuf, _T("%02d"), sST.wMinute);
			break;
		default: // A_MM and A_Mon
			_stprintf(aBuf, _T("%02d"), sST.wMonth);
			break;
		}
		break;
	case 'D': // A_DD
		_stprintf(aBuf, _T("%02d"), sST.wDay);
		break;
	case 'W': // A_WDay
		_stprintf(aBuf, _T("%d"), sST.wDayOfWeek + 1);
		break;
	case 'H': // A_Hour
		_stprintf(aBuf, _T("%02d"), sST.wHour);
		break;
	case 'S': // A_Sec
		_stprintf(aBuf, _T("%02d"), sST.wSecond);
		break;
	default:
		aBuf[0] = L'\0';
		break;
	}
}

void BIV_DateTime(ResultToken &aResultToken, LPTSTR aVarName)
{
	LinuxGetDateTimeBIV(aResultToken.buf, aVarName);
	aResultToken.ReturnPtr(aResultToken.buf);
}

static void LinuxSetPersistentString(ResultToken &aResultToken, LPTSTR aString); // Defined below.

void BIV_MMM_DDD(ResultToken &aResultToken, LPTSTR aVarName)
{
	// A_MMM/A_MMMM/A_DDD/A_DDDD (English names; locale-aware month/day
	// formatting can be added later via strftime).
	static const wchar_t *months_short[] = {L"Jan", L"Feb", L"Mar", L"Apr", L"May", L"Jun"
		, L"Jul", L"Aug", L"Sep", L"Oct", L"Nov", L"Dec"};
	static const wchar_t *months_full[] = {L"January", L"February", L"March", L"April", L"May", L"June"
		, L"July", L"August", L"September", L"October", L"November", L"December"};
	static const wchar_t *days_short[] = {L"Sun", L"Mon", L"Tue", L"Wed", L"Thu", L"Fri", L"Sat"};
	static const wchar_t *days_full[] = {L"Sunday", L"Monday", L"Tuesday", L"Wednesday"
		, L"Thursday", L"Friday", L"Saturday"};
	SYSTEMTIME st;
	GetLocalTime(&st);
	const wchar_t *result = L"";
	if (ctoupper(aVarName[2]) == 'M')
		result = aVarName[5] ? months_full[st.wMonth - 1] : months_short[st.wMonth - 1];
	else
		result = aVarName[5] ? days_full[st.wDayOfWeek] : days_short[st.wDayOfWeek];
	LinuxSetPersistentString(aResultToken, const_cast<LPTSTR>(result));
}

void BIV_Now(ResultToken &aResultToken, LPTSTR aVarName)
{
	SYSTEMTIME st;
	if (aVarName[5]) // A_NowUTC
		GetSystemTime(&st);
	else
		GetLocalTime(&st);
	SystemTimeToYYYYMMDD(aResultToken.buf, st);
	aResultToken.ReturnPtr(aResultToken.buf);
}

// RunWait: launch a process and wait for it to finish, returning its exit code.
// The OutputVarPID parameter is assigned before waiting (per the docs).
BIF_DECL(BIF_RunWait)
{
	TCHAR target_buf[LINE_SIZE];
	target_buf[0] = L'\0';
	LPTSTR target = aParamCount > 0 ? TokenToString(*aParam[0], target_buf, nullptr) : nullptr;
	TCHAR wd_buf[4096];
	wd_buf[0] = L'\0';
	LPTSTR wd = aParamCount > 1 ? TokenToString(*aParam[1], wd_buf, nullptr) : nullptr;
	TCHAR opt_buf[64];
	opt_buf[0] = L'\0';
	LPTSTR opts = aParamCount > 2 ? TokenToString(*aParam[2], opt_buf, nullptr) : nullptr;
	Var *output_var = ParamIndexToOutputVar(3);
	if (output_var)
		output_var->Assign();

	HANDLE running_process = nullptr;
	if (!g_script.ActionExec(target, nullptr, wd, true, opts, &running_process, true, true))
	{
		aResultToken.SetExitResult(FAIL);
		return;
	}

	// For the output var to be useful, it must be assigned before we wait:
	if (output_var && running_process)
		output_var->Assign((__int64)GetProcessId(running_process));

	if (!running_process) // Nothing to wait for.
	{
		aResultToken.SetValue((__int64)0);
		return;
	}

	for (;;)
	{
		if (WaitForSingleObject(running_process, 0) != WAIT_TIMEOUT)
			break;
		ScriptSleep(10);
	}
	DWORD exit_code = 0;
	GetExitCodeProcess(running_process, &exit_code);
	CloseHandle(running_process);
	aResultToken.SetValue((__int64)(int)exit_code);
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
	TCHAR wbuf[4096];
	LPTSTR wide = TokenToString(*aParam[0], wbuf, nullptr);
	if (!wide)
		wide = wbuf;
	DWORD attr;
	// Docs: returns the attribute string of the first matching directory, or "".
	if (!DoesFilePatternExist(wide, &attr, FILE_ATTRIBUTE_DIRECTORY))
	{
		aResultToken.SetValue(_T(""));
		return;
	}
	LPTSTR out = aResultToken.buf;
	FileAttribToStr(out, attr);
	aResultToken.SetValue(out);
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
	TCHAR wbuf[4096];
	LPTSTR wide = TokenToString(*aParam[0], wbuf, nullptr);
	if (!wide)
		wide = wbuf;
	DWORD attr;
	// Docs: returns the attribute string of the first matching file, e.g. "A"
	// for a regular file or "D" for a directory (the pattern is also matched
	// against directories), or "" if nothing matches.
	if (!DoesFilePatternExist(wide, &attr, 0))
	{
		aResultToken.SetValue(_T(""));
		return;
	}
	LPTSTR out = aResultToken.buf;
	FileAttribToStr(out, attr);
	if (!*out) // See FileOrDirExist() in lib/file.cpp.
	{
		out[0] = _T('X');
		out[1] = _T('\0');
	}
	aResultToken.SetValue(out);
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