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
#include <map>
#include <sys/stat.h>

// Real implementations from window.cpp / script2.cpp (declared here because
// they have no header declaration on the Windows build either).
ResultType MsgBoxParseOptions(LPCTSTR aOptions, int &aType, double &aTimeout, HWND &aOwner);
LPTSTR MsgBoxResultString(int aResult);
void ScriptSleep(int aDelay);

#define LINUX_BIF_STUB(name) \
void name(BIF_DECL_PARAMS) { (void)aParam; (void)aParamCount; }

#define LINUX_BIF_STUB_ERR(name) \
void name(BIF_DECL_PARAMS) { (void)aParam; (void)aParamCount; aResultToken.Error(_T("This built-in function has not been ported to Linux yet.")); }

#define LINUX_BIV_STUB(name) \
void name(ResultToken &aResultToken, LPTSTR aVarName) { (void)aVarName; (void)aResultToken; }

#define LINUX_BIV_STUB_RW(name) \
LINUX_BIV_STUB(name) \
void name##_Set(ResultToken &aResultToken, LPTSTR aVarName, ExprTokenType &aValue) { (void)aVarName; (void)aValue; (void)aResultToken; }

// BIF_CaretGetPos: implemented in core_caret_linux.cpp (X11 / GTK caret).

// WinExist/WinActive: implemented in core_win_linux.cpp (X11 window backend).

LINUX_BIV_STUB_RW(BIV_AllowMainWindow)
LINUX_BIV_STUB(BIV_Cursor)
LINUX_BIV_STUB(BIV_EndChar)
LINUX_BIV_STUB_RW(BIV_Hotkey)

// A_IconFile / A_IconNumber: the file + number passed to TraySetIcon
// (check_detail0821 §5-M5).  Set by BIF_Linux_TraySetIcon; mirrors upstream.
BIV_DECL_R(BIV_IconFile)
{
	_f_return_p(g_script.mCustomIconFile ? g_script.mCustomIconFile : _T(""));
}

BIV_DECL_R(BIV_IconNumber)
{
	_f_return_i((int)g_script.mCustomIconNumber);
}

LINUX_BIV_STUB_RW(BIV_IconHidden)
LINUX_BIV_STUB_RW(BIV_IconTip)

// --- Built-in variables with real doc semantics on Linux ---

BIV_DECL_R(BIV_EventInfo)
{
	aResultToken.SetValue((__int64)(g ? g->EventInfo : 0));
}
void BIV_EventInfo_Set(ResultToken &aResultToken, LPTSTR aVarName, ExprTokenType &aValue)
{
	(void)aResultToken; (void)aVarName;
	if (g)
		g->EventInfo = (EventInfoType)TokenToInt64(aValue);
}

BIV_DECL_R(BIV_IsCritical)
{
	// Docs: 0 if not critical, otherwise the peek frequency (as in lib/vars.cpp).
	if (g && g->ThreadIsCritical)
		aResultToken.SetValue((__int64)g->PeekFrequency);
	else
		aResultToken.SetValue((__int64)0);
}

BIV_DECL_R(BIV_IsPaused)
{
	// Docs: 1 if the thread beneath the current thread is paused (lib/vars.cpp logic).
	aResultToken.SetValue((__int64)(g > g_array && g[-1].IsPaused));
}

BIV_DECL_R(BIV_IsSuspended)
{
	aResultToken.SetValue((__int64)(g_IsSuspended ? 1 : 0));
}

BIV_DECL_R(BIV_Language)
{
	// Docs: a four-digit hexadecimal language identifier.  Best-effort mapping
	// of the LANG environment's language code to a Windows LCID.
	const char *lang = std::getenv("LANG");
	unsigned lcid = 0x0409; // en-US default.
	if (lang)
	{
		if (!strncasecmp(lang, "zh", 2)) lcid = 0x0804;
		else if (!strncasecmp(lang, "ja", 2)) lcid = 0x0411;
		else if (!strncasecmp(lang, "de", 2)) lcid = 0x0407;
		else if (!strncasecmp(lang, "fr", 2)) lcid = 0x040C;
		else if (!strncasecmp(lang, "es", 2)) lcid = 0x040A;
		else if (!strncasecmp(lang, "ru", 2)) lcid = 0x0419;
		else if (!strncasecmp(lang, "ko", 2)) lcid = 0x0412;
		else if (!strncasecmp(lang, "it", 2)) lcid = 0x0410;
		else if (!strncasecmp(lang, "pt", 2)) lcid = 0x0416;
		else if (!strncasecmp(lang, "nl", 2)) lcid = 0x0413;
		else if (!strncasecmp(lang, "pl", 2)) lcid = 0x0415;
		else if (!strncasecmp(lang, "tr", 2)) lcid = 0x041F;
		else if (!strncasecmp(lang, "sv", 2)) lcid = 0x041D;
		else if (!strncasecmp(lang, "fi", 2)) lcid = 0x040B;
		else if (!strncasecmp(lang, "cs", 2)) lcid = 0x0405;
	}
	TCHAR buf[16];
	_stprintf(buf, _T("%04X"), lcid);
	LPTSTR persistent = (LPTSTR)SimpleHeap::Alloc((_tcslen(buf) + 1) * sizeof(TCHAR));
	_tcscpy(persistent, buf);
	aResultToken.SetValue(persistent);
}

BIV_DECL_R(BIV_LastError)
{
	aResultToken.SetValue((__int64)(g ? g->LastError : 0));
}
void BIV_LastError_Set(ResultToken &aResultToken, LPTSTR aVarName, ExprTokenType &aValue)
{
	(void)aResultToken; (void)aVarName;
	if (g)
	{
		g->LastError = (DWORD)TokenToInt64(aValue);
		SetLastError(g->LastError);
	}
}

BIV_DECL_R(BIV_LineFile)
{
	aResultToken.SetValue(g_script.CurrentFile());
}
BIV_DECL_R(BIV_LineNumber)
{
	aResultToken.SetValue((__int64)g_script.CurrentLine());
}
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
BIV_DECL_R(BIV_MyDocuments)
{
	// Docs: full path of the user's Documents folder.  Use the XDG convention
	// (~/Documents), falling back to the home directory.
	const char *home = std::getenv("HOME");
	std::string path = home && *home ? home : "/tmp";
	std::string docs = path + "/Documents";
	struct stat st;
	if (stat(docs.c_str(), &st) != 0 || !S_ISDIR(st.st_mode))
		docs = path;
	wchar_t buf[4096];
	if (mbstowcs(buf, docs.c_str(), 4095) == (size_t)-1)
		buf[0] = L'\0';
	buf[4095] = L'\0';
	LPTSTR persistent = (LPTSTR)SimpleHeap::Alloc((_tcslen(buf) + 1) * sizeof(TCHAR));
	_tcscpy(persistent, buf);
	aResultToken.SetValue(persistent);
}
BIV_DECL_R(BIV_ScriptHwnd)
{
	// Docs: the HWND of the script's main window.  The Linux port has no main
	// window, so this is always 0.
	aResultToken.SetValue((__int64)0);
}
BIV_DECL_R(BIV_ThisFunc)
{
	aResultToken.SetValue(g && g->CurrentFunc ? g->CurrentFunc->mName : _T(""));
}

// --- Settings built-in variables (semantics per docs; mirrors lib/vars.cpp) ---

// Returns the previous setting (read side) for the x-delay variables.
static int &LinuxBIVxDelay(LPTSTR aVarName)
{
	switch (ctoupper(aVarName[2])) // a_X...
	{
	case 'K':
		if (ctolower(aVarName[6]) == 'e') // a_keydE...
		{
			if (aVarName[10]) // a_keydelayP...
				return g->KeyDelayPlay;
			else
				return g->KeyDelay;
		}
		else // a_keydU...
		{
			if (aVarName[13]) // a_keydurationP...
				return g->PressDurationPlay;
			else
				return g->PressDuration;
		}
	case 'M':
		if (aVarName[12]) // a_mousedelayP...
			return g->MouseDelayPlay;
		else
			return g->MouseDelay;
	case 'W':
		return g->WinDelay;
	default:
		return g->ControlDelay;
	}
}

BIV_DECL_R(BIV_xDelay)
{
	aResultToken.SetValue((__int64)LinuxBIVxDelay(aVarName));
}
void BIV_xDelay_Set(ResultToken &aResultToken, LPTSTR aVarName, ExprTokenType &aValue)
{
	(void)aResultToken;
	LinuxBIVxDelay(aVarName) = (int)TokenToInt64(aValue);
}

BIV_DECL_R(BIV_DefaultMouseSpeed)
{
	aResultToken.SetValue((__int64)g->DefaultMouseSpeed);
}
void BIV_DefaultMouseSpeed_Set(ResultToken &aResultToken, LPTSTR aVarName, ExprTokenType &aValue)
{
	(void)aResultToken; (void)aVarName;
	g->DefaultMouseSpeed = (int)TokenToInt64(aValue);
}

BIV_DECL_R(BIV_CoordMode)
{
	static LPTSTR sCoordModes[] = COORD_MODES;
	// aVarName is "A_CoordMode<Name>"; "A_CoordMode" is 11 chars.
	CoordModeType shift = Line::ConvertCoordModeCmd(aVarName + 11);
	aResultToken.SetValue(sCoordModes[(g->CoordMode >> shift) & COORD_MODE_MASK]);
}
void BIV_CoordMode_Set(ResultToken &aResultToken, LPTSTR aVarName, ExprTokenType &aValue)
{
	CoordModeType shift = Line::ConvertCoordModeCmd(aVarName + 11);
	TCHAR mode_buf[64];
	LPTSTR m = TokenToString(aValue, mode_buf, nullptr);
	CoordModeType mode = Line::ConvertCoordMode(m ? m : mode_buf);
	if (mode == COORD_MODE_INVALID)
		aResultToken.Error(_T("Invalid value."), _T(""), ErrorPrototype::Value);
	else
		g->CoordMode = (g->CoordMode & ~(COORD_MODE_MASK << shift)) | (mode << shift);
}

BIV_DECL_R(BIV_DetectHiddenWindows)
{
	aResultToken.SetValue(g->DetectHiddenWindows ? 1 : 0);
}
void BIV_DetectHiddenWindows_Set(ResultToken &aResultToken, LPTSTR aVarName, ExprTokenType &aValue)
{
	(void)aResultToken; (void)aVarName;
	g->DetectHiddenWindows = TokenToBOOL(aValue);
}

BIV_DECL_R(BIV_DetectHiddenText)
{
	aResultToken.SetValue(g->DetectHiddenText ? 1 : 0);
}
void BIV_DetectHiddenText_Set(ResultToken &aResultToken, LPTSTR aVarName, ExprTokenType &aValue)
{
	(void)aResultToken; (void)aVarName;
	g->DetectHiddenText = TokenToBOOL(aValue);
}

BIV_DECL_R(BIV_TitleMatchMode)
{
	if (g->TitleMatchMode == FIND_REGEX)
		aResultToken.SetValue(_T("RegEx"));
	else
		aResultToken.SetValue((__int64)g->TitleMatchMode);
}
void BIV_TitleMatchMode_Set(ResultToken &aResultToken, LPTSTR aVarName, ExprTokenType &aValue)
{
	(void)aVarName;
	TCHAR mode_buf[64];
	LPTSTR m = TokenToString(aValue, mode_buf, nullptr);
	TitleMatchModes mode = Line::ConvertTitleMatchMode(m ? m : mode_buf);
	if (mode == FIND_FAST || mode == FIND_SLOW)
		g->TitleFindFast = (mode == FIND_FAST);
	else if (mode == MATCHMODE_INVALID)
		aResultToken.Error(_T("Invalid value."), _T(""), ErrorPrototype::Value);
	else
		g->TitleMatchMode = mode;
}

BIV_DECL_R(BIV_TitleMatchModeSpeed)
{
	aResultToken.SetValue(g->TitleFindFast ? _T("Fast") : _T("Slow"));
}

BIV_DECL_R(BIV_SendMode)
{
	static LPTSTR sSendModes[] = SEND_MODES;
	aResultToken.SetValue(sSendModes[g->SendMode]);
}
void BIV_SendMode_Set(ResultToken &aResultToken, LPTSTR aVarName, ExprTokenType &aValue)
{
	(void)aVarName;
	TCHAR mode_buf[64];
	LPTSTR m = TokenToString(aValue, mode_buf, nullptr);
	SendModes mode = Line::ConvertSendMode(m ? m : mode_buf, SM_INVALID);
	if (mode == SM_INVALID)
		aResultToken.Error(_T("Invalid value."), _T(""), ErrorPrototype::Value);
	else
		g->SendMode = mode;
}

BIV_DECL_R(BIV_SendLevel)
{
	aResultToken.SetValue((__int64)g->SendLevel);
}
void BIV_SendLevel_Set(ResultToken &aResultToken, LPTSTR aVarName, ExprTokenType &aValue)
{
	(void)aVarName;
	int level = (int)TokenToInt64(aValue);
	if (!SendLevelIsValid(level))
		aResultToken.Error(_T("Invalid value."), _T(""), ErrorPrototype::Value);
	else
		g->SendLevel = (SendLevelType)level;
}

BIV_DECL_R(BIV_StoreCapsLockMode)
{
	aResultToken.SetValue(g->StoreCapslockMode ? 1 : 0);
}
void BIV_StoreCapsLockMode_Set(ResultToken &aResultToken, LPTSTR aVarName, ExprTokenType &aValue)
{
	(void)aResultToken; (void)aVarName;
	g->StoreCapslockMode = TokenToBOOL(aValue);
}

BIV_DECL_R(BIV_RegView)
{
	switch (g->RegView)
	{
	case KEY_WOW64_32KEY: aResultToken.SetValue(_T("32")); break;
	case KEY_WOW64_64KEY: aResultToken.SetValue(_T("64")); break;
	default: aResultToken.SetValue(_T("Default")); break;
	}
}
void BIV_RegView_Set(ResultToken &aResultToken, LPTSTR aVarName, ExprTokenType &aValue)
{
	(void)aVarName;
	TCHAR view_buf[32];
	LPTSTR v = TokenToString(aValue, view_buf, nullptr);
	DWORD reg_view = Line::RegConvertView(v ? v : view_buf);
	if (reg_view == (DWORD)-1)
		aResultToken.Error(_T("Invalid value."), _T(""), ErrorPrototype::Value);
	else if (sizeof(void *) == 8)
		g->RegView = reg_view;
}

BIV_DECL_R(BIV_FileEncoding)
{
	LPTSTR enc;
	switch (g->Encoding)
	{
	case CP_UTF8:               enc = _T("UTF-8");      break;
	case CP_UTF8 | CP_AHKNOBOM: enc = _T("UTF-8-RAW");  break;
	case CP_UTF16:              enc = _T("UTF-16");     break;
	case CP_UTF16 | CP_AHKNOBOM: enc = _T("UTF-16-RAW"); break;
	default:
	{
		TCHAR *buf = aResultToken.buf;
		buf[0] = _T('C');
		buf[1] = _T('P');
		_itot(g->Encoding, buf + 2, 10);
		aResultToken.SetValue(buf);
		return;
	}
	}
	aResultToken.SetValue(enc);
}
void BIV_FileEncoding_Set(ResultToken &aResultToken, LPTSTR aVarName, ExprTokenType &aValue)
{
	(void)aVarName;
	UINT new_encoding = Line::ConvertFileEncoding(aValue);
	if (new_encoding == (UINT)-1)
		aResultToken.Error(_T("Invalid value."), _T(""), ErrorPrototype::Value);
	else
		g->Encoding = new_encoding;
}

BIV_DECL_R(BIV_ListLines)
{
	aResultToken.SetValue(g->ListLinesIsEnabled ? 1 : 0);
}
void BIV_ListLines_Set(ResultToken &aResultToken, LPTSTR aVarName, ExprTokenType &aValue)
{
	(void)aResultToken; (void)aVarName;
	g->ListLinesIsEnabled = TokenToBOOL(aValue);
}

// Screen metrics: shared with the SysGet implementation in core_mdfunc_linux.cpp.
int LinuxGetSystemMetric(int aIndex);

BIV_DECL_R(BIV_ScreenWidth_Height)
{
	// aVarName is "A_ScreenWidth"/"A_ScreenHeight": 'W'/'H' is at index 8.
	int index = ctoupper(aVarName[8]) == 'W' ? 0 : 1;
	aResultToken.SetValue((__int64)LinuxGetSystemMetric(index));
}

BIV_DECL_R(BIV_ScreenDPI)
{
	aResultToken.SetValue((__int64)(g_ScreenDPI ? g_ScreenDPI : 96));
}

LINUX_BIV_STUB(BIV_PriorHotkey)
LINUX_BIV_STUB(BIV_PriorKey)
LINUX_BIV_STUB(BIV_SpecialFolderPath)
LINUX_BIV_STUB(BIV_ThisHotkey)
LINUX_BIV_STUB(BIV_TimeIdle)
LINUX_BIV_STUB(BIV_TimeSincePriorHotkey)
LINUX_BIV_STUB(BIV_TimeSinceThisHotkey)

// A_TrayMenu (check_detail0821 §5-M5): the script-customizable tray menu.
// The Linux UserMenu (script_menu_linux.cpp) stores the items; the SNI tray
// (core_tray_linux.cpp) renders them and invokes the callbacks on click.
// The script retains the object, so no AddRef here (matches upstream).
BIV_DECL_R(BIV_TrayMenu)
{
	// The upstream Script::Init creates mTrayMenu after CreateWindowEx, which
	// is not reached on this port (no main Win32 window), so create it lazily
	// on first access.
	if (!g_script.mTrayMenu)
		g_script.mTrayMenu = new UserMenu(MENU_TYPE_POPUP);
	_f_return(g_script.mTrayMenu);
}

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
BIV_DECL_R(BIV_PtrSize) { aResultToken.SetValue((__int64)sizeof(void*)); }
BIV_DECL_R(BIV_ScriptDir) { aResultToken.SetValue(g_script.mFileDir ? g_script.mFileDir : _T("")); }
BIV_DECL_R(BIV_ScriptFullPath) { aResultToken.SetValue(g_script.mFileSpec ? g_script.mFileSpec : _T("")); }
void BIV_ScriptName(ResultToken &aResultToken, LPTSTR aVarName) { (void)aVarName; aResultToken.SetValue(g_script.mFileName ? g_script.mFileName : _T("")); }
void BIV_ScriptName_Set(ResultToken &aResultToken, LPTSTR aVarName, ExprTokenType &aValue) { (void)aResultToken; (void)aVarName; (void)aValue; }
BIV_DECL_R(BIV_Space_Tab) { aResultToken.SetValue(_tcsicmp(aVarName, _T("Tab")) == 0 ? _T("\t") : _T(" ")); }
BIV_DECL_R(BIV_Temp) { aResultToken.SetValue(_T("/tmp")); }
BIV_DECL_R(BIV_TickCount) { aResultToken.SetValue((__int64)GetTickCount()); }

BIV_DECL_R(BIV_AhkPath)
{
	// Docs: the full path of the AutoHotkey executable (/proc/self/exe).
	char narrow[4096];
	ssize_t n = readlink("/proc/self/exe", narrow, sizeof(narrow) - 1);
	if (n <= 0)
	{
		aResultToken.SetValue(_T(""));
		return;
	}
	narrow[n] = '\0';
	wchar_t wide[4096];
	if (mbstowcs(wide, narrow, 4095) == (size_t)-1)
		wide[0] = L'\0';
	wide[4095] = L'\0';
	LPTSTR persistent = (LPTSTR)SimpleHeap::Alloc((_tcslen(wide) + 1) * sizeof(TCHAR));
	_tcscpy(persistent, wide);
	aResultToken.SetValue(persistent);
}

BIV_DECL_R(BIV_OSVersion)
{
	// Docs: the OS version.  Report the kernel release (e.g. "6.8.0-31-generic").
	struct utsname uts;
	if (uname(&uts) == 0)
	{
		wchar_t wide[512];
		if (mbstowcs(wide, uts.release, 511) == (size_t)-1)
			wide[0] = L'\0';
		wide[511] = L'\0';
		LPTSTR persistent = (LPTSTR)SimpleHeap::Alloc((_tcslen(wide) + 1) * sizeof(TCHAR));
		_tcscpy(persistent, wide);
		aResultToken.SetValue(persistent);
	}
	else
		aResultToken.SetValue(_T("Linux"));
}

BIV_DECL_R(BIV_UserName_ComputerName)
{
	// Docs: A_UserName = the account name; A_ComputerName = the network name.
	wchar_t buf[256];
	buf[0] = L'\0';
	if (aVarName[10]) // A_Computer[N]ame (index 10 is 'N').
	{
		char narrow[256];
		if (gethostname(narrow, sizeof(narrow)) == 0)
			mbstowcs(buf, narrow, 255);
	}
	else
	{
		const char *user = std::getenv("USER");
		if (!user)
			user = std::getenv("LOGNAME");
		if (user)
			mbstowcs(buf, user, 255);
	}
	buf[255] = L'\0';
	LPTSTR persistent = (LPTSTR)SimpleHeap::Alloc((_tcslen(buf) + 1) * sizeof(TCHAR));
	_tcscpy(persistent, buf);
	aResultToken.SetValue(persistent);
}
BIV_DECL_R(BIV_WinDir) { aResultToken.SetValue(_T("/usr")); }

// ---------------------------------------------------------------------------
// Registry functions (RegRead/RegWrite/RegDelete/RegDeleteKey/RegCreateKey)
// on a Linux file-backed store.
//
// There is no Windows registry on Linux; this port keeps a virtual registry
// in an INI-like text file at $XDG_CONFIG_HOME/autohotkey-registry.txt
// (default ~/.config/autohotkey-registry.txt).  Each key is a section:
//   [HKEY_CURRENT_USER\Software\MyApp]
//   @=REG_SZ:default value        (@ is the "(Default)" value)
//   Name=REG_DWORD:42
// Values are stored as "TYPE:data"; backslash, newline, CR and '=' inside the
// data are backslash-escaped.  REG_DWORD is a decimal number, REG_BINARY is
// uppercase hex, REG_MULTI_SZ components are separated by newlines.
// ---------------------------------------------------------------------------

static std::string LinuxRegStorePath()
{
	const char *xdg = getenv("XDG_CONFIG_HOME");
	const char *home = getenv("HOME");
	std::string dir;
	if (xdg && *xdg)
		dir = xdg;
	else if (home && *home)
		dir = std::string(home) + "/.config";
	else
		dir = "/tmp";
	return dir + "/autohotkey-registry.txt";
}

static std::string LinuxRegEscape(const std::string &s)
{
	std::string out;
	for (char c : s)
	{
		switch (c)
		{
		case '\\': out += "\\\\"; break;
		case '\n': out += "\\n"; break;
		case '\r': out += "\\r"; break;
		case '=': out += "\\="; break;
		default: out += c; break;
		}
	}
	return out;
}

static void LinuxRegUnescape(const std::string &s, std::string &out)
{
	out.clear();
	for (size_t i = 0; i < s.size(); ++i)
	{
		if (s[i] != '\\' || i + 1 >= s.size())
		{
			out += s[i];
			continue;
		}
		switch (s[++i])
		{
		case '\\': out += '\\'; break;
		case 'n': out += '\n'; break;
		case 'r': out += '\r'; break;
		case '=': out += '='; break;
		default: out += s[i]; break;
		}
	}
}

// Normalize the root prefix (HKCU -> HKEY_CURRENT_USER etc.); false if invalid.
static bool LinuxRegNormalizeKey(const std::string &aKey, std::string &aOut)
{
	aOut = aKey;
	if (aKey.empty())
		return false;
	static const char *const roots[][2] = {
		{ "HKEY_LOCAL_MACHINE", "HKEY_LOCAL_MACHINE" },
		{ "HKEY_USERS", "HKEY_USERS" },
		{ "HKEY_CURRENT_USER", "HKEY_CURRENT_USER" },
		{ "HKEY_CLASSES_ROOT", "HKEY_CLASSES_ROOT" },
		{ "HKEY_CURRENT_CONFIG", "HKEY_CURRENT_CONFIG" },
		{ "HKLM", "HKEY_LOCAL_MACHINE" },
		{ "HKU", "HKEY_USERS" },
		{ "HKCU", "HKEY_CURRENT_USER" },
		{ "HKCR", "HKEY_CLASSES_ROOT" },
		{ "HKCC", "HKEY_CURRENT_CONFIG" },
	};
	size_t slash = aKey.find('\\');
	std::string root = aKey.substr(0, slash == std::string::npos ? aKey.size() : slash);
	for (auto &r : roots)
	{
		size_t rlen = strlen(r[0]);
		if (root.size() == rlen && !strncasecmp(root.c_str(), r[0], rlen))
		{
			aOut = r[1];
			if (slash != std::string::npos)
				aOut += aKey.substr(slash);
			return true;
		}
	}
	return false;
}

typedef std::map<std::string, std::pair<std::string, std::string>> LinuxRegSection; // name -> (type, value)
typedef std::map<std::string, LinuxRegSection> LinuxRegStore;

static bool LinuxRegLoad(LinuxRegStore &aStore)
{
	std::ifstream f(LinuxRegStorePath());
	if (!f)
		return true; // No store file yet == empty registry.
	std::string line, section;
	while (std::getline(f, line))
	{
		if (!line.empty() && line.back() == '\r')
			line.pop_back();
		if (line.empty())
			continue;
		if (line[0] == '[')
		{
			size_t end = line.find(']');
			if (end == std::string::npos)
				continue;
			section = line.substr(1, end - 1);
			continue;
		}
		size_t eq = line.find('=');
		if (eq == std::string::npos)
			continue;
		std::string name = line.substr(0, eq);
		std::string rest = line.substr(eq + 1);
		size_t colon = rest.find(':');
		if (colon == std::string::npos)
			continue;
		std::string value;
		LinuxRegUnescape(rest.substr(colon + 1), value);
		aStore[section][name] = std::make_pair(rest.substr(0, colon), value);
	}
	return true;
}

static bool LinuxRegSave(const LinuxRegStore &aStore)
{
	std::string path = LinuxRegStorePath();
	size_t slash = path.find_last_of('/');
	if (slash != std::string::npos)
	{
		std::string dir = path.substr(0, slash);
		struct stat st;
		if (stat(dir.c_str(), &st) != 0)
			mkdir(dir.c_str(), 0700);
	}
	std::ofstream f(path, std::ios::trunc);
	if (!f)
		return false;
	for (auto &sec : aStore)
	{
		if (sec.second.empty())
			continue;
		f << "[" << sec.first << "]\n";
		for (auto &ent : sec.second)
			f << ent.first << "=" << ent.second.first << ":" << LinuxRegEscape(ent.second.second) << "\n";
	}
	return true;
}

static void LinuxRegSetLastError(DWORD aError)
{
	if (g)
		g->LastError = aError;
	SetLastError(aError);
}

BIF_DECL(BIF_Reg)
{
	// RegRead(KeyName, ValueName, Default) / RegWrite(Value, ValueType,
	// KeyName, ValueName) / RegCreateKey(KeyName) / RegDelete(KeyName,
	// ValueName) / RegDeleteKey(KeyName).  The loop-based forms (KeyName
	// omitted) require a registry loop, which the Linux port does not have.
	if (g)
		g->LastError = 0;

	switch (_f_callee_id)
	{
	case FID_RegCreateKey:
	{
		if (ParamIndexIsOmitted(0))
			break;
		TCHAR key_buf[1024];
		LPTSTR key_wide = ParamIndexToString(0, key_buf);
		char key_narrow[1024];
		if (wcstombs(key_narrow, key_wide, sizeof(key_narrow)) == (size_t)-1)
			break;
		std::string key;
		if (!LinuxRegNormalizeKey(key_narrow, key))
		{
			LinuxRegSetLastError(87);
			aResultToken.Error(_T("Invalid root key."), _T(""), ErrorPrototype::OS);
			break;
		}
		LinuxRegStore store;
		LinuxRegLoad(store);
		store[key]; // Ensure the section exists.
		LinuxRegSave(store);
		break;
	}

	case FID_RegRead:
	{
		if (ParamIndexIsOmitted(0))
			break;
		TCHAR key_buf[1024];
		LPTSTR key_wide = ParamIndexToString(0, key_buf);
		char key_narrow[1024];
		if (wcstombs(key_narrow, key_wide, sizeof(key_narrow)) == (size_t)-1)
			break;
		std::string key;
		if (!LinuxRegNormalizeKey(key_narrow, key))
		{
			LinuxRegSetLastError(87);
			aResultToken.Error(_T("Invalid root key."), _T(""), ErrorPrototype::OS);
			break;
		}
		std::string value_name = "@"; // Docs: omitted ValueName reads the default value.
		if (!ParamIndexIsOmitted(1))
		{
			TCHAR vn_buf[256];
			LPTSTR vn_wide = ParamIndexToString(1, vn_buf);
			char vn_narrow[256];
			if (wcstombs(vn_narrow, vn_wide, sizeof(vn_narrow)) == (size_t)-1)
				break;
			value_name = vn_narrow;
		}
		LinuxRegStore store;
		LinuxRegLoad(store);
		auto sec_it = store.find(key);
		bool found = sec_it != store.end();
		std::string type, value;
		if (found)
		{
			auto ent_it = sec_it->second.find(value_name);
			found = ent_it != sec_it->second.end();
			if (found)
			{
				type = ent_it->second.first;
				value = ent_it->second.second;
			}
		}
		if (!found)
		{
			// Docs: Default is returned if provided, otherwise OSError.
			if (aParamCount > 2 && !ParamIndexIsOmitted(2))
			{
				aResultToken.CopyValueFrom(*aParam[2]);
				break;
			}
			LinuxRegSetLastError(2);
			aResultToken.Error(_T("Requested registry key or value does not exist."), _T(""), ErrorPrototype::OS);
			break;
		}
		if (!strcasecmp(type.c_str(), "REG_DWORD"))
		{
			aResultToken.SetValue((__int64)strtoull(value.c_str(), nullptr, 10)); // Docs: positive decimal.
		}
		else if (!strcasecmp(type.c_str(), "REG_BINARY"))
		{
			// Docs: read as a string of hex characters.
			LPTSTR persistent = (LPTSTR)SimpleHeap::Alloc((value.size() + 1) * sizeof(TCHAR));
			for (size_t i = 0; i < value.size(); ++i)
				persistent[i] = (TCHAR)(unsigned char)value[i];
			persistent[value.size()] = 0;
			aResultToken.SetValue(persistent, value.size());
		}
		else
		{
			// REG_SZ / REG_EXPAND_SZ / REG_MULTI_SZ: the value contains real
			// newlines between multi-sz components.
			LPTSTR persistent = (LPTSTR)SimpleHeap::Alloc((value.size() + 1) * sizeof(TCHAR));
			for (size_t i = 0; i < value.size(); ++i)
				persistent[i] = (TCHAR)(unsigned char)value[i];
			persistent[value.size()] = 0;
			aResultToken.SetValue(persistent, value.size());
		}
		break;
	}

	case FID_RegWrite:
	{
		if (aParamCount < 3 || ParamIndexIsOmitted(2)) // ValueType, KeyName required without a loop.
			break;
		TCHAR key_buf[1024];
		LPTSTR key_wide = ParamIndexToString(2, key_buf);
		char key_narrow[1024];
		if (wcstombs(key_narrow, key_wide, sizeof(key_narrow)) == (size_t)-1)
			break;
		std::string key;
		if (!LinuxRegNormalizeKey(key_narrow, key))
		{
			LinuxRegSetLastError(87);
			aResultToken.Error(_T("Invalid root key."), _T(""), ErrorPrototype::OS);
			break;
		}
		std::string value_name = "@";
		if (aParamCount > 3 && !ParamIndexIsOmitted(3))
		{
			TCHAR vn_buf[256];
			LPTSTR vn_wide = ParamIndexToString(3, vn_buf);
			char vn_narrow[256];
			if (wcstombs(vn_narrow, vn_wide, sizeof(vn_narrow)) == (size_t)-1)
				break;
			value_name = vn_narrow;
		}
		TCHAR type_buf[32];
		LPTSTR type_wide = ParamIndexToString(1, type_buf);
		char type_narrow[32];
		if (wcstombs(type_narrow, type_wide, sizeof(type_narrow)) == (size_t)-1)
			break;
		std::string type = type_narrow;
		std::string value;
		__int64 bytes_written = 0;
		if (!strcasecmp(type.c_str(), "REG_DWORD"))
		{
			if (!TokenIsNumeric(*aParam[0]))
			{
				LinuxRegSetLastError(87);
				aResultToken.Error(_T("REG_DWORD requires a numeric value."), _T(""), ErrorPrototype::OS);
				break;
			}
			__int64 n = TokenToInt64(*aParam[0]);
			if (n < 0)
				n &= 0xFFFFFFFF; // Docs: stored as a positive decimal.
			value = std::to_string(n);
			bytes_written = 4;
		}
		else if (!strcasecmp(type.c_str(), "REG_BINARY"))
		{
			// Value: a Buffer-like object or a string of hex digits.
			std::string hex;
			if (IObject *obj = TokenToObject(*aParam[0]))
			{
				ResultToken rt;
				size_t ptr, size;
				GetBufferObjectPtr(rt, obj, ptr, size);
				if (rt.Exited() || !ptr)
				{
					LinuxRegSetLastError(87);
					aResultToken.Error(_T("Invalid buffer for REG_BINARY."), _T(""), ErrorPrototype::OS);
					break;
				}
				static const char digits[] = "0123456789ABCDEF";
				unsigned char *data = (unsigned char *)ptr;
				for (size_t i = 0; i < size; ++i)
				{
					hex += digits[data[i] >> 4];
					hex += digits[data[i] & 0xF];
				}
				bytes_written = (__int64)size;
			}
			else
			{
				TCHAR val_buf[16384];
				size_t val_len = 0;
				LPTSTR val_wide = ParamIndexToString(0, val_buf, &val_len);
				char val_narrow[32768];
				size_t n = wcstombs(val_narrow, val_wide, sizeof(val_narrow) - 1);
				if (n == (size_t)-1)
					break;
				val_narrow[n] = 0;
				for (char *p = val_narrow; *p; ++p)
					if (*p != ' ' && *p != ',')
						hex += *p;
				if (hex.size() % 2)
				{
					LinuxRegSetLastError(87);
					aResultToken.Error(_T("Invalid hex string for REG_BINARY."), _T(""), ErrorPrototype::OS);
					break;
				}
				bytes_written = (__int64)(hex.size() / 2);
			}
			value = hex;
		}
		else if (!strcasecmp(type.c_str(), "REG_MULTI_SZ"))
		{
			TCHAR val_buf[16384];
			size_t val_len = 0;
			LPTSTR val_wide = ParamIndexToString(0, val_buf, &val_len);
			char val_narrow[32768];
			size_t n = wcstombs(val_narrow, val_wide, sizeof(val_narrow) - 1);
			if (n == (size_t)-1)
				break;
			val_narrow[n] = 0;
			value = val_narrow; // Components are separated by real newlines.
			bytes_written = 0;
			size_t start = 0;
			for (size_t i = 0; i <= value.size(); ++i)
				if (i == value.size() || value[i] == '\n')
				{
					bytes_written += (__int64)(i - start + 1) * 2;
					start = i + 1;
				}
		}
		else if (!strcasecmp(type.c_str(), "REG_SZ") || !strcasecmp(type.c_str(), "REG_EXPAND_SZ"))
		{
			TCHAR val_buf[16384];
			size_t val_len = 0;
			LPTSTR val_wide = ParamIndexToString(0, val_buf, &val_len);
			char val_narrow[32768];
			size_t n = wcstombs(val_narrow, val_wide, sizeof(val_narrow) - 1);
			if (n == (size_t)-1)
				break;
			val_narrow[n] = 0;
			value = val_narrow;
			bytes_written = (__int64)(value.size() + 1) * 2;
		}
		else
		{
			LinuxRegSetLastError(87);
			aResultToken.Error(_T("Unsupported registry value type."), _T(""), ErrorPrototype::OS);
			break;
		}
		LinuxRegStore store;
		LinuxRegLoad(store);
		store[key][value_name] = std::make_pair(type, value);
		LinuxRegSave(store);
		aResultToken.SetValue(bytes_written);
		break;
	}

	case FID_RegDelete:
	{
		if (ParamIndexIsOmitted(0))
			break;
		TCHAR key_buf[1024];
		LPTSTR key_wide = ParamIndexToString(0, key_buf);
		char key_narrow[1024];
		if (wcstombs(key_narrow, key_wide, sizeof(key_narrow)) == (size_t)-1)
			break;
		std::string key;
		if (!LinuxRegNormalizeKey(key_narrow, key))
		{
			LinuxRegSetLastError(87);
			aResultToken.Error(_T("Invalid root key."), _T(""), ErrorPrototype::OS);
			break;
		}
		std::string value_name = "@"; // Docs: omitted ValueName deletes the default value.
		if (!ParamIndexIsOmitted(1))
		{
			TCHAR vn_buf[256];
			LPTSTR vn_wide = ParamIndexToString(1, vn_buf);
			char vn_narrow[256];
			if (wcstombs(vn_narrow, vn_wide, sizeof(vn_narrow)) == (size_t)-1)
				break;
			value_name = vn_narrow;
		}
		LinuxRegStore store;
		LinuxRegLoad(store);
		auto sec_it = store.find(key);
		if (sec_it == store.end() || sec_it->second.erase(value_name) == 0)
		{
			LinuxRegSetLastError(2);
			aResultToken.Error(_T("Requested registry value does not exist."), _T(""), ErrorPrototype::OS);
			break;
		}
		LinuxRegSave(store);
		break;
	}

	case FID_RegDeleteKey:
	{
		if (ParamIndexIsOmitted(0))
			break;
		TCHAR key_buf[1024];
		LPTSTR key_wide = ParamIndexToString(0, key_buf);
		char key_narrow[1024];
		if (wcstombs(key_narrow, key_wide, sizeof(key_narrow)) == (size_t)-1)
			break;
		std::string key;
		if (!LinuxRegNormalizeKey(key_narrow, key))
		{
			LinuxRegSetLastError(87);
			aResultToken.Error(_T("Invalid root key."), _T(""), ErrorPrototype::OS);
			break;
		}
		LinuxRegStore store;
		LinuxRegLoad(store);
		std::string prefix = key + "\\";
		bool deleted_any = store.erase(key) > 0;
		for (auto it = store.begin(); it != store.end();)
		{
			if (it->first.compare(0, prefix.size(), prefix) == 0)
			{
				it = store.erase(it);
				deleted_any = true;
			}
			else
				++it;
		}
		if (!deleted_any)
		{
			LinuxRegSetLastError(2);
			aResultToken.Error(_T("Requested registry key does not exist."), _T(""), ErrorPrototype::OS);
			break;
		}
		LinuxRegSave(store);
		break;
	}
	}
}
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

// Set the result from an already-decoded wide string (SimpleHeap copy).
static void LinuxSetResultFromWide(ResultToken &aResultToken, const std::wstring &aWide)
{
	if (aWide.empty())
	{
		aResultToken.SetValue(_T(""));
		return;
	}
	LPTSTR persistent = (LPTSTR)SimpleHeap::Alloc((aWide.size() + 1) * sizeof(TCHAR));
	tmemcpy(persistent, aWide.c_str(), aWide.size() + 1);
	aResultToken.SetValue(persistent, aWide.size());
}

// Append one code point as UTF-16LE bytes.
static void LinuxAppendUtf16LE(std::string &aOut, unsigned int aCp)
{
	if (aCp >= 0x10000)
	{
		aCp -= 0x10000;
		unsigned int hi = 0xD800 + (aCp >> 10);
		unsigned int lo = 0xDC00 + (aCp & 0x3FF);
		aOut += (char)(hi & 0xFF);
		aOut += (char)((hi >> 8) & 0xFF);
		aOut += (char)(lo & 0xFF);
		aOut += (char)((lo >> 8) & 0xFF);
	}
	else
	{
		aOut += (char)(aCp & 0xFF);
		aOut += (char)((aCp >> 8) & 0xFF);
	}
}

// Decode UTF-16LE bytes (surrogate pairs included) into wide characters.
static void LinuxDecodeUtf16LE(const std::string &aBytes, std::wstring &aWide)
{
	size_t i = 0;
	while (i + 1 < aBytes.size())
	{
		unsigned int cp = (unsigned char)aBytes[i] | ((unsigned int)(unsigned char)aBytes[i + 1] << 8);
		i += 2;
		if (cp >= 0xD800 && cp <= 0xDBFF && i + 1 < aBytes.size())
		{
			unsigned int lo = (unsigned char)aBytes[i] | ((unsigned int)(unsigned char)aBytes[i + 1] << 8);
			if (lo >= 0xDC00 && lo <= 0xDFFF)
			{
				cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
				i += 2;
			}
		}
		aWide += (wchar_t)cp;
	}
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
	if (!ofs)
	{
		// Docs: FileAppend throws an OSError if the file cannot be opened
		// for writing (was a silent no-op before round-27).
		if (g)
		{
			g->LastError = 13; // EACCES.
			SetLastError(13);
		}
		aResultToken.Error(_T("The system cannot open the file for writing."), _T(""), ErrorPrototype::OS);
		return;
	}
	// Docs: FileEncoding sets the default encoding used by FileAppend.
	// UTF-8/UTF-16 write a BOM when creating a new (empty) file; the -RAW
	// variants never write a BOM; CP0 is the locale encoding (UTF-8 here).
	UINT enc = g ? g->Encoding : 0;
	bool utf16 = (enc == CP_UTF16 || enc == (CP_UTF16 | CP_AHKNOBOM));
	bool with_bom = (enc == CP_UTF16 || enc == CP_UTF8);
	if (with_bom && ofs.tellp() == 0) // Only for a new/empty file.
	{
		if (utf16)
			ofs.write("\xFF\xFE", 2);
		else
			ofs.write("\xEF\xBB\xBF", 3);
	}
	if (utf16)
	{
		std::string bytes;
		for (const wchar_t *p = text; *p; ++p)
			LinuxAppendUtf16LE(bytes, (unsigned int)*p);
		ofs.write(bytes.data(), (std::streamsize)bytes.size());
	}
	else
	{
		char mb[4096];
		size_t n = wcstombs(mb, text, sizeof(mb));
		if (n != (size_t)-1)
			ofs.write(mb, (std::streamsize)n);
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
		// Docs: OSError is thrown if the file cannot be opened for reading.
		if (g)
		{
			g->LastError = 2; // ERROR_FILE_NOT_FOUND.
			SetLastError(2);
		}
		aResultToken.Error(_T("The system cannot find the file specified."), _T(""), ErrorPrototype::OS);
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
	// Docs: FileEncoding sets the default encoding used by FileRead; a BOM in
	// the file overrides the default.  UTF-16 (LE) is decoded with surrogate
	// support; everything else is treated as UTF-8 (the Linux locale encoding).
	UINT enc = g ? g->Encoding : 0;
	size_t off = 0;
	if (content.size() >= 3 && (unsigned char)content[0] == 0xEF && (unsigned char)content[1] == 0xBB && (unsigned char)content[2] == 0xBF)
	{
		off = 3;
		enc = CP_UTF8;
	}
	else if (content.size() >= 2 && (unsigned char)content[0] == 0xFF && (unsigned char)content[1] == 0xFE)
	{
		off = 2;
		enc = CP_UTF16;
	}
	if (enc == CP_UTF16 || enc == (CP_UTF16 | CP_AHKNOBOM))
	{
		std::wstring wide;
		LinuxDecodeUtf16LE(content.substr(off), wide);
		LinuxSetResultFromWide(aResultToken, wide);
	}
	else
	{
		SetResultFromUtf8(aResultToken, content.substr(off));
	}
}
// Keep the preprocessor namespace clean.
#undef LINUX_BIF_STUB
#undef LINUX_BIV_STUB
#undef LINUX_BIV_STUB_RW