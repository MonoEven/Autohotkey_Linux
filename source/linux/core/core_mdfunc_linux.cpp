// Linux implementation of Script::GetBuiltInMdFunc().
//
// On Windows this function is provided by MdFunc.cpp, which marshals the
// metadata-driven functions from lib/functions.h through the DynaCall x64
// assembly helper.  The Linux port instead exposes a curated table of
// built-in functions using the standard BIF calling convention:
//   - entries with a real native implementation available in already-linked
//     translation units are wired to it through small wrappers;
//   - entries whose native implementation has not been ported yet resolve to
//     a clear "not implemented on Linux" runtime error, so that scripts using
//     them fail with a proper error instead of being treated as reads of an
//     unassigned variable (the old behaviour for any function missing from
//     both g_BIF and this table).

#include "../../stdafx.h"
#include "../../script.h"
#include "../../globaldata.h"
#include "../../abi.h"
#include "../../script_func_impl.h"
#include "../gui/x11_gui.h"
#include <strings.h> // strcasecmp (AHK_STRICT_PARITY).
#include <cstdio>

// Parity lookup (core_parity_linux.cpp; classification table generated from
// tests/doccheck/parity.tsv).  Used by BIF_Linux_ParityLevel.
extern "C" const char *LinuxParityLookup(const char *aName, int &aLevel);
#include "core_win_linux.h"
#include "core_input_linux.h"
#include "core_ctrl_linux.h"
#include "core_screen_linux.h"
#include "core_image_linux.h"
#include "core_display_linux.h"
#include "core_timer_linux.h"
#include "core_hotkey_linux.h"
#include "core_pack_linux.h"
#include "input_backend.h"
#include "core_clipboard_linux.h"
#include "core_ime_linux.h"
#include <cstdlib>
#include <cstring>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <list>
#include <map>
#include <dirent.h>
#include <sys/statvfs.h>
#include <sys/stat.h>
#include <sys/resource.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <X11/Xlib.h>

// ---------------------------------------------------------------------------
// Declarations of native implementations from already-linked translation
// units.  On Windows these are declared by lib/functions.h when MdFunc.cpp
// includes it with md_mode=decl; here we declare the ones we use explicitly.
// ---------------------------------------------------------------------------

FResult TraySetIcon(optl<StrArg> aIconFile, optl<int> aIconNumber, optl<BOOL> aFreeze);
FResult Reload();
ResultType Exit(optl<int> aExitCode);
ResultType ExitApp(optl<int> aExitCode);
FResult Suspend(optl<int> aMode);
FResult Pause(optl<int> aNewState);
FResult TrayTip(optl<StrArg> aText, optl<StrArg> aTitle, optl<StrArg> aOptions);
void ListLines(optl<int> aMode, int &aRetVal);
void Persistent(optl<BOOL> aNewValue, BOOL &aOldValue);
BOOL IsLabel(StrArg aName);
FResult GetKeyState(StrArg key_name, optl<StrArg> aMode, ResultToken &aResultToken);
int GetKeyVK(StrArg aKeyName);
int GetKeySC(StrArg aKeyName);
void GetKeyName(StrArg aKeyName, StrRet &aRetVal);
FResult HotIf(ExprTokenType *aCriterion);
FResult HotIfWinActive(optl<StrArg> aWinTitle, optl<StrArg> aWinText);
FResult HotIfWinNotActive(optl<StrArg> aWinTitle, optl<StrArg> aWinText);
FResult HotIfWinExist(optl<StrArg> aWinTitle, optl<StrArg> aWinText);
FResult HotIfWinNotExist(optl<StrArg> aWinTitle, optl<StrArg> aWinText);
void ScriptSleep(int aDelay);
void Critical(optl<StrArg> aSetting, int &aRetVal);
FResult Thread(StrArg aCommand, optl<int> aValue1, optl<int> aValue2);
void OutputDebug(StrArg aText);
FResult OnClipboardChange(IObject *aFunction, optl<int> aAddRemove);
FResult OnError(IObject *aFunction, optl<int> aAddRemove);
FResult OnExit(IObject *aFunction, optl<int> aAddRemove);
FResult OnMessage(UINT aNumber, IObject *aFunction, optl<int> aMaxThreads);
FResult BIF_Hotstring(StrArg name, ExprTokenType *aReplacement, optl<StrArg> aOnOff, ResultToken &aResultToken);
FResult DateAdd(StrArg aDateTime, double aTime, StrArg aTimeUnits, StrRet &aRetVal);
FResult DateDiff(StrArg aTime1, StrArg aTime2, StrArg aTimeUnits, __int64 &aRetVal);
FResult FileGetAttrib(optl<StrArg> aPath, StrRet &aRetVal);
FResult FileGetTime(optl<StrArg> aPath, optl<StrArg> aWhichTime, StrRet &aRetVal);
FResult FileGetSize(optl<StrArg> aPath, optl<StrArg> aUnits, __int64 &aRetVal);
FResult FileSetAttrib(StrArg aAttributes, optl<StrArg> aFilePattern, optl<StrArg> aMode);
FResult FileSetTime(optl<StrArg> aYYYYMMDD, optl<StrArg> aFilePattern, optl<StrArg> aWhichTime, optl<StrArg> aMode);
FResult StrSplit(StrArg aInputString, ExprTokenType *aDelimiters, optl<StrArg> aOmitChars, optl<int> aMaxParts, IObject *&aRetVal);
FResult DirCopy(StrArg aSource, StrArg aDest, optl<int> aOverwrite);
FResult DirMove(StrArg aSource, StrArg aDest, optl<StrArg> aFlag);

// ---------------------------------------------------------------------------
// Parameter helpers (BIF convention -> native signatures)
// ---------------------------------------------------------------------------

// NOTE: the returned optl<int> points at aSlot, so aSlot must remain alive
// until the native function call completes (i.e. it must be a local of the
// wrapper, not of this helper).
static optl<int> LinuxOptInt(int &aSlot, ExprTokenType *aParam[], int aParamCount, int aIndex)
{
	if (aIndex >= aParamCount || aParam[aIndex]->symbol == SYM_MISSING)
		return optl<int>(nullptr);
	aSlot = (int)TokenToInt64(*aParam[aIndex]);
	return optl<int>(aSlot);
}

static optl<StrArg> LinuxOptStr(ExprTokenType *aParam[], int aParamCount, int aIndex, TCHAR *aBuf, size_t aBufSize)
{
	aBuf[0] = L'\0';
	if (aIndex >= aParamCount || aParam[aIndex]->symbol == SYM_MISSING)
		return optl<StrArg>(nullptr);
	LPTSTR s = TokenToString(*aParam[aIndex], aBuf, nullptr);
	if (!s)
		s = aBuf;
	(void)aBufSize;
	return optl<StrArg>(s);
}

// Copy a StrRet result into aResultToken (same logic as MdFunc::Call).
static void LinuxCopyStrRet(ResultToken &aResultToken, StrRet &aRet)
{
	if (aRet.Value())
	{
		if (aRet.UsedMalloc())
			aResultToken.AcceptMem(const_cast<LPTSTR>(aRet.Value()), aRet.Length());
		else
			aResultToken.SetValue(const_cast<LPTSTR>(aRet.Value()), aRet.Length());
	}
	else
		aResultToken.SetValue(_T(""));
}

// Set the result token to a narrow UTF-8 string (defined further below).
static void LinuxSetPersistentStrResult(ResultToken &aResultToken, const char *aUtf8);

// ---------------------------------------------------------------------------
// Wrappers
// ---------------------------------------------------------------------------

BIF_DECL(BIF_Linux_NotImplemented)
{
	(void)aParam;
	(void)aParamCount;
	aResultToken.Error(_T("This built-in function has not been ported to Linux yet."));
}

// TrayTip -> org.freedesktop.Notifications.Notify (check_detail0821 §5-M5).
// The notification daemon is per-desktop (GNOME Shell, KDE, XFCE, ...); with
// no daemon the call is a silent no-op (TrayTip is documented as never a
// critical error).
extern bool LinuxTrayNotify(const wchar_t *aTitle, const wchar_t *aText);
// TraySetIcon -> StatusNotifierItem (check_detail0821 §5-M5).  Best effort:
// no watcher / no session bus is a silent no-op.
extern bool LinuxTraySetIcon(const wchar_t *aIconFile);

BIF_DECL(BIF_Linux_TrayTip)
{
	TCHAR text_buf[8192], title_buf[512];
	LPTSTR text = aParamCount > 0 ? TokenToString(*aParam[0], text_buf, nullptr) : nullptr;
	LPTSTR title = aParamCount > 1 ? TokenToString(*aParam[1], title_buf, nullptr) : nullptr;
	if (text && !*text)
		text = nullptr;
	if (title && !*title)
		title = nullptr;
	// Both empty => "remove the notification": a no-op on Linux (the daemon
	// owns the notification lifetime).  Empty title => omit the title line.
	if (!text && !title)
		return;
	LinuxTrayNotify(title ? title : L"AutoHotkey", text ? text : L"");
}

BIF_DECL(BIF_Linux_TraySetIcon)
{
	TCHAR icon_buf[1024];
	LPTSTR icon = aParamCount > 0 ? TokenToString(*aParam[0], icon_buf, nullptr) : nullptr;
	// Track the file/number for A_IconFile / A_IconNumber (check_detail0821 §5-M5).
	int icon_number = (aParamCount > 1 && aParam[1]->symbol == SYM_INTEGER)
		? (int)aParam[1]->value_int64 : 1;
	if (icon && *icon)
	{
		free(g_script.mCustomIconFile);
		g_script.mCustomIconFile = _tcsdup(icon);
		g_script.mCustomIconNumber = (UINT)icon_number;
	}
	// Omitted/empty FileName restores the port's own AutoHotkey icon (the
	// previous generic application-x-executable name was commonly rendered as
	// a terminal icon by desktop themes).
	LinuxTraySetIcon(icon && *icon ? icon : nullptr);
}

// CallbackCreate/CallbackFree (libffi closure backend, core_callback_linux.cpp).
extern FResult CallbackCreate(IObject *func, optl<StrArg> aOptions, optl<int> aParamCount, UINT_PTR &aRetVal);
extern FResult CallbackFree(UINT_PTR aCallback);

BIF_DECL(BIF_Linux_CallbackCreate)
{
	// CallbackCreate(Function [, Options, ParamCount])
	if (aParamCount < 1)
	{
		aResultToken.ParamError(0, aParam[0]);
		return;
	}
	IObject *func = TokenToObject(*aParam[0]);
	if (!func)
	{
		aResultToken.ParamError(0, aParam[0], _T("Object"));
		return;
	}
	TCHAR opt_buf[MAX_NUMBER_SIZE];
	optl<StrArg> options = (aParamCount > 1 && !ParamIndexIsOmitted(1))
		? optl<StrArg>(ParamIndexToString(1, opt_buf))
		: optl<StrArg>(nullptr);
	// optl<int> keeps a *reference* to its slot, so the slot must live at
	// function scope (not inside the if-block) to be valid when
	// CallbackCreate() consumes it below.
	int pc = 0;
	optl<int> param_count = optl<int>(nullptr);
	if (aParamCount > 2 && !ParamIndexIsOmitted(2))
	{
		pc = (int)ParamIndexToInt64(2);
		param_count = optl<int>(pc);
	}
	UINT_PTR retval = 0;
	FResult fr = CallbackCreate(func, options, param_count, retval);
	if (FAILED(fr))
	{
		FResultToError(aResultToken, aParam, aParamCount, fr, 0);
		return;
	}
	aResultToken.SetValue((__int64)retval);
}

BIF_DECL(BIF_Linux_CallbackFree)
{
	// CallbackFree(Address)
	if (aParamCount < 1)
	{
		aResultToken.ParamError(0, aParam[0]);
		return;
	}
	UINT_PTR addr = (UINT_PTR)ParamIndexToInt64(0);
	FResult fr = CallbackFree(addr);
	if (FAILED(fr))
		FResultToError(aResultToken, aParam, aParamCount, fr, 0);
}

BIF_DECL(BIF_Linux_Exit)
{
	(void)aResultToken;
	int slot = 0;
	aResultToken.SetResult(Exit(LinuxOptInt(slot, aParam, aParamCount, 0)));
}

BIF_DECL(BIF_Linux_ExitApp)
{
	int slot = 0;
	aResultToken.SetResult(ExitApp(LinuxOptInt(slot, aParam, aParamCount, 0)));
}

BIF_DECL(BIF_Linux_Sleep)
{
	ScriptSleep(ParamIndexToInt(0));
}

BIF_DECL(BIF_Linux_Persistent)
{
	int slot = 0;
	optl<int> new_value = LinuxOptInt(slot, aParam, aParamCount, 0);
	if (aParamCount > 0 && !ParamIndexIsOmitted(0))
	{
		slot = TokenToBOOL(*aParam[0]);
		new_value = optl<int>(slot);
	}
	BOOL old_value = 0;
	Persistent(new_value, old_value);
	aResultToken.SetValue(old_value ? 1 : 0);
}

BIF_DECL(BIF_Linux_ListLines)
{
	int slot = 0;
	optl<int> mode = LinuxOptInt(slot, aParam, aParamCount, 0);
	int ret = 0;
	ListLines(mode, ret);
	aResultToken.SetValue(ret);
}

BIF_DECL(BIF_Linux_Critical)
{
	TCHAR buf[64];
	optl<StrArg> setting = (aParamCount > 0 && !ParamIndexIsOmitted(0))
		? LinuxOptStr(aParam, aParamCount, 0, buf, sizeof(buf))
		: optl<StrArg>(nullptr);
	int ret = 0;
	Critical(setting, ret);
	aResultToken.SetValue(ret);
}

BIF_DECL(BIF_Linux_IsLabel)
{
	TCHAR name_buf[256];
	LPTSTR name = TokenToString(*aParam[0], name_buf, nullptr);
	aResultToken.SetValue(IsLabel(name ? name : name_buf) ? 1 : 0);
}

BIF_DECL(BIF_Linux_OutputDebug)
{
	TCHAR text_buf[4096];
	LPTSTR text = TokenToString(*aParam[0], text_buf, nullptr);
	OutputDebug(text ? text : text_buf);
}

BIF_DECL(BIF_Linux_Pause)
{
	int slot = 0;
	FResult fr = Pause(LinuxOptInt(slot, aParam, aParamCount, 0));
	if (FAILED(fr))
		FResultToError(aResultToken, aParam, aParamCount, fr, 0);
}

BIF_DECL(BIF_Linux_Suspend)
{
	int slot = 0;
	FResult fr = Suspend(LinuxOptInt(slot, aParam, aParamCount, 0));
	if (FAILED(fr))
		FResultToError(aResultToken, aParam, aParamCount, fr, 0);
}

BIF_DECL(BIF_Linux_Reload)
{
	(void)aParam;
	(void)aParamCount;
	// Linux restart protocol (upstream Windows signals the old process
	// through a registered restart message, which has no Linux
	// counterpart): launch a fresh interpreter with
	//   "/restart /script <script> /pid <pid>".
	// The new instance loads the script first and only then sends SIGTERM
	// to the old process, which exits through the EXIT_RELOAD path (OnExit
	// callbacks run with ExitReason "Reload").  If the new instance fails
	// to load the script it prints the error and exits WITHOUT signalling,
	// so the old script keeps running -- same semantics as upstream.
	if (!g_script.mFileSpec || !*g_script.mFileSpec)
	{
		aResultToken.Error(_T("Reload is not available for this script."), _T(""), ErrorPrototype::OS);
		return;
	}
	TCHAR arg_string[LINE_SIZE];
	_sntprintf(arg_string, _countof(arg_string), _T("/restart /script \"%s\" /pid %d")
		, g_script.mFileSpec, (int)getpid());
	ResultType result = g_script.ActionExec(g_script.mOurEXE, arg_string, g_WorkingDirOrig, true);
	if (result != OK)
		aResultToken.SetExitResult(FAIL);
	else
		aResultToken.SetValue(_T(""));
}

BIF_DECL(BIF_Linux_Thread)
{
	TCHAR cmd_buf[64];
	LPTSTR cmd = TokenToString(*aParam[0], cmd_buf, nullptr);
	int slot1 = 0, slot2 = 0;
	FResult fr = Thread(cmd ? cmd : cmd_buf
		, LinuxOptInt(slot1, aParam, aParamCount, 1)
		, LinuxOptInt(slot2, aParam, aParamCount, 2));
	if (FAILED(fr))
		FResultToError(aResultToken, aParam, aParamCount, fr, 0);
}

BIF_DECL(BIF_Linux_SetWorkingDir)
{
	TCHAR path_buf[4096];
	LPTSTR path = TokenToString(*aParam[0], path_buf, nullptr);
	FResult fr = SetWorkingDir(path ? path : path_buf);
	if (FAILED(fr))
		FResultToError(aResultToken, aParam, aParamCount, fr, 0);
}

BIF_DECL(BIF_Linux_EnvGet)
{
	TCHAR name_buf[1024];
	name_buf[0] = L'\0';
	LPTSTR name = TokenToString(*aParam[0], name_buf, nullptr);
	if (!name)
		name = name_buf;
	char narrow[2048];
	if (!name || wcstombs(narrow, name, sizeof(narrow)) == (size_t)-1)
	{
		aResultToken.SetValue(_T(""));
		return;
	}
	const char *value = std::getenv(narrow);
	if (!value)
	{
		aResultToken.SetValue(_T(""));
		return;
	}
	wchar_t wide[8192];
	size_t n = mbstowcs(wide, value, _countof(wide) - 1);
	if (n == (size_t)-1)
		n = 0;
	wide[n] = L'\0';
	LPTSTR persistent = (LPTSTR)SimpleHeap::Alloc((n + 1) * sizeof(TCHAR));
	tmemcpy(persistent, wide, n + 1);
	aResultToken.SetValue(persistent, n);
}

BIF_DECL(BIF_Linux_EnvSet)
{
	TCHAR name_buf[1024];
	name_buf[0] = L'\0';
	LPTSTR name = TokenToString(*aParam[0], name_buf, nullptr);
	if (!name)
		name = name_buf;
	TCHAR value_buf[8192];
	value_buf[0] = L'\0';
	LPTSTR value = aParamCount > 1 ? TokenToString(*aParam[1], value_buf, nullptr) : nullptr;
	if (!value)
		value = value_buf;
	char narrow_name[2048], narrow_value[16384];
	if (!name || wcstombs(narrow_name, name, sizeof(narrow_name)) == (size_t)-1)
		return;
	if (wcstombs(narrow_value, value, sizeof(narrow_value)) == (size_t)-1)
		return;
	setenv(narrow_name, narrow_value, 1);
}

BIF_DECL(BIF_Linux_GetKeyVK)
{
	TCHAR key_buf[256];
	LPTSTR key = TokenToString(*aParam[0], key_buf, nullptr);
	aResultToken.SetValue((__int64)GetKeyVK(key ? key : key_buf));
}

BIF_DECL(BIF_Linux_GetKeySC)
{
	TCHAR key_buf[256];
	LPTSTR key = TokenToString(*aParam[0], key_buf, nullptr);
	aResultToken.SetValue((__int64)GetKeySC(key ? key : key_buf));
}

BIF_DECL(BIF_Linux_GetKeyName)
{
	TCHAR key_buf[256];
	key_buf[0] = L'\0';
	LPTSTR key = TokenToString(*aParam[0], key_buf, nullptr);
	StrRet ret(aResultToken.buf);
	GetKeyName(key ? key : key_buf, ret);
	LinuxCopyStrRet(aResultToken, ret);
}

BIF_DECL(BIF_Linux_GetKeyState)
{
	TCHAR key_buf[256], mode_buf[64];
	LPTSTR key = TokenToString(*aParam[0], key_buf, nullptr);
	optl<StrArg> mode = (aParamCount > 1 && !ParamIndexIsOmitted(1))
		? LinuxOptStr(aParam, aParamCount, 1, mode_buf, sizeof(mode_buf))
		: optl<StrArg>(nullptr);
	FResult fr = GetKeyState(key ? key : key_buf, mode, aResultToken);
	if (FAILED(fr))
		FResultToError(aResultToken, aParam, aParamCount, fr, 0);
}

BIF_DECL(BIF_Linux_HotIf)
{
	FResult fr = HotIf(aParamCount > 0 ? aParam[0] : nullptr);
	if (FAILED(fr))
		FResultToError(aResultToken, aParam, aParamCount, fr, 0);
}

#define LINUX_HOTIF_WRAPPER(name, native) \
BIF_DECL(name) \
{ \
	TCHAR t1[1024], t2[1024]; \
	optl<StrArg> title = (aParamCount > 0 && !ParamIndexIsOmitted(0)) \
		? LinuxOptStr(aParam, aParamCount, 0, t1, sizeof(t1)) : optl<StrArg>(nullptr); \
	optl<StrArg> text = (aParamCount > 1 && !ParamIndexIsOmitted(1)) \
		? LinuxOptStr(aParam, aParamCount, 1, t2, sizeof(t2)) : optl<StrArg>(nullptr); \
	FResult fr = native(title, text); \
	if (FAILED(fr)) \
		FResultToError(aResultToken, aParam, aParamCount, fr, 0); \
}

LINUX_HOTIF_WRAPPER(BIF_Linux_HotIfWinActive, HotIfWinActive)
LINUX_HOTIF_WRAPPER(BIF_Linux_HotIfWinNotActive, HotIfWinNotActive)
LINUX_HOTIF_WRAPPER(BIF_Linux_HotIfWinExist, HotIfWinExist)
LINUX_HOTIF_WRAPPER(BIF_Linux_HotIfWinNotExist, HotIfWinNotExist)

#undef LINUX_HOTIF_WRAPPER

BIF_DECL(BIF_Linux_OnExit)
{
	IObject *fn = aParamCount > 0 ? TokenToObject(*aParam[0]) : nullptr;
	int slot = 0;
	FResult fr = OnExit(fn, LinuxOptInt(slot, aParam, aParamCount, 1));
	if (FAILED(fr))
		FResultToError(aResultToken, aParam, aParamCount, fr, 0);
}

BIF_DECL(BIF_Linux_OnError)
{
	IObject *fn = aParamCount > 0 ? TokenToObject(*aParam[0]) : nullptr;
	int slot = 0;
	FResult fr = OnError(fn, LinuxOptInt(slot, aParam, aParamCount, 1));
	if (FAILED(fr))
		FResultToError(aResultToken, aParam, aParamCount, fr, 0);
}

BIF_DECL(BIF_Linux_OnClipboardChange)
{
	IObject *fn = aParamCount > 0 ? TokenToObject(*aParam[0]) : nullptr;
	int slot = 0;
	FResult fr = OnClipboardChange(fn, LinuxOptInt(slot, aParam, aParamCount, 1));
	if (FAILED(fr))
		FResultToError(aResultToken, aParam, aParamCount, fr, 0);
}

BIF_DECL(BIF_Linux_DateAdd)
{
	TCHAR dt_buf[128], units_buf[16];
	LPTSTR dt = TokenToString(*aParam[0], dt_buf, nullptr);
	double time = TokenToDouble(*aParam[1]);
	LPTSTR units = TokenToString(*aParam[2], units_buf, nullptr);
	StrRet ret(aResultToken.buf);
	FResult fr = DateAdd(dt ? dt : dt_buf, time, units ? units : units_buf, ret);
	if (FAILED(fr))
	{
		FResultToError(aResultToken, aParam, aParamCount, fr, 0);
		return;
	}
	LinuxCopyStrRet(aResultToken, ret);
}

BIF_DECL(BIF_Linux_DateDiff)
{
	TCHAR d1_buf[128], d2_buf[128], units_buf[16];
	LPTSTR d1 = TokenToString(*aParam[0], d1_buf, nullptr);
	LPTSTR d2 = TokenToString(*aParam[1], d2_buf, nullptr);
	LPTSTR units = TokenToString(*aParam[2], units_buf, nullptr);
	__int64 ret = 0;
	FResult fr = DateDiff(d1 ? d1 : d1_buf, d2 ? d2 : d2_buf, units ? units : units_buf, ret);
	if (FAILED(fr))
	{
		FResultToError(aResultToken, aParam, aParamCount, fr, 0);
		return;
	}
	aResultToken.SetValue(ret);
}

BIF_DECL(BIF_Linux_StrSplit)
{
	TCHAR input_buf[8192], omit_buf[256];
	input_buf[0] = L'\0';
	LPTSTR input = TokenToString(*aParam[0], input_buf, nullptr);
	if (!input)
		input = input_buf;
	optl<StrArg> omit = (aParamCount > 2 && !ParamIndexIsOmitted(2))
		? LinuxOptStr(aParam, aParamCount, 2, omit_buf, sizeof(omit_buf))
		: optl<StrArg>(nullptr);
	int max_slot = 0;
	optl<int> max_parts = LinuxOptInt(max_slot, aParam, aParamCount, 3);
	IObject *ret = nullptr;
	FResult fr = StrSplit(input, aParamCount > 1 ? aParam[1] : nullptr, omit, max_parts, ret);
	if (FAILED(fr))
	{
		FResultToError(aResultToken, aParam, aParamCount, fr, 0);
		return;
	}
	if (ret)
		aResultToken.SetValue(ret);
	else
		aResultToken.SetValue(_T(""));
}

BIF_DECL(BIF_Linux_DirCopy)
{
	TCHAR src_buf[4096], dst_buf[4096];
	LPTSTR src = TokenToString(*aParam[0], src_buf, nullptr);
	LPTSTR dst = TokenToString(*aParam[1], dst_buf, nullptr);
	int slot = 0;
	FResult fr = DirCopy(src ? src : src_buf, dst ? dst : dst_buf, LinuxOptInt(slot, aParam, aParamCount, 2));
	if (FAILED(fr))
		FResultToError(aResultToken, aParam, aParamCount, fr, 0);
}

BIF_DECL(BIF_Linux_DirMove)
{
	TCHAR src_buf[4096], dst_buf[4096], flag_buf[64];
	LPTSTR src = TokenToString(*aParam[0], src_buf, nullptr);
	LPTSTR dst = TokenToString(*aParam[1], dst_buf, nullptr);
	optl<StrArg> flag = (aParamCount > 2 && !ParamIndexIsOmitted(2))
		? LinuxOptStr(aParam, aParamCount, 2, flag_buf, sizeof(flag_buf))
		: optl<StrArg>(nullptr);
	FResult fr = DirMove(src ? src : src_buf, dst ? dst : dst_buf, flag);
	if (FAILED(fr))
		FResultToError(aResultToken, aParam, aParamCount, fr, 0);
}

BIF_DECL(BIF_Linux_FileGetAttrib)
{
	TCHAR path_buf[4096];
	optl<StrArg> path = (aParamCount > 0 && !ParamIndexIsOmitted(0))
		? LinuxOptStr(aParam, aParamCount, 0, path_buf, sizeof(path_buf))
		: optl<StrArg>(nullptr);
	StrRet ret(aResultToken.buf);
	FResult fr = FileGetAttrib(path, ret);
	if (FAILED(fr)) { FResultToError(aResultToken, aParam, aParamCount, fr, 0); return; }
	LinuxCopyStrRet(aResultToken, ret);
}

BIF_DECL(BIF_Linux_FileGetTime)
{
	TCHAR path_buf[4096], which_buf[16];
	optl<StrArg> path = (aParamCount > 0 && !ParamIndexIsOmitted(0))
		? LinuxOptStr(aParam, aParamCount, 0, path_buf, sizeof(path_buf))
		: optl<StrArg>(nullptr);
	optl<StrArg> which = (aParamCount > 1 && !ParamIndexIsOmitted(1))
		? LinuxOptStr(aParam, aParamCount, 1, which_buf, sizeof(which_buf))
		: optl<StrArg>(nullptr);
	StrRet ret(aResultToken.buf);
	FResult fr = FileGetTime(path, which, ret);
	if (FAILED(fr)) { FResultToError(aResultToken, aParam, aParamCount, fr, 0); return; }
	LinuxCopyStrRet(aResultToken, ret);
}

BIF_DECL(BIF_Linux_FileGetSize)
{
	TCHAR path_buf[4096], units_buf[16];
	optl<StrArg> path = (aParamCount > 0 && !ParamIndexIsOmitted(0))
		? LinuxOptStr(aParam, aParamCount, 0, path_buf, sizeof(path_buf))
		: optl<StrArg>(nullptr);
	optl<StrArg> units = (aParamCount > 1 && !ParamIndexIsOmitted(1))
		? LinuxOptStr(aParam, aParamCount, 1, units_buf, sizeof(units_buf))
		: optl<StrArg>(nullptr);
	__int64 ret = 0;
	FResult fr = FileGetSize(path, units, ret);
	if (FAILED(fr)) { FResultToError(aResultToken, aParam, aParamCount, fr, 0); return; }
	aResultToken.SetValue(ret);
}

BIF_DECL(BIF_Linux_FileSetAttrib)
{
	TCHAR attr_buf[256], path_buf[4096], mode_buf[64];
	LPTSTR attr = TokenToString(*aParam[0], attr_buf, nullptr);
	if (!attr)
		attr = attr_buf;
	optl<StrArg> path = (aParamCount > 1 && !ParamIndexIsOmitted(1))
		? LinuxOptStr(aParam, aParamCount, 1, path_buf, sizeof(path_buf))
		: optl<StrArg>(nullptr);
	optl<StrArg> mode = (aParamCount > 2 && !ParamIndexIsOmitted(2))
		? LinuxOptStr(aParam, aParamCount, 2, mode_buf, sizeof(mode_buf))
		: optl<StrArg>(nullptr);
	FResult fr = FileSetAttrib(attr, path, mode);
	if (FAILED(fr)) { FResultToError(aResultToken, aParam, aParamCount, fr, 0); return; }
}

BIF_DECL(BIF_Linux_FileSetTime)
{
	TCHAR yyyymmdd_buf[64], path_buf[4096], which_buf[16], mode_buf[64];
	optl<StrArg> yyyymmdd = (aParamCount > 0 && !ParamIndexIsOmitted(0))
		? LinuxOptStr(aParam, aParamCount, 0, yyyymmdd_buf, sizeof(yyyymmdd_buf))
		: optl<StrArg>(nullptr);
	optl<StrArg> path = (aParamCount > 1 && !ParamIndexIsOmitted(1))
		? LinuxOptStr(aParam, aParamCount, 1, path_buf, sizeof(path_buf))
		: optl<StrArg>(nullptr);
	optl<StrArg> which = (aParamCount > 2 && !ParamIndexIsOmitted(2))
		? LinuxOptStr(aParam, aParamCount, 2, which_buf, sizeof(which_buf))
		: optl<StrArg>(nullptr);
	optl<StrArg> mode = (aParamCount > 3 && !ParamIndexIsOmitted(3))
		? LinuxOptStr(aParam, aParamCount, 3, mode_buf, sizeof(mode_buf))
		: optl<StrArg>(nullptr);
	FResult fr = FileSetTime(yyyymmdd, path, which, mode);
	if (FAILED(fr)) { FResultToError(aResultToken, aParam, aParamCount, fr, 0); return; }
}

// Run: launch Target (via Script::ActionExec -> POSIX CreateProcess/xdg-open).
// The optional 4th parameter is an output var which receives the PID.
BIF_DECL(BIF_Linux_Run)
{
	TCHAR target_buf[LINE_SIZE];
	target_buf[0] = L'\0';
	LPTSTR target = aParamCount > 0 ? TokenToString(*aParam[0], target_buf, nullptr) : nullptr;
	TCHAR wd_buf[4096], opt_buf[64];
	optl<StrArg> wd = (aParamCount > 1 && !ParamIndexIsOmitted(1))
		? LinuxOptStr(aParam, aParamCount, 1, wd_buf, sizeof(wd_buf)) : optl<StrArg>(nullptr);
	optl<StrArg> opts = (aParamCount > 2 && !ParamIndexIsOmitted(2))
		? LinuxOptStr(aParam, aParamCount, 2, opt_buf, sizeof(opt_buf)) : optl<StrArg>(nullptr);

	HANDLE hprocess = nullptr;
	ResultType result = g_script.ActionExec(target, nullptr, wd.value_or_null(), true
		, opts.value_or_null(), &hprocess, true, true);
	if (hprocess)
	{
		if (aParamCount > 3)
		{
			Var *out_var = TokenToOutputVar(*aParam[3]);
			if (out_var)
				out_var->Assign((__int64)GetProcessId(hprocess));
		}
		CloseHandle(hprocess);
	}
	if (result != OK)
	{
		aResultToken.SetExitResult(FAIL);
		return;
	}
	aResultToken.SetValue((__int64)0);
}
// InputBox: X11 input dialog when a display is available, stdin otherwise.
// Returns an object with .Value and .Result properties (per the v2 docs).
BIF_DECL(BIF_Linux_InputBox)
{
	TCHAR prompt_buf[4096], title_buf[1024], def_buf[4096];
	prompt_buf[0] = L'\0'; title_buf[0] = L'\0'; def_buf[0] = L'\0';
	LPTSTR prompt = aParamCount > 0 ? TokenToString(*aParam[0], prompt_buf, nullptr) : nullptr;
	LPTSTR title = aParamCount > 1 ? TokenToString(*aParam[1], title_buf, nullptr) : nullptr;
	LPTSTR def = aParamCount > 3 ? TokenToString(*aParam[3], def_buf, nullptr) : nullptr;
	wchar_t value_buf[8192];
	bool confirmed = LinuxInputBox(prompt ? prompt : L"", title ? title : L"AutoHotkey"
		, def ? def : L"", value_buf, _countof(value_buf));

	Object *obj = Object::Create();
	if (!obj)
	{
		aResultToken.SetValue(_T(""));
		return;
	}
	size_t vlen = wcslen(value_buf);
	LPTSTR persistent = (LPTSTR)SimpleHeap::Alloc((vlen + 1) * sizeof(TCHAR));
	tmemcpy(persistent, value_buf, vlen + 1);
	obj->SetOwnProp(_T("Value"), persistent);
	obj->SetOwnProp(_T("Result"), confirmed ? _T("OK") : _T("Cancel"));
	aResultToken.SetValue(obj);
}

// FileSelect: a path-entry dialog (X11, with a headless stdin fallback) --
// the Linux equivalent of the standard open/save dialog, which needs a
// desktop file-picker service.  The option letters D (folder pick), M
// (multi-select) and S (save dialog) and the numeric flag bits (1, 2, 8,
// 16, 32 per docs) are parsed and validated exactly as upstream, but the
// flags have no effect on the Linux dialog (documented in CHECK_REPORT).
// Returns the chosen path, an empty string on cancel, or an Array of paths
// when M is used (docs).
BIF_DECL(BIF_Linux_FileSelect)
{
	TCHAR opt_buf[128];
	opt_buf[0] = L'\0';
	LPTSTR options = (aParamCount > 0 && !ParamIndexIsOmitted(0))
		? TokenToString(*aParam[0], opt_buf, nullptr) : nullptr;
	if (!options)
		options = opt_buf;
	// One leading option letter (upstream: switch on the first character).
	bool pick_folder = false, multi = false, save = false;
	switch (ctoupper(*options))
	{
	case 'D': pick_folder = true; ++options; break;
	case 'M': multi = true; ++options; break;
	case 'S': save = true; ++options; break;
	}
	(void)save; // No separate save mode in the Linux entry dialog.
	// The remainder must be a number (upstream: else FR_E_ARG(0)).
	if (!IsNumeric(options, false, true))
	{
		FResultToError(aResultToken, aParam, aParamCount, FR_E_ARG(0), 0);
		return;
	}
	// Docs: the Filter parameter cannot be used with the D option.
	TCHAR filter_buf[1024];
	LPTSTR filter = (aParamCount > 3 && !ParamIndexIsOmitted(3))
		? TokenToString(*aParam[3], filter_buf, nullptr) : nullptr;
	if (pick_folder && filter && *filter)
	{
		FResultToError(aResultToken, aParam, aParamCount, FR_E_ARG(3), 0);
		return;
	}
	// RootDir\Filename (param 1, docs): an existing directory is used as
	// the initial directory; otherwise the part before the last '/' (or
	// '\' for Windows-style paths) is the initial directory and the rest
	// the default filename.
	std::wstring root;
	if (aParamCount > 1 && !ParamIndexIsOmitted(1))
	{
		TCHAR rd_buf[8192];
		LPTSTR rd = TokenToString(*aParam[1], rd_buf, nullptr);
		if (rd)
			root = rd;
	}
	std::wstring initial_dir, default_name;
	if (!root.empty())
	{
		DWORD attr = GetFileAttributes(root.c_str());
		if (attr != 0xFFFFFFFF && (attr & FILE_ATTRIBUTE_DIRECTORY))
			initial_dir = root;
		else
		{
			size_t slash = root.find_last_of(L"/\\");
			if (slash == std::wstring::npos)
				default_name = root;
			else
			{
				initial_dir = root.substr(0, slash);
				default_name = root.substr(slash + 1);
			}
		}
	}
	// Title (param 2): default "Select File/Folder - <script>" (docs).
	std::wstring title;
	if (aParamCount > 2 && !ParamIndexIsOmitted(2))
	{
		TCHAR tb[1024];
		LPTSTR t = TokenToString(*aParam[2], tb, nullptr);
		if (t)
			title = t;
	}
	if (title.empty())
		title = std::wstring(pick_folder ? L"Select Folder - " : L"Select File - ")
			+ g_script.DefaultDialogTitle();
	std::wstring def = default_name.empty()
		? initial_dir
		: (initial_dir.empty() ? default_name : (initial_dir + L"/" + default_name));
	wchar_t result_buf[16384];
	bool confirmed = LinuxFileDialog(title.c_str(), title.c_str(), def.c_str()
		, result_buf, _countof(result_buf), multi);
	if (!confirmed)
	{
		// Docs: an empty string (or empty array for M) on cancel.
		if (multi)
		{
			Array *arr = Array::Create();
			if (arr)
				aResultToken.SetValue(arr);
			else
				aResultToken.SetValue(_T(""));
		}
		else
			aResultToken.SetValue(_T(""));
		return;
	}
	if (multi)
	{
		Array *arr = Array::Create();
		if (arr)
		{
			std::wstring all(result_buf);
			size_t start = 0;
			for (size_t i = 0; i <= all.size(); ++i)
				if (i == all.size() || all[i] == L'\n')
				{
					if (i > start)
						arr->Append(all.substr(start, i - start).c_str());
					start = i + 1;
				}
		}
		if (arr)
			aResultToken.SetValue(arr);
		else
			aResultToken.SetValue(_T(""));
		return;
	}
	LinuxWinSetPersistentEx(aResultToken, std::wstring(result_buf));
}

// DirSelect: same dialog mechanism as FileSelect (docs: "Displays a
// standard dialog that allows the user to select a folder").  The
// StartingFolder parameter may contain "root*initial": the part after the
// '*' is the initial folder (docs).  Options (0/1/2, default 1) select
// whether the native dialog offers a "create new folder" button / edit box;
// they are accepted for compatibility but have no effect on the Linux
// path-entry dialog (documented).
BIF_DECL(BIF_Linux_DirSelect)
{
	std::wstring spec;
	if (aParamCount > 0 && !ParamIndexIsOmitted(0))
	{
		TCHAR sb[8192];
		LPTSTR s = TokenToString(*aParam[0], sb, nullptr);
		if (s)
			spec = s;
	}
	std::wstring root, initial;
	size_t star = spec.find(L'*');
	if (star != std::wstring::npos)
	{
		root = spec.substr(0, star);
		initial = spec.substr(star + 1);
	}
	else
		root = spec;
	// Prompt (param 2): default "Select Folder - <script>" (docs).
	std::wstring title;
	if (aParamCount > 2 && !ParamIndexIsOmitted(2))
	{
		TCHAR tb[1024];
		LPTSTR t = TokenToString(*aParam[2], tb, nullptr);
		if (t)
			title = t;
	}
	if (title.empty())
		title = std::wstring(L"Select Folder - ") + g_script.DefaultDialogTitle();
	// Initial selection: the folder after '*', else the starting folder,
	// else the user's home directory (the Linux counterpart of "My
	// Documents" from the docs).
	std::wstring def = initial;
	if (def.empty())
		def = root;
	if (def.empty())
	{
		const char *home = getenv("HOME");
		if (home)
		{
			wchar_t home_wide[4096];
			if (mbstowcs(home_wide, home, _countof(home_wide)) != (size_t)-1)
				def = home_wide;
		}
	}
	wchar_t result_buf[16384];
	bool confirmed = LinuxFileDialog(title.c_str(), title.c_str(), def.c_str()
		, result_buf, _countof(result_buf), false);
	if (!confirmed)
	{
		// Docs: empty string when the user cancels.
		aResultToken.SetValue(_T(""));
		return;
	}
	LinuxWinSetPersistentEx(aResultToken, std::wstring(result_buf));
}

// ---------------------------------------------------------------------------
// OnMessage / SendMessage / PostMessage / MenuSelect
// ---------------------------------------------------------------------------
//
// Windows message plumbing.  X11 has no Win32 message queue, so:
//   - OnMessage registers/unregisters message monitors exactly like
//     upstream (same functor validation, same MaxThreads semantics via the
//     upstream OnMessage BIF); the callbacks can never fire because the
//     port never receives Win32 messages (documented).
//   - SendMessage/PostMessage resolve the target window/control per docs
//     (TargetError if not found), validate MsgNumber (0..0xFFFFFFFF,
//     ValueError otherwise) and the wParam/lParam values (integer or an
//     object with a Ptr property, per docs), then return 0 -- the reply a
//     window gives for a message it does not handle (DefWindowProc
//     default).  The Timeout parameter is accepted but there is nothing to
//     wait for (documented).
//   - MenuSelect resolves the target window per docs and then raises the
//     documented "does not have a standard Win32 menu" TargetError: no X11
//     window can have a standard Win32 menu (documented).

// OnMessage: register/query/unregister a message monitor (upstream BIF).
BIF_DECL(BIF_Linux_OnMessage)
{
	// (In, UInt32, Number): docs "between 0 and 4294967295 (0xFFFFFFFF)".
	__int64 msg = TokenToInt64(*aParam[0]);
	if (msg < 0 || msg > 0xFFFFFFFFLL)
	{
		FResultToError(aResultToken, aParam, aParamCount, FR_E_ARG(0), 0);
		return;
	}
	// (In, Object, Function).
	IObject *fn = TokenToObject(*aParam[1]);
	if (!fn)
	{
		aResultToken.ParamError(1, aParam[1], _T("Object"));
		return;
	}
	int max_threads_slot = 0;
	FResult fr = OnMessage((UINT)msg, fn, LinuxOptInt(max_threads_slot, aParam, aParamCount, 2));
	if (FAILED(fr))
		FResultToError(aResultToken, aParam, aParamCount, fr, 0);
}

// wParam/lParam for the message functions: an integer or an object with a
// Ptr property such as a Buffer (the upstream md signatures are Variant for
// both SendMessage and PostMessage, and PostSendMessage accepts objects for
// both).
static bool LinuxMsgParamValue(ExprTokenType *aTok, __int64 &aOut, ResultToken &aResultToken)
{
	aOut = 0;
	if (!aTok)
		return true;
	if (aTok->symbol == SYM_OBJECT)
	{
		// Docs: "an object with a Ptr property, such as a Buffer".
		UINT_PTR ptr = 0;
		if (GetObjectPtrProperty(aTok->object, _T("Ptr"), ptr, aResultToken, true) == OK)
		{
			aOut = (__int64)ptr;
			return true;
		}
		return false;
	}
	if (aTok->symbol == SYM_MISSING)
		return true; // Omitted: 0 is sent (docs).
	aOut = TokenToInt64(*aTok);
	return true;
}

// Shared body of SendMessage/PostMessage: validate and resolve; the actual
// "send" is a no-op returning 0 (see module comment).
static void LinuxPostSendMessage(ResultToken &aResultToken, ExprTokenType *aParam[], int aParamCount
	, bool aReturnResult)
{
	__int64 msg = TokenToInt64(*aParam[0]);
	if (msg < 0 || msg > 0xFFFFFFFFLL)
	{
		FResultToError(aResultToken, aParam, aParamCount, FR_E_ARG(0), 0);
		return;
	}
	__int64 wp = 0, lp = 0;
	for (int i = 1; i <= 2; ++i)
		if (i < aParamCount && !LinuxMsgParamValue(aParam[i], i == 1 ? wp : lp, aResultToken))
		{
			// Docs: "Each parameter must be an integer [or an object with
			// a Ptr property]".
			FResultToError(aResultToken, aParam, aParamCount, FR_E_ARG(i), 0);
			return;
		}
	// ControlID (3) is optional: omitted -> the message goes to the target
	// window itself (docs).  Timeout (8) is accepted and ignored (nothing
	// to wait for on Linux; documented).
	Window target = 0, control = 0;
	if (!LinuxCtrlTargetEx(aResultToken, aParam, aParamCount, *g, 3, 4, target, control))
		return;
	(void)wp; (void)lp;
	if (aReturnResult)
		aResultToken.SetValue((__int64)0); // The DefWindowProc default reply.
}

BIF_DECL(BIF_Linux_SendMessage)
{
	LinuxPostSendMessage(aResultToken, aParam, aParamCount, true);
}

BIF_DECL(BIF_Linux_PostMessage)
{
	LinuxPostSendMessage(aResultToken, aParam, aParamCount, false);
}

// MenuSelect: docs "A TargetError is thrown if the window or control could
// not be found, or does not have a standard Win32 menu."
BIF_DECL(BIF_Linux_MenuSelect)
{
	Window w = LinuxWinFindTargetEx(aResultToken, aParam, aParamCount, *g, 0, 1, 9);
	if (!w)
		return; // TargetError already raised.
	// No X11 window has a standard Win32 menu (upstream: ERR_WINDOW_HAS_NO_MENU).
	aResultToken.Error(ERR_WINDOW_HAS_NO_MENU, _T(""), ErrorPrototype::Target);
}

// ---------------------------------------------------------------------------
// Hotstring / RunAs
// ---------------------------------------------------------------------------
//
//   - Hotstring: adapter to the upstream BIF (same parsing, option and
//     OnOffToggle validation, same registry: FindHotstring/AddHotstring and
//     the EndChars/MouseReset/Reset subfunctions).  Hotstrings are
//     registered but never expand, because the port has no keyboard hook
//     that could watch typed text (documented).
//   - RunAs: stores the credentials in the Script object exactly like
//     upstream.  Launching with credentials is not possible on Linux (no
//     CreateProcessWithLogonW): the port's DoRunAs shim reports failure, so
//     a subsequent Run/RunWait raises the documented "Launch Error
//     (possibly related to RunAs)" (documented).

BIF_DECL(BIF_Linux_Hotstring)
{
	// (In, String, String): the hotstring trigger with colons/options.
	TCHAR name_buf[1024];
	name_buf[0] = L'\0';
	LPTSTR name = TokenToString(*aParam[0], name_buf, nullptr);
	// (In_Opt, Variant, Replacement): text or function object.
	ExprTokenType *repl = (aParamCount > 1 && !ParamIndexIsOmitted(1)) ? aParam[1] : nullptr;
	// (In_Opt, String, OnOffToggle): On/Off/Toggle/1/0/-1.
	TCHAR toggle_buf[64];
	optl<StrArg> toggle = (aParamCount > 2 && !ParamIndexIsOmitted(2))
		? LinuxOptStr(aParam, aParamCount, 2, toggle_buf, sizeof(toggle_buf))
		: optl<StrArg>(nullptr);
	FResult fr = BIF_Hotstring(name ? name : name_buf, repl, toggle, aResultToken);
	if (FAILED(fr))
		FResultToError(aResultToken, aParam, aParamCount, fr, 0);
	// Upstream only manifests when the Windows keybd hook is absent; the
	// Linux typed-text capture needs to know about every registration and
	// On/Off toggle.
	LinuxHotkeyStateChanged();
}BIF_DECL(BIF_Linux_RunAs)
{
	// (In_Opt, String, User), (In_Opt, String, Password), (In_Opt, String,
	// Domain); all omitted -> the RunAs feature is turned off (docs).
	// Stores the credentials like upstream RunAs; they cannot actually be
	// used on Linux (no logon API) -- see the module comment.
	TCHAR user_buf[512], pass_buf[512], domain_buf[512];
	g_script.mRunAsUser = LinuxOptStr(aParam, aParamCount, 0, user_buf, sizeof(user_buf)).value_or_empty();
	g_script.mRunAsPass = LinuxOptStr(aParam, aParamCount, 1, pass_buf, sizeof(pass_buf)).value_or_empty();
	g_script.mRunAsDomain = LinuxOptStr(aParam, aParamCount, 2, domain_buf, sizeof(domain_buf)).value_or_empty();
}

// ---------------------------------------------------------------------------
// GuiFromHwnd / GuiCtrlFromHwnd / MenuFromHandle
// ---------------------------------------------------------------------------
//
// Docs: "This function returns the Gui object associated with the specified
// HWND, or an empty string if there isn't one or the HWND is invalid" (same
// for GuiControl objects); MenuFromHandle: "or an empty string if the
// handle is invalid or no Menu object corresponds to it".
//
// GuiFromHwnd/GuiCtrlFromHwnd are resolved against the real GTK window map
// (script_gui_linux.cpp): every shown Gui registers its widget, so a script
// can look itself up by Hwnd (and by default also the parent Gui of a
// control).  MenuFromHandle stays "" because Linux has no Win32 HMENU to map
// (the GTK menu objects are not handle-addressable, so no handle can ever
// correspond to a script Menu object -- the documented empty-string result).

// The reverse mapping lives in the GTK GUI backend:
extern void GuiFromHwnd(UINT aHwnd, optl<BOOL> aRecurse, IObject *&aGui);
extern void GuiCtrlFromHwnd(UINT aHwnd, IObject *&aGuiCtrl);

BIF_DECL(BIF_Linux_GuiFromHwnd)
{
	UINT hwnd = (UINT)TokenToInt64(*aParam[0]);
	BOOL recurse = FALSE;
	optl<BOOL> ropt = optl<BOOL>(nullptr);
	if (aParamCount > 1 && !ParamIndexIsOmitted(1))
	{
		recurse = (BOOL)TokenToInt64(*aParam[1]);
		ropt = optl<BOOL>(recurse);
	}
	IObject *gui = nullptr;
	GuiFromHwnd(hwnd, ropt, gui);
	if (gui)
		aResultToken.SetValue(gui);  // sets obj + SYM_OBJECT (AddRef'd).
	else
		aResultToken.SetValue(_T(""));
}

BIF_DECL(BIF_Linux_GuiCtrlFromHwnd)
{
	UINT hwnd = (UINT)TokenToInt64(*aParam[0]);
	IObject *ctrl = nullptr;
	GuiCtrlFromHwnd(hwnd, ctrl);
	if (ctrl)
		aResultToken.SetValue(ctrl);  // sets obj + SYM_OBJECT (AddRef'd).
	else
		aResultToken.SetValue(_T(""));
}

BIF_DECL(BIF_Linux_MenuFromHandle)
{
	(void)TokenToInt64(*aParam[0]);
	aResultToken.SetValue(_T(""));
}

// ---------------------------------------------------------------------------
// Process functions (kill() + /proc based)
// ---------------------------------------------------------------------------

static std::string LinuxNarrowOf(ExprTokenType *aParam[], int aParamCount, int aIndex, char *aBuf, size_t aBufSize)
{
	aBuf[0] = '\0';
	if (aIndex >= aParamCount)
		return std::string();
	TCHAR wbuf[512];
	wbuf[0] = L'\0';
	LPTSTR s = TokenToString(*aParam[aIndex], wbuf, nullptr);
	if (!s)
		s = wbuf;
	if (!s)
		return std::string();
	wcstombs(aBuf, s, aBufSize);
	aBuf[aBufSize - 1] = '\0';
	return std::string(aBuf);
}

// Returns true if the pid exists and is not a zombie (dead-but-unreaped).
static bool LinuxProcessAlive(pid_t aPid)
{
	if (aPid <= 0 || kill(aPid, 0) != 0)
		return false;
	// A zombie still responds to kill(pid, 0); check its state in /proc.
	char path[64];
	snprintf(path, sizeof(path), "/proc/%d/stat", (int)aPid);
	std::ifstream f(path);
	if (!f)
		return false;
	std::string stat;
	std::getline(f, stat);
	size_t close_paren = stat.rfind(')');
	if (close_paren == std::string::npos || close_paren + 2 >= stat.size())
		return false;
	char state = stat[close_paren + 2]; // After ") ".
	return state != 'Z' && state != 'X';
}

// Returns the pid of the process matching aNameOrPid, or 0.
static pid_t LinuxFindProcess(const char *aNameOrPid)
{
	if (!aNameOrPid || !*aNameOrPid)
		return 0;
	if (isdigit((unsigned char)aNameOrPid[0]))
	{
		pid_t pid = (pid_t)atol(aNameOrPid);
		if (LinuxProcessAlive(pid))
			return pid;
		return 0;
	}
	std::string wanted(aNameOrPid);
	// Strip any path to compare against the process name.
	size_t slash = wanted.find_last_of('/');
	if (slash != std::string::npos)
		wanted = wanted.substr(slash + 1);
	DIR *dir = opendir("/proc");
	if (!dir)
		return 0;
	pid_t found = 0;
	while (struct dirent *ent = readdir(dir))
	{
		if (!isdigit((unsigned char)ent->d_name[0]))
			continue;
		std::string path = std::string("/proc/") + ent->d_name + "/comm";
		std::ifstream f(path.c_str());
		if (!f)
			continue;
		std::string name;
		std::getline(f, name);
		if (!name.empty() && name.back() == '\n')
			name.pop_back();
		if (name == wanted)
		{
			pid_t candidate = (pid_t)atol(ent->d_name);
			if (LinuxProcessAlive(candidate))
			{
				found = candidate;
				break;
			}
		}
	}
	closedir(dir);
	return found;
}

static void LinuxSetResultPid(ResultToken &aResultToken, pid_t aPid)
{
	aResultToken.SetValue((__int64)aPid);
}

BIF_DECL(BIF_Linux_ProcessExist)
{
	pid_t pid = 0;
	if (aParamCount > 0 && !ParamIndexIsOmitted(0))
	{
		char name_buf[512];
		pid = LinuxFindProcess(LinuxNarrowOf(aParam, aParamCount, 0, name_buf, sizeof(name_buf)).c_str());
	}
	else
		pid = getpid(); // Docs: omitting the parameter means the script's own PID.
	LinuxSetResultPid(aResultToken, pid);
}

BIF_DECL(BIF_Linux_ProcessClose)
{
	char name_buf[512];
	std::string target = LinuxNarrowOf(aParam, aParamCount, 0, name_buf, sizeof(name_buf));
	pid_t pid = LinuxFindProcess(target.c_str());
	if (pid > 0)
		kill(pid, SIGTERM);
	LinuxSetResultPid(aResultToken, pid);
}

BIF_DECL(BIF_Linux_ProcessWait)
{
	char name_buf[512];
	std::string target = LinuxNarrowOf(aParam, aParamCount, 0, name_buf, sizeof(name_buf));
	double timeout = aParamCount > 1 ? TokenToDouble(*aParam[1]) : 0;
	double waited = 0;
	pid_t pid = 0;
	for (;;)
	{
		pid = LinuxFindProcess(target.c_str());
		if (pid)
			break;
		if (timeout > 0 && waited >= timeout)
			break;
		ScriptSleep(50);
		waited += 0.05;
	}
	LinuxSetResultPid(aResultToken, pid);
}

BIF_DECL(BIF_Linux_ProcessWaitClose)
{
	char name_buf[512];
	std::string target = LinuxNarrowOf(aParam, aParamCount, 0, name_buf, sizeof(name_buf));
	double timeout = aParamCount > 1 ? TokenToDouble(*aParam[1]) : 0;
	double waited = 0;
	pid_t pid = 0;
	for (;;)
	{
		pid = LinuxFindProcess(target.c_str());
		if (!pid)
			break;
		if (timeout > 0 && waited >= timeout)
			break;
		ScriptSleep(50);
		waited += 0.05;
	}
	LinuxSetResultPid(aResultToken, pid);
}

BIF_DECL(BIF_Linux_ProcessSetPriority)
{
	TCHAR prio_wide[64];
	prio_wide[0] = L'\0';
	LPTSTR prio_s = TokenToString(*aParam[0], prio_wide, nullptr);
	if (!prio_s)
		prio_s = prio_wide;
	int nice = 0;
	if (!_tcsicmp(prio_s, _T("Low"))) nice = 19;
	else if (!_tcsicmp(prio_s, _T("BelowNormal"))) nice = 10;
	else if (!_tcsicmp(prio_s, _T("Normal"))) nice = 0;
	else if (!_tcsicmp(prio_s, _T("AboveNormal"))) nice = -5;
	else if (!_tcsicmp(prio_s, _T("High"))) nice = -11;
	else if (!_tcsicmp(prio_s, _T("Realtime"))) nice = -20;
	char name_buf[512];
	std::string target = LinuxNarrowOf(aParam, aParamCount, 1, name_buf, sizeof(name_buf));
	// Docs: "If PIDOrName is omitted, the script's own PID is used."
	pid_t pid = target.empty() ? (pid_t)getpid() : LinuxFindProcess(target.c_str());
	if (pid > 0)
		setpriority(PRIO_PROCESS, (id_t)pid, nice);
	LinuxSetResultPid(aResultToken, pid);
}

// ---------------------------------------------------------------------------
// Drive functions (statvfs + /proc/mounts based)
// ---------------------------------------------------------------------------

struct LinuxMountInfo
{
	std::string device;
	std::string mount_point;
	std::string fstype;
};

static std::vector<LinuxMountInfo> LinuxMounts()
{
	std::vector<LinuxMountInfo> mounts;
	std::ifstream f("/proc/mounts");
	std::string line;
	while (std::getline(f, line))
	{
		// device mount_point fstype options dump pass
		std::istringstream ss(line);
		LinuxMountInfo mi;
		ss >> mi.device >> mi.mount_point >> mi.fstype;
		if (!mi.mount_point.empty())
			mounts.push_back(mi);
	}
	return mounts;
}

static std::string LinuxMountOf(const std::vector<LinuxMountInfo> &aMounts, const std::string &aPath)
{
	std::string best;
	for (auto &m : aMounts)
		if (aPath == m.mount_point || (aPath.size() > m.mount_point.size()
			&& aPath.compare(0, m.mount_point.size(), m.mount_point) == 0
			&& (m.mount_point == "/" || aPath[m.mount_point.size()] == '/')))
			if (m.mount_point.size() > best.size())
				best = m.mount_point;
	return best;
}

BIF_DECL(BIF_Linux_DriveGetType)
{
	char path_buf[4096];
	std::string path = LinuxNarrowOf(aParam, aParamCount, 0, path_buf, sizeof(path_buf));
	if (path.empty())
		path = "/";
	std::string result = "";
	auto mounts = LinuxMounts();
	std::string mp = LinuxMountOf(mounts, path);
	for (auto &m : mounts)
	{
		if (m.mount_point != mp)
			continue;
		if (m.fstype == "tmpfs" || m.fstype == "ramfs")
			result = "RAMDisk";
		else if (m.fstype == "nfs" || m.fstype == "nfs4" || m.fstype == "cifs" || m.fstype == "smb3")
			result = "Network";
		else if (m.device.rfind("/dev/sr", 0) == 0 || m.fstype == "iso9660" || m.fstype == "udf")
			result = "CDROM";
		else if (m.device.rfind("/dev/sd", 0) == 0 || m.device.rfind("/dev/nvme", 0) == 0
			|| m.device.rfind("/dev/vd", 0) == 0 || m.device.rfind("/dev/mmc", 0) == 0)
			result = "Fixed";
		else if (m.device.rfind("/dev/fd", 0) == 0)
			result = "Removable";
		else
			result = "Fixed";
		break;
	}
	// No mount found for the path: check statvfs works at all.
	if (result.empty())
	{
		struct statvfs sv;
		if (statvfs(path.c_str(), &sv) == 0)
			result = "Fixed";
	}
	LinuxSetPersistentStrResult(aResultToken, result.c_str());
}

BIF_DECL(BIF_Linux_DriveGetList)
{
	char type_buf[64];
	std::string type = LinuxNarrowOf(aParam, aParamCount, 0, type_buf, sizeof(type_buf));
	auto mounts = LinuxMounts();
	std::string list;
	for (auto &m : mounts)
	{
		bool skip_virtual = m.fstype == "proc" || m.fstype == "sysfs" || m.fstype == "devpts"
			|| m.fstype == "cgroup" || m.fstype == "cgroup2" || m.fstype == "overlay"
			|| m.fstype == "mqueue" || m.fstype == "securityfs" || m.fstype == "debugfs"
			|| m.fstype == "pstore" || m.fstype == "tracefs" || m.fstype == "fusectl"
			|| m.fstype == "configfs" || m.fstype == "bpf" || m.fstype == "binfmt_misc"
			|| m.fstype == "hugetlbfs" || m.fstype == "autofs";
		if (skip_virtual)
			continue;
		if (type.empty())
			list += m.mount_point + "\n";
		else
		{
			// Match against the AHK drive type names.
			std::string t;
			if (m.fstype == "tmpfs" || m.fstype == "ramfs") t = "RAMDisk";
			else if (m.fstype == "nfs" || m.fstype == "nfs4" || m.fstype == "cifs") t = "Network";
			else if (m.device.rfind("/dev/sr", 0) == 0) t = "CDROM";
			else if (m.device.rfind("/dev/fd", 0) == 0) t = "Removable";
			else if (m.device.rfind("/dev/", 0) == 0) t = "Fixed";
			else t = "Fixed";
			if (t == type)
				list += m.mount_point + "\n";
		}
	}
	LinuxSetPersistentStrResult(aResultToken, list.c_str());
}

BIF_DECL(BIF_Linux_DriveGetSpaceFree)
{
	char path_buf[4096];
	std::string path = LinuxNarrowOf(aParam, aParamCount, 0, path_buf, sizeof(path_buf));
	if (path.empty())
		path = "/";
	struct statvfs sv;
	__int64 result = 0;
	if (statvfs(path.c_str(), &sv) == 0)
		result = (__int64)sv.f_bavail * (__int64)sv.f_frsize;
	aResultToken.SetValue(result);
}

BIF_DECL(BIF_Linux_DriveGetCapacity)
{
	char path_buf[4096];
	std::string path = LinuxNarrowOf(aParam, aParamCount, 0, path_buf, sizeof(path_buf));
	if (path.empty())
		path = "/";
	struct statvfs sv;
	__int64 result = 0;
	if (statvfs(path.c_str(), &sv) == 0)
		result = (__int64)sv.f_blocks * (__int64)sv.f_frsize;
	aResultToken.SetValue(result);
}

BIF_DECL(BIF_Linux_DriveGetFilesystem)
{
	char path_buf[4096];
	std::string path = LinuxNarrowOf(aParam, aParamCount, 0, path_buf, sizeof(path_buf));
	if (path.empty())
		path = "/";
	auto mounts = LinuxMounts();
	std::string mp = LinuxMountOf(mounts, path);
	std::string fs = "";
	for (auto &m : mounts)
		if (m.mount_point == mp)
		{
			fs = m.fstype;
			break;
		}
	LinuxSetPersistentStrResult(aResultToken, fs.c_str());
}

BIF_DECL(BIF_Linux_DriveGetStatus)
{
	char path_buf[4096];
	std::string path = LinuxNarrowOf(aParam, aParamCount, 0, path_buf, sizeof(path_buf));
	if (path.empty())
		path = "/";
	struct statvfs sv;
	std::string status = statvfs(path.c_str(), &sv) == 0 ? "Ready" : "NotReady";
	LinuxSetPersistentStrResult(aResultToken, status.c_str());
}

BIF_DECL(BIF_Linux_DriveGetLabel) { LinuxSetPersistentStrResult(aResultToken, ""); }
BIF_DECL(BIF_Linux_DriveGetSerial) { aResultToken.SetValue((__int64)0); }
BIF_DECL(BIF_Linux_DriveGetStatusCD) { LinuxSetPersistentStrResult(aResultToken, ""); }

// ---------------------------------------------------------------------------
// SoundBeep: terminal bell with display (X11) fallback
// ---------------------------------------------------------------------------

BIF_DECL(BIF_Linux_SoundBeep)
{
	int frequency = aParamCount > 0 ? (int)TokenToInt64(*aParam[0]) : 523;
	int duration = aParamCount > 1 ? (int)TokenToInt64(*aParam[1]) : 150;
	(void)frequency;
	if (LinuxHasDisplay())
	{
		extern void LinuxXBell();
		LinuxXBell();
	}
	else
	{
		std::printf("\a");
		std::fflush(stdout);
	}
	if (duration > 0)
		ScriptSleep(duration);
	aResultToken.SetValue((__int64)0);
}

// ---------------------------------------------------------------------------
// A_Clipboard: system clipboard (X11 CLIPBOARD selection, Wayland data
// device, process-internal fallback headless) -- core_clipboard_linux.cpp.
// ---------------------------------------------------------------------------

void BIV_Clipboard(ResultToken &aResultToken, LPTSTR aVarName)
{
	(void)aVarName;
	std::wstring text;
	LinuxClipboardGetText(text);
	LPTSTR persistent = (LPTSTR)SimpleHeap::Alloc((text.size() + 1) * sizeof(TCHAR));
	tmemcpy(persistent, text.c_str(), text.size() + 1);
	aResultToken.SetValue(persistent, text.size());
}

void BIV_Clipboard_Set(ResultToken &aResultToken, LPTSTR aVarName, ExprTokenType &aValue)
{
	(void)aResultToken; (void)aVarName;
	TCHAR buf[65536];
	buf[0] = L'\0';
	size_t len = 0;
	LPTSTR s = TokenToString(aValue, buf, &len);
	if (!s)
		s = buf;
	LinuxClipboardSetText(s ? s : L"");
}

BIF_DECL(BIF_Linux_ClipWait)
{
	double timeout = aParamCount > 0 ? TokenToDouble(*aParam[0]) : 0;
	double waited = 0;
	std::wstring text;
	LinuxClipboardGetText(text);
	bool non_empty = !text.empty();
	while (!non_empty && (timeout <= 0 || waited < timeout))
	{
		ScriptSleep(50);
		waited += 0.05;
		LinuxClipboardGetText(text);
		non_empty = !text.empty();
	}
	aResultToken.SetValue(non_empty ? 1 : 0);
}

// ImeGetState() -> string (check0820 §2: ibus/fcitx active-state detection).
// Returns a compact "flux|group" string:
//   flux   "ibus" | "fcitx5" | "none"   (framework with the current owner on
//          the session bus; the *active* input-method framework, if any)
//   group  current XKB group index on X11 (-1 with no working display;
//          IMEs typically engage as group >= 1, 0 = the base layout)
// Example: ImeGetState() = "ibus|0"  (ibus running, base layout group).
BIF_DECL(BIF_Linux_ImeGetState)
{
	std::string r;
	switch (LinuxImeFramework())
	{
	case LINUX_IME_IBUS:   r = "ibus";   break;
	case LINUX_IME_FCITX5: r = "fcitx5"; break;
	default:               r = "none";   break;
	}
	char g[16];
	snprintf(g, sizeof(g), "|%d", LinuxImeXkbGroup());
	r += g;
	LinuxSetPersistentStrResult(aResultToken, r.c_str());
}

// ---------------------------------------------------------------------------
// INI functions (simple UTF-8 INI parser)
// ---------------------------------------------------------------------------

static bool LinuxReadFileUtf8(const char *aPath, std::string &aOut)
{
	std::ifstream f(aPath, std::ios::binary);
	if (!f)
		return false;
	std::ostringstream ss;
	ss << f.rdbuf();
	aOut = ss.str();
	return true;
}

static void LinuxSetResultUtf8(ResultToken &aResultToken, const std::string &aUtf8)
{
	std::wstring w;
	size_t i = 0;
	if (aUtf8.size() >= 3 && (unsigned char)aUtf8[0] == 0xEF && (unsigned char)aUtf8[1] == 0xBB && (unsigned char)aUtf8[2] == 0xBF)
		i = 3;
	while (i < aUtf8.size())
	{
		unsigned char c = (unsigned char)aUtf8[i];
		if (c < 0x80) { w += (wchar_t)c; ++i; }
		else if ((c & 0xE0) == 0xC0 && i + 1 < aUtf8.size()) { w += (wchar_t)(((c & 0x1F) << 6) | ((unsigned char)aUtf8[i+1] & 0x3F)); i += 2; }
		else if ((c & 0xF0) == 0xE0 && i + 2 < aUtf8.size()) { w += (wchar_t)(((c & 0x0F) << 12) | (((unsigned char)aUtf8[i+1] & 0x3F) << 6) | ((unsigned char)aUtf8[i+2] & 0x3F)); i += 3; }
		else if ((c & 0xF8) == 0xF0 && i + 3 < aUtf8.size()) { w += (wchar_t)(((c & 0x07) << 18) | (((unsigned char)aUtf8[i+1] & 0x3F) << 12) | (((unsigned char)aUtf8[i+2] & 0x3F) << 6) | ((unsigned char)aUtf8[i+3] & 0x3F)); i += 4; }
		else { w += L'?'; ++i; }
	}
	LPTSTR persistent = (LPTSTR)SimpleHeap::Alloc((w.size() + 1) * sizeof(TCHAR));
	tmemcpy(persistent, w.c_str(), w.size() + 1);
	aResultToken.SetValue(persistent, w.size());
}

static std::string LinuxWideToNarrow(const wchar_t *aIn)
{
	if (!aIn)
		return std::string();
	char buf[8192];
	size_t n = wcstombs(buf, aIn, sizeof(buf) - 1);
	if (n == (size_t)-1)
		return std::string();
	buf[n] = '\0';
	return std::string(buf);
}

BIF_DECL(BIF_Linux_IniRead)
{
	char path_buf[4096];
	std::string path = LinuxNarrowOf(aParam, aParamCount, 0, path_buf, sizeof(path_buf));
	TCHAR section_buf[512], key_buf[512], def_buf[4096];
	section_buf[0] = key_buf[0] = def_buf[0] = L'\0';
	LPTSTR section = aParamCount > 1 ? TokenToString(*aParam[1], section_buf, nullptr) : nullptr;
	LPTSTR key = aParamCount > 2 ? TokenToString(*aParam[2], key_buf, nullptr) : nullptr;
	LPTSTR def = aParamCount > 3 ? TokenToString(*aParam[3], def_buf, nullptr) : nullptr;
	if (!def)
		def = def_buf;
	std::string content;
	if (!LinuxReadFileUtf8(path.c_str(), content))
	{
		LinuxSetResultUtf8(aResultToken, "");
		return;
	}
	std::string want_section = section ? LinuxWideToNarrow(section) : "";
	std::string want_key = key ? LinuxWideToNarrow(key) : "";
	std::string result;
	bool in_section = want_section.empty();
	std::istringstream ss(content);
	std::string line;
	while (std::getline(ss, line))
	{
		if (!line.empty() && (line.back() == '\r'))
			line.pop_back();
		std::string trimmed = line;
		size_t b = trimmed.find_first_not_of(" \t");
		if (b == std::string::npos)
			continue;
		trimmed = trimmed.substr(b);
		if (!trimmed.empty() && trimmed[0] == ';')
			continue;
		if (!trimmed.empty() && trimmed[0] == '[')
		{
			size_t e = trimmed.find(']');
			in_section = e != std::string::npos && trimmed.substr(1, e - 1) == want_section;
			continue;
		}
		if (!in_section)
			continue;
		size_t eq = trimmed.find('=');
		if (eq == std::string::npos)
			continue;
		std::string k = trimmed.substr(0, eq);
		size_t ke = k.find_last_not_of(" \t");
		if (ke != std::string::npos)
			k = k.substr(0, ke + 1);
		if (k != want_key)
			continue;
		std::string v = trimmed.substr(eq + 1);
		size_t vb = v.find_first_not_of(" \t");
		if (vb != std::string::npos)
			v = v.substr(vb);
		result = v;
		break;
	}
	if (result.empty() && def && *def)
		LinuxSetResultUtf8(aResultToken, LinuxWideToNarrow(def));
	else
		LinuxSetResultUtf8(aResultToken, result);
}

BIF_DECL(BIF_Linux_IniWrite)
{
	char path_buf[4096];
	std::string path = LinuxNarrowOf(aParam, aParamCount, 1, path_buf, sizeof(path_buf));
	TCHAR value_buf[4096], section_buf[512], key_buf[512];
	value_buf[0] = section_buf[0] = key_buf[0] = L'\0';
	LPTSTR value = TokenToString(*aParam[0], value_buf, nullptr);
	LPTSTR section = aParamCount > 2 ? TokenToString(*aParam[2], section_buf, nullptr) : nullptr;
	LPTSTR key = aParamCount > 3 ? TokenToString(*aParam[3], key_buf, nullptr) : nullptr;
	std::string want_section = section ? LinuxWideToNarrow(section) : "";
	std::string want_key = key ? LinuxWideToNarrow(key) : "";
	std::string new_value = value ? LinuxWideToNarrow(value) : "";

	std::string content;
	std::vector<std::string> lines;
	if (LinuxReadFileUtf8(path.c_str(), content))
	{
		std::istringstream ss(content);
		std::string line;
		while (std::getline(ss, line))
			lines.push_back(line);
	}

	bool in_section = want_section.empty();
	bool replaced = false;
	std::vector<std::string> out;
	for (auto &line : lines)
	{
		std::string trimmed = line;
		size_t b = trimmed.find_first_not_of(" \t");
		if (b == std::string::npos)
			b = 0;
		trimmed = trimmed.substr(b);
		if (!trimmed.empty() && trimmed[0] == '[')
		{
			size_t e = trimmed.find(']');
			in_section = e != std::string::npos && trimmed.substr(1, e - 1) == want_section;
		}
		else if (in_section)
		{
			size_t eq = trimmed.find('=');
			if (eq != std::string::npos)
			{
				std::string k = trimmed.substr(0, eq);
				size_t ke = k.find_last_not_of(" \t");
				if (ke != std::string::npos)
					k = k.substr(0, ke + 1);
				if (k == want_key)
				{
					std::string indent = line.substr(0, line.find_first_not_of(" \t"));
					out.push_back(indent + want_key + "=" + new_value);
					replaced = true;
					continue;
				}
			}
		}
		out.push_back(line);
	}
	if (!replaced)
	{
		if (!in_section)
		{
			if (!out.empty() && !out.back().empty())
				out.push_back("");
			out.push_back("[" + want_section + "]");
		}
		out.push_back(want_key + "=" + new_value);
	}
	std::ofstream f(path.c_str(), std::ios::binary | std::ios::trunc);
	if (f)
	{
		for (auto &line : out)
			f << line << "\n";
	}
	aResultToken.SetValue((__int64)0);
}

BIF_DECL(BIF_Linux_IniDelete)
{
	char path_buf[4096];
	std::string path = LinuxNarrowOf(aParam, aParamCount, 0, path_buf, sizeof(path_buf));
	TCHAR section_buf[512], key_buf[512];
	section_buf[0] = key_buf[0] = L'\0';
	LPTSTR section = aParamCount > 1 ? TokenToString(*aParam[1], section_buf, nullptr) : nullptr;
	LPTSTR key = aParamCount > 2 ? TokenToString(*aParam[2], key_buf, nullptr) : nullptr;
	std::string want_section = section ? LinuxWideToNarrow(section) : "";
	std::string want_key = key ? LinuxWideToNarrow(key) : "";

	std::string content;
	std::vector<std::string> lines;
	if (!LinuxReadFileUtf8(path.c_str(), content))
	{
		aResultToken.SetValue((__int64)0);
		return;
	}
	{
		std::istringstream ss(content);
		std::string line;
		while (std::getline(ss, line))
			lines.push_back(line);
	}
	bool in_section = false;
	bool skip_until_section_end = false;
	std::vector<std::string> out;
	for (auto &line : lines)
	{
		std::string trimmed = line;
		size_t b = trimmed.find_first_not_of(" \t");
		if (b == std::string::npos)
			b = 0;
		trimmed = trimmed.substr(b);
		if (!trimmed.empty() && trimmed[0] == '[')
		{
			size_t e = trimmed.find(']');
			std::string sec = e != std::string::npos ? trimmed.substr(1, e - 1) : "";
			if (sec == want_section)
			{
				if (want_key.empty())
				{
					skip_until_section_end = true; // Delete the whole section.
					continue;
				}
				in_section = true;
			}
			else
			{
				skip_until_section_end = false;
				in_section = false;
			}
		}
		else if (skip_until_section_end)
			continue;
		else if (in_section)
		{
			size_t eq = trimmed.find('=');
			if (eq != std::string::npos)
			{
				std::string k = trimmed.substr(0, eq);
				size_t ke = k.find_last_not_of(" \t");
				if (ke != std::string::npos)
					k = k.substr(0, ke + 1);
				if (k == want_key)
					continue; // Delete this key line.
			}
		}
		out.push_back(line);
	}
	std::ofstream f(path.c_str(), std::ios::binary | std::ios::trunc);
	if (f)
	{
		for (auto &line : out)
			f << line << "\n";
	}
	aResultToken.SetValue((__int64)0);
}

// ---------------------------------------------------------------------------
// Registry
// ---------------------------------------------------------------------------

// Set the result token to a narrow UTF-8 string (converted to wide, copied to
// SimpleHeap so the caller can keep it).
static void LinuxSetPersistentStrResult(ResultToken &aResultToken, const char *aUtf8)
{
	if (!aUtf8)
		aUtf8 = "";
	std::wstring w;
	size_t i = 0;
	while (aUtf8[i])
	{
		unsigned char c = (unsigned char)aUtf8[i];
		if (c < 0x80) { w += (wchar_t)c; ++i; }
		else if ((c & 0xE0) == 0xC0 && aUtf8[i+1]) { w += (wchar_t)(((c & 0x1F) << 6) | ((unsigned char)aUtf8[i+1] & 0x3F)); i += 2; }
		else if ((c & 0xF0) == 0xE0 && aUtf8[i+1] && aUtf8[i+2]) { w += (wchar_t)(((c & 0x0F) << 12) | (((unsigned char)aUtf8[i+1] & 0x3F) << 6) | ((unsigned char)aUtf8[i+2] & 0x3F)); i += 3; }
		else if ((c & 0xF8) == 0xF0 && aUtf8[i+1] && aUtf8[i+2] && aUtf8[i+3]) { w += (wchar_t)(((c & 0x07) << 18) | (((unsigned char)aUtf8[i+1] & 0x3F) << 12) | (((unsigned char)aUtf8[i+2] & 0x3F) << 6) | ((unsigned char)aUtf8[i+3] & 0x3F)); i += 4; }
		else { w += L'?'; ++i; }
	}
	LPTSTR persistent = (LPTSTR)SimpleHeap::Alloc((w.size() + 1) * sizeof(TCHAR));
	tmemcpy(persistent, w.c_str(), w.size() + 1);
	aResultToken.SetValue(persistent, w.size());
}

// ---------------------------------------------------------------------------
// Round 5: settings / process / file ops / system / network modules.
// Semantics follow docs-v2 (Return Value / Error Handling sections) and the
// upstream implementations in lib/vars.cpp, lib/process.cpp, lib/env.cpp,
// lib/drive.cpp, lib/sound.cpp and script_autoit.cpp.
// ---------------------------------------------------------------------------

static LPTSTR sCoordModes[] = COORD_MODES; // "Client","Window","Screen".
static LPTSTR sSendModes[] = SEND_MODES;   // "Event","Input","Play","InputThenPlay".

static void LinuxSetStrRet(ResultToken &aResultToken, StrRet &aRet)
{
	LinuxCopyStrRet(aResultToken, aRet);
}

// ---- Settings module ----

BIF_DECL(BIF_Linux_CoordMode)
{
	TCHAR cmd_buf[64];
	LPTSTR cmd = TokenToString(*aParam[0], cmd_buf, nullptr);
	CoordModeType shift = Line::ConvertCoordModeCmd(cmd ? cmd : cmd_buf);
	if (shift == COORD_MODE_INVALID)
	{
		FResultToError(aResultToken, aParam, aParamCount, FR_E_ARG(0), 0);
		return;
	}
	CoordModeType mode = COORD_MODE_SCREEN;
	if (aParamCount > 1 && !ParamIndexIsOmitted(1))
	{
		TCHAR mode_buf[64];
		LPTSTR m = TokenToString(*aParam[1], mode_buf, nullptr);
		mode = Line::ConvertCoordMode(m ? m : mode_buf);
		if (mode == COORD_MODE_INVALID)
		{
			FResultToError(aResultToken, aParam, aParamCount, FR_E_ARG(1), 0);
			return;
		}
	}
	// Docs: returns the area that TargetType was previously relative to.
	aResultToken.SetValue(sCoordModes[(g->CoordMode >> shift) & COORD_MODE_MASK]);
	g->CoordMode = (g->CoordMode & ~(COORD_MODE_MASK << shift)) | (mode << shift);
}

BIF_DECL(BIF_Linux_DetectHiddenWindows)
{
	// Docs: returns the previous setting (0/1).
	aResultToken.SetValue(g->DetectHiddenWindows ? 1 : 0);
	g->DetectHiddenWindows = TokenToBOOL(*aParam[0]);
}

BIF_DECL(BIF_Linux_DetectHiddenText)
{
	aResultToken.SetValue(g->DetectHiddenText ? 1 : 0);
	g->DetectHiddenText = TokenToBOOL(*aParam[0]);
}

BIF_DECL(BIF_Linux_SetTitleMatchMode)
{
	TCHAR mode_buf[64];
	LPTSTR m = TokenToString(*aParam[0], mode_buf, nullptr);
	TitleMatchModes mode = Line::ConvertTitleMatchMode(m ? m : mode_buf);
	switch (mode)
	{
	case FIND_FAST:
	case FIND_SLOW:
		// Docs: returns the previous speed ("Fast"/"Slow").
		aResultToken.SetValue(g->TitleFindFast ? _T("Fast") : _T("Slow"));
		g->TitleFindFast = (mode == FIND_FAST);
		return;
	case MATCHMODE_INVALID:
		FResultToError(aResultToken, aParam, aParamCount, FR_E_ARG(0), 0);
		return;
	default:
		// Docs: returns the previous match mode (1/2/3 or "RegEx").
		if (g->TitleMatchMode == FIND_REGEX)
			aResultToken.SetValue(_T("RegEx"));
		else
			aResultToken.SetValue((__int64)g->TitleMatchMode);
		g->TitleMatchMode = mode;
	}
}

BIF_DECL(BIF_Linux_SetKeyDelay)
{
	int slot0 = 0, slot1 = 0;
	optl<int> delay = LinuxOptInt(slot0, aParam, aParamCount, 0);
	optl<int> duration = LinuxOptInt(slot1, aParam, aParamCount, 1);
	if (delay.has_value() && *delay < -1)
	{
		FResultToError(aResultToken, aParam, aParamCount, FR_E_ARG(0), 0);
		return;
	}
	if (duration.has_value() && *duration < -1)
	{
		FResultToError(aResultToken, aParam, aParamCount, FR_E_ARG(1), 0);
		return;
	}
	bool play = false;
	if (aParamCount > 2 && !ParamIndexIsOmitted(2))
	{
		TCHAR mode_buf[64];
		LPTSTR m = TokenToString(*aParam[2], mode_buf, nullptr);
		if (!_tcsicmp(m ? m : mode_buf, _T("Play")))
			play = true;
		else if ((m ? m : mode_buf)[0]) // Anything other than "Play" or "" is invalid.
		{
			FResultToError(aResultToken, aParam, aParamCount, FR_E_ARG(2), 0);
			return;
		}
	}
	if (play)
	{
		if (delay.has_value())
			g->KeyDelayPlay = *delay;
		if (duration.has_value())
			g->PressDurationPlay = *duration;
	}
	else
	{
		if (delay.has_value())
			g->KeyDelay = *delay;
		if (duration.has_value())
			g->PressDuration = *duration;
	}
}

BIF_DECL(BIF_Linux_SetMouseDelay)
{
	int delay = (int)TokenToInt64(*aParam[0]);
	if (delay < -1)
	{
		FResultToError(aResultToken, aParam, aParamCount, FR_E_ARG(0), 0);
		return;
	}
	bool play = false;
	if (aParamCount > 1 && !ParamIndexIsOmitted(1))
	{
		TCHAR mode_buf[64];
		LPTSTR m = TokenToString(*aParam[1], mode_buf, nullptr);
		if (!_tcsicmp(m ? m : mode_buf, _T("Play")))
			play = true;
		else if ((m ? m : mode_buf)[0])
		{
			FResultToError(aResultToken, aParam, aParamCount, FR_E_ARG(1), 0);
			return;
		}
	}
	// Docs: returns the previous delay.
	if (play)
	{
		aResultToken.SetValue((__int64)g->MouseDelayPlay);
		g->MouseDelayPlay = delay;
	}
	else
	{
		aResultToken.SetValue((__int64)g->MouseDelay);
		g->MouseDelay = delay;
	}
}

BIF_DECL(BIF_Linux_SetWinDelay)
{
	int delay = (int)TokenToInt64(*aParam[0]);
	if (delay < -1)
	{
		FResultToError(aResultToken, aParam, aParamCount, FR_E_ARG(0), 0);
		return;
	}
	aResultToken.SetValue((__int64)g->WinDelay);
	g->WinDelay = delay;
}

BIF_DECL(BIF_Linux_SetControlDelay)
{
	int delay = (int)TokenToInt64(*aParam[0]);
	if (delay < -1)
	{
		FResultToError(aResultToken, aParam, aParamCount, FR_E_ARG(0), 0);
		return;
	}
	aResultToken.SetValue((__int64)g->ControlDelay);
	g->ControlDelay = delay;
}

BIF_DECL(BIF_Linux_SetDefaultMouseSpeed)
{
	int speed = (int)TokenToInt64(*aParam[0]);
	if (speed < 0 || speed > MAX_MOUSE_SPEED)
	{
		FResultToError(aResultToken, aParam, aParamCount, FR_E_ARG(0), 0);
		return;
	}
	aResultToken.SetValue((__int64)g->DefaultMouseSpeed);
	g->DefaultMouseSpeed = (UCHAR)speed;
}

BIF_DECL(BIF_Linux_SendMode)
{
	TCHAR mode_buf[64];
	LPTSTR m = TokenToString(*aParam[0], mode_buf, nullptr);
	SendModes new_mode = Line::ConvertSendMode(m ? m : mode_buf, SM_INVALID);
	if (new_mode == SM_INVALID)
	{
		FResultToError(aResultToken, aParam, aParamCount, FR_E_ARG(0), 0);
		return;
	}
	// Docs: returns the previous send mode.
	aResultToken.SetValue(sSendModes[g->SendMode]);
	g->SendMode = new_mode;
}

BIF_DECL(BIF_Linux_SendLevel)
{
	int level = (int)TokenToInt64(*aParam[0]);
	if (!SendLevelIsValid(level))
	{
		FResultToError(aResultToken, aParam, aParamCount, FR_E_ARG(0), 0);
		return;
	}
	aResultToken.SetValue((__int64)g->SendLevel);
	g->SendLevel = (SendLevelType)level;
}

// A_ParityLevel(FuncName) -> parity level 1..4 (P1 compatible .. P4
// unavailable; check_detail0821 §13).  A function not in the classification
// table is P1.  Used by defensive scripts to detect "fake compatibility"
// (e.g. skip a P4 feature before calling it).
BIF_DECL(BIF_Linux_ParityLevel)
{
	TCHAR name_buf[256];
	LPTSTR name = aParamCount > 0 ? TokenToString(*aParam[0], name_buf, nullptr) : nullptr;
	if (!name)
	{
		aResultToken.Error(_T("A_ParityLevel requires a function name."), _T(""), ErrorPrototype::Type);
		return;
	}
	char narrow[256];
	size_t n = wcstombs(narrow, name, sizeof(narrow) - 1);
	if (n == (size_t)-1)
		n = 0;
	narrow[n] = 0;
	int level = 1;
	LinuxParityLookup(narrow, level);
	aResultToken.SetValue((__int64)level);
}

// A_HotkeyBackend -> the effective input-backend name ("x11"/"portal"/
// "gnome-shell"/"evdev"; check_detail0821 §1-B/D / R3).  Mirrors --diag's
// input-backend line.
BIV_DECL_R(BIV_HotkeyBackend)
{
	const char *name = LinuxInputBackendName();
	if (name && *name)
	{
		wchar_t wbuf[64];
		size_t n = mbstowcs(wbuf, name, _countof(wbuf) - 1);
		if (n != (size_t)-1)
		{
			wbuf[n] = 0;
			// BIV results are read after this frame returns (ExpandExpression
			// wcslens the value), so the string must be persistent -- returning
			// a stack buffer is a stack-use-after-return under ASan.
			LPTSTR persistent = (LPTSTR)SimpleHeap::Alloc((n + 1) * sizeof(TCHAR));
			tmemcpy(persistent, wbuf, n + 1);
			_f_return_p(persistent);
		}
	}
	aResultToken.SetValue(_T(""));
}

// HotkeyBackendGet([KeyName]) -> a versioned capability object.  caps_version
// lets scripts reject a schema they do not understand; synthetic_provenance
// is none/heuristic/authoritative rather than an over-promising bool.  With a
// key name it reports the backend that per-hotkey routing would currently
// choose; without one it reports the effective backend lane.
BIF_DECL(BIF_Linux_HotkeyBackendGet)
{
	AhkInputBackendKind kind = LinuxInputBackendKind();
	if (aParamCount > 0)
	{
		TCHAR name_buf[256];
		LPTSTR keyname = TokenToString(*aParam[0], name_buf, nullptr);
		if (keyname && *keyname)
		{
			UCHAR no_suppress = 0;
			bool hook_mandatory = false;
			Hotkey *hk = Hotkey::FindHotkeyByTrueNature(keyname, no_suppress, hook_mandatory);
			if (hk)
			{
				bool passthrough = (no_suppress & NO_SUPPRESS_PREFIX) != 0;
				bool key_up = hk->mKeyUp;
				bool bare = (hk->mModifiers == 0 && hk->mVK != 0);
				// Wildcard has no dedicated field on Hotkey; a name starting
				// with '*' is the wildcard form.
				bool wildcard = keyname[0] == _T('*');
				kind = LinuxInputBackendRoute(passthrough, key_up, bare, wildcard);
			}
		}
	}
	const char *name = LinuxInputBackendNameFor(kind);
	const AhkInputBackendCaps *caps = LinuxInputBackendCapsFor(kind);
	Object *obj = Object::Create();
	if (!obj)
	{
		aResultToken.Error(_T("HotkeyBackendGet: out of memory."));
		return;
	}
	if (name && *name)
	{
		wchar_t wbuf[64];
		size_t n = mbstowcs(wbuf, name, _countof(wbuf) - 1);
		if (n == (size_t)-1)
			n = 0;
		wbuf[n] = 0;
		LPTSTR persistent = (LPTSTR)SimpleHeap::Alloc((wcslen(wbuf) + 1) * sizeof(TCHAR));
		tmemcpy(persistent, wbuf, wcslen(wbuf) + 1);
		obj->SetOwnProp(_T("backend"), persistent);
	}
	else
		obj->SetOwnProp(_T("backend"), _T(""));
	obj->SetOwnProp(_T("caps_version"), (__int64)LinuxInputBackendCapsVersion());
	obj->SetOwnProp(_T("global_hotkeys"), (__int64)(caps && caps->global_hotkeys));
	obj->SetOwnProp(_T("suppress"), (__int64)(caps && caps->suppress));
	obj->SetOwnProp(_T("passthrough"), (__int64)(caps && caps->passthrough));
	obj->SetOwnProp(_T("key_up"), (__int64)(caps && caps->key_up));
	obj->SetOwnProp(_T("wildcard"), (__int64)(caps && caps->wildcard));
	obj->SetOwnProp(_T("bare_keys"), (__int64)(caps && caps->bare_keys));
	obj->SetOwnProp(_T("zero_confirm"), (__int64)(caps && caps->zero_confirm));
	obj->SetOwnProp(_T("dynamic"), (__int64)(caps && caps->dynamic));
	obj->SetOwnProp(_T("multi_owner"), (__int64)(caps && caps->multi_owner));
	obj->SetOwnProp(_T("scan_code"), (__int64)(caps && caps->scan_code));
	obj->SetOwnProp(_T("custom_combo"), (__int64)(caps && caps->custom_combo));
	obj->SetOwnProp(_T("char_stream"), (__int64)(caps && caps->char_stream));
	obj->SetOwnProp(_T("send_level_gate"), (__int64)(caps && caps->send_level_gate));
	obj->SetOwnProp(_T("injection_unicode"), (__int64)(caps && caps->injection_unicode));
	const char *provenance = caps
		? LinuxInputBackendProvenanceName(caps->synthetic_provenance) : "none";
	wchar_t prov_buf[32];
	size_t prov_len = mbstowcs(prov_buf, provenance, _countof(prov_buf) - 1);
	if (prov_len == (size_t)-1) prov_len = 0;
	prov_buf[prov_len] = 0;
	LPTSTR prov_persistent = (LPTSTR)SimpleHeap::Alloc((prov_len + 1) * sizeof(TCHAR));
	tmemcpy(prov_persistent, prov_buf, prov_len + 1);
	obj->SetOwnProp(_T("synthetic_provenance"), prov_persistent);
	aResultToken.SetValue(obj);
}

BIF_DECL(BIF_Linux_SetRegView)
{
	TCHAR view_buf[32];
	LPTSTR v = TokenToString(*aParam[0], view_buf, nullptr);
	DWORD reg_view = Line::RegConvertView(v ? v : view_buf);
	if (reg_view == (DWORD)-1)
	{
		FResultToError(aResultToken, aParam, aParamCount, FR_E_ARG(0), 0);
		return;
	}
	// Docs: returns the previous setting.
	switch (g->RegView)
	{
	case KEY_WOW64_32KEY: aResultToken.SetValue(_T("32")); break;
	case KEY_WOW64_64KEY: aResultToken.SetValue(_T("64")); break;
	default: aResultToken.SetValue(_T("Default")); break;
	}
	if (sizeof(void *) == 8)
		g->RegView = reg_view;
}

BIF_DECL(BIF_Linux_SetStoreCapsLockMode)
{
	aResultToken.SetValue(g->StoreCapslockMode ? 1 : 0);
	g->StoreCapslockMode = TokenToBOOL(*aParam[0]);
}

BIF_DECL(BIF_Linux_FileEncoding)
{
	UINT new_encoding = Line::ConvertFileEncoding(*aParam[0]);
	if (new_encoding == (UINT)-1)
	{
		FResultToError(aResultToken, aParam, aParamCount, FR_E_ARG(0), 0);
		return;
	}
	// Docs: returns the previous setting as a readable string.
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
		g->Encoding = new_encoding;
		return;
	}
	}
	aResultToken.SetValue(enc);
	g->Encoding = new_encoding;
}

// ---- Process module (/proc based; lib/process.cpp semantics) ----

// Resolve the PIDOrName parameter: omitted -> own PID; numeric -> PID;
// otherwise the first (case-insensitive) name match.  Returns 0 if not found.
static pid_t LinuxResolveProcess(ExprTokenType *aParam[], int aParamCount)
{
	if (aParamCount == 0 || ParamIndexIsOmitted(0))
		return getpid();
	char name_buf[512];
	std::string target = LinuxNarrowOf(aParam, aParamCount, 0, name_buf, sizeof(name_buf));
	if (target.empty())
		return getpid();
	if (isdigit((unsigned char)target[0]))
	{
		pid_t pid = (pid_t)atol(target.c_str());
		return LinuxProcessAlive(pid) ? pid : 0;
	}
	// Docs: "The name is not case-sensitive" and "only the first process will
	// be operated upon".
	size_t slash = target.find_last_of('/');
	std::string wanted = slash != std::string::npos ? target.substr(slash + 1) : target;
	std::string wl;
	for (char c : wanted)
		wl += (char)tolower((unsigned char)c);
	DIR *dir = opendir("/proc");
	if (!dir)
		return 0;
	pid_t found = 0;
	while (struct dirent *ent = readdir(dir))
	{
		if (!isdigit((unsigned char)ent->d_name[0]))
			continue;
		std::ifstream f((std::string("/proc/") + ent->d_name + "/comm").c_str());
		if (!f)
			continue;
		std::string name;
		std::getline(f, name);
		std::string nl;
		for (char c : name)
			nl += (char)tolower((unsigned char)c);
		if (nl == wl)
		{
			pid_t candidate = (pid_t)atol(ent->d_name);
			if (LinuxProcessAlive(candidate))
			{
				found = candidate;
				break;
			}
		}
	}
	closedir(dir);
	return found;
}

static bool LinuxProcName(pid_t aPid, std::string &aName)
{
	std::ifstream f((std::string("/proc/") + std::to_string(aPid) + "/comm").c_str());
	if (!f)
		return false;
	std::getline(f, aName);
	return !aName.empty();
}

static bool LinuxProcParent(pid_t aPid, pid_t &aParent)
{
	std::ifstream f((std::string("/proc/") + std::to_string(aPid) + "/stat").c_str());
	if (!f)
		return false;
	std::string stat;
	std::getline(f, stat);
	size_t close_paren = stat.rfind(')');
	if (close_paren == std::string::npos || close_paren + 2 >= stat.size())
		return false;
	std::istringstream ss(stat.substr(close_paren + 2));
	char state = 0;
	pid_t ppid = 0;
	ss >> state >> ppid;
	aParent = ppid;
	return true;
}

static bool LinuxProcExe(pid_t aPid, std::string &aPath)
{
	char buf[4096];
	ssize_t n = readlink((std::string("/proc/") + std::to_string(aPid) + "/exe").c_str(), buf, sizeof(buf) - 1);
	if (n <= 0)
		return false;
	buf[n] = '\0';
	aPath = buf;
	return true;
}

BIF_DECL(BIF_Linux_ProcessGetName)
{
	pid_t pid = LinuxResolveProcess(aParam, aParamCount);
	if (!pid)
	{
		// Docs: TargetError if the process could not be found.
		aResultToken.Error(_T("The specified process could not be found."), _T(""), ErrorPrototype::Target);
		return;
	}
	std::string name;
	if (!LinuxProcName(pid, name))
	{
		aResultToken.Error(_T("The process name could not be retrieved."), _T(""), ErrorPrototype::OS);
		return;
	}
	LinuxSetPersistentStrResult(aResultToken, name.c_str());
}

BIF_DECL(BIF_Linux_ProcessGetPath)
{
	pid_t pid = LinuxResolveProcess(aParam, aParamCount);
	if (!pid)
	{
		aResultToken.Error(_T("The specified process could not be found."), _T(""), ErrorPrototype::Target);
		return;
	}
	std::string path;
	if (!LinuxProcExe(pid, path))
	{
		aResultToken.Error(_T("The process path could not be retrieved."), _T(""), ErrorPrototype::OS);
		return;
	}
	LinuxSetPersistentStrResult(aResultToken, path.c_str());
}

BIF_DECL(BIF_Linux_ProcessGetParent)
{
	pid_t pid = LinuxResolveProcess(aParam, aParamCount);
	if (!pid)
	{
		aResultToken.Error(_T("The specified process could not be found."), _T(""), ErrorPrototype::Target);
		return;
	}
	pid_t parent = 0;
	if (!LinuxProcParent(pid, parent))
	{
		aResultToken.Error(_T("The parent process could not be retrieved."), _T(""), ErrorPrototype::OS);
		return;
	}
	aResultToken.SetValue((__int64)parent);
}

// ---- File ops (lib/file.cpp native impls already linked) ----

FResult FileCopy(StrArg aSource, StrArg aDest, optl<int> aFlag);
FResult FileMove(StrArg aSource, StrArg aDest, optl<int> aFlag);
FResult FileInstall(StrArg aSource, StrArg aDest, optl<int> aFlag);

static void LinuxFileCopyMove(ResultToken &aResultToken, ExprTokenType *aParam[], int aParamCount, bool aMove)
{
	TCHAR s_buf[4096], d_buf[4096];
	LPTSTR s = TokenToString(*aParam[0], s_buf, nullptr);
	LPTSTR d = TokenToString(*aParam[1], d_buf, nullptr);
	int slot = 0;
	FResult fr = aMove
		? FileMove(s ? s : s_buf, d ? d : d_buf, LinuxOptInt(slot, aParam, aParamCount, 2))
		: FileCopy(s ? s : s_buf, d ? d : d_buf, LinuxOptInt(slot, aParam, aParamCount, 2));
	if (FAILED(fr))
		FResultToError(aResultToken, aParam, aParamCount, fr, 0);
}

BIF_DECL(BIF_Linux_FileCopy)
{
	LinuxFileCopyMove(aResultToken, aParam, aParamCount, false);
}

BIF_DECL(BIF_Linux_FileMove)
{
	LinuxFileCopyMove(aResultToken, aParam, aParamCount, true);
}

BIF_DECL(BIF_Linux_FileInstall)
{
	TCHAR s_buf[4096], d_buf[4096];
	LPTSTR s = TokenToString(*aParam[0], s_buf, nullptr);
	LPTSTR d = TokenToString(*aParam[1], d_buf, nullptr);
	// In a packed binary the FileInstall resource is embedded: extract it to
	// the target instead of copying a source file (check_detail0821 §5-M6).
	if (g_LinuxPacked)
	{
		char narrow_s[4096];
		if (s && wcstombs(narrow_s, s, sizeof(narrow_s)) != (size_t)-1)
		{
			narrow_s[sizeof(narrow_s) - 1] = '\0';
			std::vector<unsigned char> data;
			if (LinuxPackGetResource(narrow_s, data))
			{
				if (!d)
					_f_return_retval; // Dest empty: nothing to write.
				char narrow_d[4096];
				if (wcstombs(narrow_d, d, sizeof(narrow_d)) == (size_t)-1)
					narrow_d[0] = '\0';
				narrow_d[sizeof(narrow_d) - 1] = '\0';
				FILE *f = fopen(narrow_d, "wb");
				if (!f)
				{
					aResultToken.Error(_T("Cannot create destination file: "), d, ErrorPrototype::OS);
					return;
				}
				fwrite(data.data(), 1, data.size(), f);
				fclose(f);
				return;
			}
			// Not an embedded resource: fall through to the copy semantics.
		}
	}
	int slot = 0;
	// Docs: in an uncompiled script the source file is copied to the target.
	FResult fr = FileInstall(s ? s : s_buf, d ? d : d_buf, LinuxOptInt(slot, aParam, aParamCount, 2));
	if (FAILED(fr))
		FResultToError(aResultToken, aParam, aParamCount, fr, 0);
}

// ---- FileRecycle / FileRecycleEmpty (XDG Trash specification) ----

static std::string LinuxNarrow(const wchar_t *aWide)
{
	char buf[4096];
	if (wcstombs(buf, aWide, sizeof(buf)) == (size_t)-1)
		buf[0] = '\0';
	buf[sizeof(buf) - 1] = '\0';
	return std::string(buf);
}

static void LinuxTrashDirs(std::string &aFilesDir, std::string &aInfoDir)
{
	const char *xdg = std::getenv("XDG_DATA_HOME");
	std::string base;
	if (xdg && *xdg)
		base = xdg;
	else
		base = std::string(std::getenv("HOME") && *std::getenv("HOME") ? std::getenv("HOME") : "/tmp")
			+ "/.local/share";
	aFilesDir = base + "/Trash/files";
	aInfoDir = base + "/Trash/info";
}

// Unique trash name: append ".<n>" while the target exists.
static std::string LinuxUniqueTrashName(const std::string &aDir, const std::string &aBase)
{
	if (!std::filesystem::exists(aDir + "/" + aBase))
		return aBase;
	for (int i = 1; i < 10000; ++i)
	{
		std::string candidate = aBase + "." + std::to_string(i);
		if (!std::filesystem::exists(aDir + "/" + candidate))
			return candidate;
	}
	return aBase + "." + std::to_string(::getpid());
}

BIF_DECL(BIF_Linux_FileRecycle)
{
	TCHAR pat_buf[4096];
	LPTSTR pat = TokenToString(*aParam[0], pat_buf, nullptr);
	std::wstring pattern(pat ? pat : pat_buf);
	if (pattern.empty())
	{
		FResultToError(aResultToken, aParam, aParamCount, FR_E_ARG(0), 0);
		return;
	}
	std::string files_dir, info_dir;
	LinuxTrashDirs(files_dir, info_dir);
	std::filesystem::create_directories(files_dir);
	std::filesystem::create_directories(info_dir);

	WIN32_FIND_DATA findData;
	HANDLE hSearch = FindFirstFile(pattern.c_str(), &findData);
	if (hSearch == INVALID_HANDLE_VALUE)
	{
		// Docs: an exception is thrown on failure (a no-match pattern is not
		// a failure per the FileCopy/FileMove convention, but FileRecycle has
		// no such exception, so treat a literal missing file as failure).
		if (pattern.find_first_of(L"?*") == std::wstring::npos)
			aResultToken.Error(_T("The file could not be recycled."), _T(""), ErrorPrototype::OS);
		return;
	}
	int failures = 0;
	do
	{
		if (findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
			continue;
		// Rebuild the full path of the matched file.
		std::wstring spec = pattern;
		size_t slash = spec.find_last_of(L'/');
		std::wstring dir_ws = slash == std::wstring::npos ? L"." : (slash == 0 ? L"/" : spec.substr(0, slash));
		std::wstring full_ws = dir_ws + L"/" + findData.cFileName;
		std::string full = LinuxNarrow(full_ws.c_str());
		std::string name = LinuxNarrow(findData.cFileName);
		std::string trash_name = LinuxUniqueTrashName(files_dir, name);
		try
		{
			std::filesystem::rename(full, files_dir + "/" + trash_name);
			// Write the .trashinfo metadata (Path must be absolute).
			char abs_buf[4096];
			if (!realpath(full.c_str(), abs_buf))
				strcpy(abs_buf, full.c_str());
			// DeletionDate: YYYY-MM-DDThh:mm:ss in local time.
			time_t now = time(nullptr);
			struct tm tmv;
			localtime_r(&now, &tmv);
			char date_buf[32];
			strftime(date_buf, sizeof(date_buf), "%Y-%m-%dT%H:%M:%S", &tmv);
			std::ofstream info(info_dir + "/" + trash_name + ".trashinfo", std::ios::binary | std::ios::trunc);
			if (info)
			{
				info << "[Trash Info]\n";
				info << "Path=" << abs_buf << "\n";
				info << "DeletionDate=" << date_buf << "\n";
			}
		}
		catch (...)
		{
			++failures;
		}
	} while (FindNextFile(hSearch, &findData));
	FindClose(hSearch);
	if (failures)
		aResultToken.Error(_T("The file could not be recycled."), _T(""), ErrorPrototype::OS);
}

BIF_DECL(BIF_Linux_FileRecycleEmpty)
{
	(void)aParam; (void)aParamCount; // Drive parameter: no equivalent on Linux.
	std::string files_dir, info_dir;
	LinuxTrashDirs(files_dir, info_dir);
	try
	{
		if (std::filesystem::exists(files_dir))
			std::filesystem::remove_all(files_dir);
		if (std::filesystem::exists(info_dir))
			std::filesystem::remove_all(info_dir);
	}
	catch (...)
	{
		aResultToken.Error(_T("The recycle bin could not be emptied."), _T(""), ErrorPrototype::OS);
	}
}

BIF_DECL(BIF_Linux_FileGetVersion)
{
	// Docs: "Most non-executable files (and even some EXEs) have no version,
	// and thus an error will be thrown."  Linux files have no version
	// resource, so an OSError is thrown (A_LastError = ERROR_FILE_NOT_FOUND
	// for a missing file, ERROR_RESOURCE_TYPE_NOT_FOUND otherwise).
	TCHAR path_buf[4096];
	LPTSTR path = _T("");
	if (aParamCount > 0 && !ParamIndexIsOmitted(0))
		path = TokenToString(*aParam[0], path_buf, nullptr);
	else if (g && g->mLoopFile)
		path = g->mLoopFile->file_path;
	if (!path || !*path)
	{
		FResultToError(aResultToken, aParam, aParamCount, FR_E_ARG(0), 0);
		return;
	}
	std::string narrow = LinuxNarrow(path);
	struct stat st;
	if (stat(narrow.c_str(), &st) != 0)
	{
		g->LastError = 2; // ERROR_FILE_NOT_FOUND.
		SetLastError(2);
		aResultToken.Error(_T("The specified file does not exist."), _T(""), ErrorPrototype::OS);
		return;
	}
	g->LastError = 1813; // ERROR_RESOURCE_TYPE_NOT_FOUND.
	SetLastError(1813);
	aResultToken.Error(_T("The file has no version information."), _T(""), ErrorPrototype::OS);
}

// ---- SysGet (X11-backed; 0 without a display) ----

int LinuxGetSystemMetric(int aIndex)
{
	// Open a fresh connection per call so DISPLAY changes take effect.
	Display *dpy = XOpenDisplay(nullptr);
	Bool has_dpy = dpy != nullptr;
	int w = 0, h = 0;
	if (has_dpy)
	{
		w = DisplayWidth(dpy, DefaultScreen(dpy));
		h = DisplayHeight(dpy, DefaultScreen(dpy));
	}
	switch (aIndex)
	{
	case 0:  return w;                       // SM_CXSCREEN
	case 1:  return h;                       // SM_CYSCREEN
	case 4:  return has_dpy ? 25 : 0;        // SM_CYCAPTION (typical title bar height)
	case 11: return 32;                      // SM_CXICON
	case 12: return 32;                      // SM_CYICON
	case 19: return 1;                       // SM_MOUSEPRESENT
	case 43: return 3;                       // SM_CMOUSEBUTTONS
	case 63: return 1;                       // SM_NETWORK
	case 67: return 0;                       // SM_CLEANBOOT
	case 75: return has_dpy ? 1 : 0;         // SM_MOUSEWHEELPRESENT
	case 78: return w;                       // SM_CXVIRTUALSCREEN
	case 79: return h;                       // SM_CYVIRTUALSCREEN
	case 80: return has_dpy ? 1 : 0;         // SM_CMONITORS
	case 91: return has_dpy ? 1 : 0;         // SM_MOUSEHORIZONTALWHEELPRESENT
	case 0x2002: return 1;                   // SM_CARETBLINKINGENABLED
	default: return 0;                       // Everything else (docs: 0 when unsupported).
	}
}

BIF_DECL(BIF_Linux_SysGet)
{
	int index = (int)TokenToInt64(*aParam[0]);
	aResultToken.SetValue((__int64)LinuxGetSystemMetric(index));
}

// ---- SysGetIPAddresses (getifaddrs; IPv4 only, per docs) ----

BIF_DECL(BIF_Linux_SysGetIPAddresses)
{
	Array *addresses = Array::Create();
	if (!addresses)
	{
		aResultToken.Error(_T("Out of memory."), _T(""), ErrorPrototype::Memory);
		return;
	}
	struct ifaddrs *ifa = nullptr;
	if (getifaddrs(&ifa) == 0)
	{
		for (struct ifaddrs *p = ifa; p; p = p->ifa_next)
		{
			if (!p->ifa_addr || p->ifa_addr->sa_family != AF_INET)
				continue;
			char host[INET_ADDRSTRLEN];
			if (!inet_ntop(AF_INET, &((struct sockaddr_in *)p->ifa_addr)->sin_addr, host, sizeof(host)))
				continue;
			wchar_t wbuf[64];
			if (mbstowcs(wbuf, host, 63) == (size_t)-1)
				continue;
			wbuf[63] = L'\0';
			addresses->Append(wbuf);
		}
		freeifaddrs(ifa);
	}
	aResultToken.SetValue(addresses);
}

// ---- Download (curl/wget; saves the server's error page on HTTP errors,
// matching the documented Windows behaviour) ----

static int LinuxRunCommand(const std::string &aCommand)
{
	int rc = system(aCommand.c_str());
	if (rc == -1)
		return -1;
	if (WIFEXITED(rc))
		return WEXITSTATUS(rc);
	return -1;
}

BIF_DECL(BIF_Linux_Download)
{
	TCHAR url_buf[4096], file_buf[4096];
	LPTSTR url = TokenToString(*aParam[0], url_buf, nullptr);
	LPTSTR file = TokenToString(*aParam[1], file_buf, nullptr);
	const wchar_t *u = url ? url : url_buf;
	// Upstream allows a leading "*<flags> " token to override options; skip it.
	if (*u == L'*')
	{
		++u;
		while (*u && *u != L' ' && *u != L'\t')
			++u;
		while (*u == L' ' || *u == L'\t')
			++u;
	}
	std::string url_n = LinuxNarrow(u);
	std::string file_n = LinuxNarrow(file ? file : file_buf);
	if (url_n.empty() || file_n.empty())
	{
		FResultToError(aResultToken, aParam, aParamCount, FR_E_ARG(url_n.empty() ? 0 : 1), 0);
		return;
	}
	// Quote for the shell.
	auto quote = [](const std::string &s) -> std::string
	{
		std::string out = "'";
		for (char c : s)
		{
			if (c == '\'')
				out += "'\\''";
			else
				out += c;
		}
		out += "'";
		return out;
	};
	std::string cmd;
	const char *curl = "/usr/bin/curl", *wget = "/usr/bin/wget";
	if (access(curl, X_OK) == 0)
		cmd = std::string(curl) + " -sSL " + quote(url_n) + " -o " + quote(file_n);
	else if (access(wget, X_OK) == 0)
		cmd = std::string(wget) + " -q " + quote(url_n) + " -O " + quote(file_n);
	else
	{
		aResultToken.Error(_T("No download tool (curl/wget) is installed."), _T(""), ErrorPrototype::OS);
		return;
	}
	if (LinuxRunCommand(cmd) != 0)
		aResultToken.Error(_T("The download failed."), _T(""), ErrorPrototype::OS);
}

// ---- Shutdown (systemctl/loginctl) ----

BIF_DECL(BIF_Linux_Shutdown)
{
	int flags = (int)TokenToInt64(*aParam[0]);
	const char *cmd;
	if (flags & 2)
		cmd = "systemctl reboot";        // EWX_REBOOT.
	else if (flags & 8)
		cmd = "systemctl poweroff";      // EWX_POWEROFF.
	else if (flags & 1)
		cmd = "systemctl poweroff";      // EWX_SHUTDOWN.
	else
		cmd = "loginctl terminate-user $USER"; // EWX_LOGOFF.
	if (LinuxRunCommand(cmd) != 0)
		aResultToken.Error(_T("The system command failed."), _T(""), ErrorPrototype::OS);
}

// ---- Drive label/lock/eject (external tools; device resolved via /proc) ----

// Resolve a drive spec ("/", "/mnt/x", "/dev/sda1") to device + fstype.
static bool LinuxDriveResolve(const std::string &aDrive, std::string &aDevice, std::string &aFstype)
{
	if (aDrive.size() == 2 && aDrive[1] == ':')
		return false; // Windows drive letters have no Linux equivalent.
	if (aDrive.rfind("/dev/", 0) == 0)
	{
		aDevice = aDrive;
		// Find the filesystem that uses this device.
		for (auto &m : LinuxMounts())
			if (m.device == aDrive)
			{
				aFstype = m.fstype;
				return true;
			}
		aFstype = "";
		return true;
	}
	// Mount point lookup.
	for (auto &m : LinuxMounts())
		if (m.mount_point == aDrive || (aDrive + "/") == m.mount_point)
		{
			aDevice = m.device;
			aFstype = m.fstype;
			return true;
		}
	return false;
}

BIF_DECL(BIF_Linux_DriveSetLabel)
{
	char drive_buf[512];
	std::string drive = LinuxNarrowOf(aParam, aParamCount, 0, drive_buf, sizeof(drive_buf));
	std::string device, fstype;
	if (!LinuxDriveResolve(drive, device, fstype))
	{
		aResultToken.Error(_T("The specified drive does not exist."), _T(""), ErrorPrototype::OS);
		return;
	}
	TCHAR label_buf[512];
	LPTSTR label = aParamCount > 1 ? TokenToString(*aParam[1], label_buf, nullptr) : nullptr;
	std::string label_n = LinuxNarrow(label ? label : L"");
	const char *tool = nullptr;
	if (fstype.rfind("ext", 0) == 0)
		tool = "/usr/sbin/e2label";
	else if (fstype == "vfat" || fstype == "exfat" || fstype == "msdos")
		tool = "/usr/sbin/fatlabel";
	if (!tool || access(tool, X_OK) != 0)
	{
		aResultToken.Error(_T("The filesystem of this drive does not support labels."), _T(""), ErrorPrototype::OS);
		return;
	}
	auto quote = [](const std::string &s) -> std::string
	{
		std::string out = "'";
		for (char c : s)
		{
			if (c == '\'')
				out += "'\\''";
			else
				out += c;
		}
		out += "'";
		return out;
	};
	if (LinuxRunCommand(std::string(tool) + " " + quote(device) + " " + quote(label_n)) != 0)
		aResultToken.Error(_T("Failed to set the volume label."), _T(""), ErrorPrototype::OS);
}

BIF_DECL(BIF_Linux_DriveEject)
{
	char drive_buf[512];
	std::string drive = LinuxNarrowOf(aParam, aParamCount, 0, drive_buf, sizeof(drive_buf));
	if (drive.empty())
		drive = "/";
	std::string device, fstype;
	if (!LinuxDriveResolve(drive, device, fstype))
	{
		aResultToken.Error(_T("The specified drive does not exist."), _T(""), ErrorPrototype::OS);
		return;
	}
	if (access("/usr/bin/eject", X_OK) != 0
		|| LinuxRunCommand(std::string("/usr/bin/eject ") + device) != 0)
		aResultToken.Error(_T("The drive could not be ejected."), _T(""), ErrorPrototype::OS);
}

BIF_DECL(BIF_Linux_DriveRetract)
{
	char drive_buf[512];
	std::string drive = LinuxNarrowOf(aParam, aParamCount, 0, drive_buf, sizeof(drive_buf));
	if (drive.empty())
		drive = "/";
	std::string device, fstype;
	if (!LinuxDriveResolve(drive, device, fstype))
	{
		aResultToken.Error(_T("The specified drive does not exist."), _T(""), ErrorPrototype::OS);
		return;
	}
	if (access("/usr/bin/eject", X_OK) != 0
		|| LinuxRunCommand(std::string("/usr/bin/eject -t ") + device) != 0)
		aResultToken.Error(_T("The drive could not be retracted."), _T(""), ErrorPrototype::OS);
}

BIF_DECL(BIF_Linux_DriveLock)
{
	char drive_buf[512];
	std::string drive = LinuxNarrowOf(aParam, aParamCount, 0, drive_buf, sizeof(drive_buf));
	std::string device, fstype;
	if (!LinuxDriveResolve(drive, device, fstype))
	{
		aResultToken.Error(_T("The specified drive does not exist."), _T(""), ErrorPrototype::OS);
		return;
	}
	if (access("/usr/bin/udisksctl", X_OK) != 0
		|| LinuxRunCommand(std::string("/usr/bin/udisksctl lock -b ") + device) != 0)
		aResultToken.Error(_T("The drive does not support locking."), _T(""), ErrorPrototype::OS);
}

BIF_DECL(BIF_Linux_DriveUnlock)
{
	char drive_buf[512];
	std::string drive = LinuxNarrowOf(aParam, aParamCount, 0, drive_buf, sizeof(drive_buf));
	std::string device, fstype;
	if (!LinuxDriveResolve(drive, device, fstype))
	{
		aResultToken.Error(_T("The specified drive does not exist."), _T(""), ErrorPrototype::OS);
		return;
	}
	if (access("/usr/bin/udisksctl", X_OK) != 0
		|| LinuxRunCommand(std::string("/usr/bin/udisksctl unlock -b ") + device) != 0)
		aResultToken.Error(_T("The drive does not support unlocking."), _T(""), ErrorPrototype::OS);
}

// ---- SoundPlay (aplay/paplay) ----

BIF_DECL(BIF_Linux_SoundPlay)
{
	TCHAR f_buf[4096];
	LPTSTR f = TokenToString(*aParam[0], f_buf, nullptr);
	std::string file = LinuxNarrow(f ? f : f_buf);
	if (file.empty())
	{
		FResultToError(aResultToken, aParam, aParamCount, FR_E_ARG(0), 0);
		return;
	}
	// Wait is ignored on Linux: the call is synchronous either way (the docs
	// only require that Wait=1 waits for completion, which is what we do).
	const char *player = nullptr;
	if (access("/usr/bin/paplay", X_OK) == 0)
		player = "/usr/bin/paplay";
	else if (access("/usr/bin/aplay", X_OK) == 0)
		player = "/usr/bin/aplay";
	if (!player || LinuxRunCommand(std::string(player) + " " + file) != 0)
		aResultToken.Error(_T("The sound could not be played."), _T(""), ErrorPrototype::OS);
}

struct LinuxMdFuncEntry
{
	LPCTSTR name;
	BuiltInFunctionType bif;
	UCHAR min_params, max_params;
	UCHAR output_vars[MAX_FUNC_OUTPUT_VAR]; // 1-based param indices that are output vars.
};

#define LMD_IMPL(name, fn, minp, maxp, ...) { _T(#name), fn, minp, maxp, {__VA_ARGS__} }
#define LMD_NI(name, minp, maxp, ...) { _T(#name), BIF_Linux_NotImplemented, minp, maxp, {__VA_ARGS__} }

// All functions registered via lib/functions.h on Windows.  Entries marked
// LMD_IMPL are wired to a real implementation; the rest (LMD_NI) produce a
// clear "not implemented on Linux" runtime error when called.  Param ranges
// mirror lib/functions.h so that call validation behaves like upstream.
static LinuxMdFuncEntry sLinuxMdFuncs[] =
{
	LMD_IMPL(A_ParityLevel, BIF_Linux_ParityLevel, 1, 1),
	// CallbackCreate/CallbackFree: libffi closure backend (core_callback_linux.cpp).
	LMD_IMPL(CallbackCreate, BIF_Linux_CallbackCreate, 1, 3),
	LMD_IMPL(CallbackFree, BIF_Linux_CallbackFree, 1, 1),
	LMD_IMPL(BlockInput, BIF_Linux_BlockInput, 1, 1),
	LMD_IMPL(ClipWait, BIF_Linux_ClipWait, 0, 2),
	LMD_IMPL(ControlAddItem, BIF_Linux_ControlAddItem, 2, 6),
	LMD_IMPL(ControlChooseIndex, BIF_Linux_ControlChooseIndex, 2, 6),
	LMD_IMPL(ControlChooseString, BIF_Linux_ControlChooseString, 2, 6),
	LMD_IMPL(ControlClick, BIF_Linux_ControlClick, 0, 8),
	LMD_IMPL(ControlDeleteItem, BIF_Linux_ControlDeleteItem, 2, 6),
	LMD_IMPL(ControlFindItem, BIF_Linux_ControlFindItem, 2, 6),
	LMD_IMPL(ControlFocus, BIF_Linux_ControlFocus, 1, 5),
	LMD_IMPL(ControlGetChecked, BIF_Linux_ControlGetChecked, 1, 5),
	LMD_IMPL(ControlGetChoice, BIF_Linux_ControlGetChoice, 1, 5),
	LMD_IMPL(ControlGetClassNN, BIF_Linux_ControlGetClassNN, 1, 5),
	LMD_IMPL(ControlGetEnabled, BIF_Linux_ControlGetEnabled, 1, 5),
	LMD_IMPL(ControlGetExStyle, BIF_Linux_ControlGetExStyle, 1, 5),
	LMD_IMPL(ControlGetFocus, BIF_Linux_ControlGetFocus, 0, 4),
	LMD_IMPL(ControlGetHwnd, BIF_Linux_ControlGetHwnd, 1, 5),
	LMD_IMPL(ControlGetIndex, BIF_Linux_ControlGetIndex, 1, 5),
	LMD_IMPL(ControlGetItems, BIF_Linux_ControlGetItems, 1, 5),
	LMD_IMPL(ControlGetPos, BIF_Linux_ControlGetPos, 0, 9, 1, 2, 3, 4),
	LMD_IMPL(ControlGetStyle, BIF_Linux_ControlGetStyle, 1, 5),
	LMD_IMPL(ControlGetText, BIF_Linux_ControlGetText, 1, 5),
	LMD_IMPL(ControlGetVisible, BIF_Linux_ControlGetVisible, 1, 5),
	LMD_IMPL(ControlHide, BIF_Linux_ControlHide, 1, 5),
	LMD_IMPL(ControlHideDropDown, BIF_Linux_ControlHideDropDown, 1, 5),
	LMD_IMPL(ControlMove, BIF_Linux_ControlMove, 0, 9),
	LMD_IMPL(ControlSend, BIF_Linux_ControlSend, 1, 6),
	LMD_IMPL(ControlSendText, BIF_Linux_ControlSendText, 1, 6),
	LMD_IMPL(ControlSetChecked, BIF_Linux_ControlSetChecked, 2, 6),
	LMD_IMPL(ControlSetEnabled, BIF_Linux_ControlSetEnabled, 2, 6),
	LMD_IMPL(ControlSetExStyle, BIF_Linux_ControlSetExStyle, 2, 6),
	LMD_IMPL(ControlSetStyle, BIF_Linux_ControlSetStyle, 2, 6),
	LMD_IMPL(ControlSetText, BIF_Linux_ControlSetText, 2, 6),
	LMD_IMPL(ControlShow, BIF_Linux_ControlShow, 1, 5),
	LMD_IMPL(ControlShowDropDown, BIF_Linux_ControlShowDropDown, 1, 5),
	LMD_IMPL(CoordMode, BIF_Linux_CoordMode, 1, 2),
	LMD_IMPL(Critical, BIF_Linux_Critical, 0, 1),
	LMD_IMPL(DateAdd, BIF_Linux_DateAdd, 3, 3),
	LMD_IMPL(DateDiff, BIF_Linux_DateDiff, 3, 3),
	LMD_IMPL(DetectHiddenText, BIF_Linux_DetectHiddenText, 1, 1),
	LMD_IMPL(DetectHiddenWindows, BIF_Linux_DetectHiddenWindows, 1, 1),
	LMD_IMPL(DirCopy, BIF_Linux_DirCopy, 2, 3),
	LMD_IMPL(DirMove, BIF_Linux_DirMove, 2, 3),
	LMD_IMPL(DirSelect, BIF_Linux_DirSelect, 0, 3),
	LMD_IMPL(Download, BIF_Linux_Download, 2, 2),
	LMD_IMPL(DriveEject, BIF_Linux_DriveEject, 0, 1),
	LMD_IMPL(DriveGetCapacity, BIF_Linux_DriveGetCapacity, 1, 1),
	LMD_IMPL(DriveGetFilesystem, BIF_Linux_DriveGetFilesystem, 1, 1),
	LMD_IMPL(DriveGetLabel, BIF_Linux_DriveGetLabel, 1, 1),
	LMD_IMPL(DriveGetList, BIF_Linux_DriveGetList, 0, 1),
	LMD_IMPL(DriveGetSerial, BIF_Linux_DriveGetSerial, 1, 1),
	LMD_IMPL(DriveGetSpaceFree, BIF_Linux_DriveGetSpaceFree, 1, 1),
	LMD_IMPL(DriveGetStatus, BIF_Linux_DriveGetStatus, 1, 1),
	LMD_IMPL(DriveGetStatusCD, BIF_Linux_DriveGetStatusCD, 0, 1),
	LMD_IMPL(DriveGetType, BIF_Linux_DriveGetType, 1, 1),
	LMD_IMPL(DriveLock, BIF_Linux_DriveLock, 1, 1),
	LMD_IMPL(DriveRetract, BIF_Linux_DriveRetract, 0, 1),
	LMD_IMPL(DriveSetLabel, BIF_Linux_DriveSetLabel, 1, 2),
	LMD_IMPL(DriveUnlock, BIF_Linux_DriveUnlock, 1, 1),
	LMD_IMPL(Edit, BIF_Linux_Edit, 0, 0),
	LMD_IMPL(EditGetCurrentCol, BIF_Linux_EditGetCurrentCol, 1, 5),
	LMD_IMPL(EditGetCurrentLine, BIF_Linux_EditGetCurrentLine, 1, 5),
	LMD_IMPL(EditGetLine, BIF_Linux_EditGetLine, 2, 6),
	LMD_IMPL(EditGetLineCount, BIF_Linux_EditGetLineCount, 1, 5),
	LMD_IMPL(EditGetSelectedText, BIF_Linux_EditGetSelectedText, 1, 5),
	LMD_IMPL(EditPaste, BIF_Linux_EditPaste, 2, 6),
	LMD_IMPL(EnvGet, BIF_Linux_EnvGet, 1, 1),
	LMD_IMPL(EnvSet, BIF_Linux_EnvSet, 1, 2),
	LMD_IMPL(Exit, BIF_Linux_Exit, 0, 1),
	LMD_IMPL(ExitApp, BIF_Linux_ExitApp, 0, 1),
	LMD_IMPL(FileCopy, BIF_Linux_FileCopy, 2, 3),
	LMD_IMPL(FileCreateShortcut, BIF_Linux_FileCreateShortcut, 2, 9),
	LMD_IMPL(FileEncoding, BIF_Linux_FileEncoding, 1, 1),
	LMD_IMPL(FileGetAttrib, BIF_Linux_FileGetAttrib, 0, 1),
	LMD_IMPL(FileGetShortcut, BIF_Linux_FileGetShortcut, 1, 8, 2, 3, 4, 5, 6, 7, 8),
	LMD_IMPL(FileGetSize, BIF_Linux_FileGetSize, 0, 2),
	LMD_IMPL(FileGetTime, BIF_Linux_FileGetTime, 0, 2),
	LMD_IMPL(FileGetVersion, BIF_Linux_FileGetVersion, 0, 1),
	LMD_IMPL(FileInstall, BIF_Linux_FileInstall, 2, 3),
	LMD_IMPL(FileMove, BIF_Linux_FileMove, 2, 3),
	LMD_IMPL(FileRecycle, BIF_Linux_FileRecycle, 1, 1),
	LMD_IMPL(FileRecycleEmpty, BIF_Linux_FileRecycleEmpty, 0, 1),
	LMD_IMPL(FileSelect, BIF_Linux_FileSelect, 0, 4),
	LMD_IMPL(FileSetAttrib, BIF_Linux_FileSetAttrib, 1, 3),
	LMD_IMPL(FileSetTime, BIF_Linux_FileSetTime, 0, 4),
	LMD_IMPL(GetKeyName, BIF_Linux_GetKeyName, 1, 1),
	LMD_IMPL(GetKeySC, BIF_Linux_GetKeySC, 1, 1),
	LMD_IMPL(GetKeyState, BIF_Linux_GetKeyState, 1, 2),
	LMD_IMPL(GetKeyVK, BIF_Linux_GetKeyVK, 1, 1),
	LMD_IMPL(GroupActivate, BIF_Linux_GroupActivate, 1, 2),
	LMD_IMPL(GroupAdd, BIF_Linux_GroupAdd, 1, 5),
	LMD_IMPL(GroupClose, BIF_Linux_GroupClose, 1, 2),
	LMD_IMPL(GroupDeactivate, BIF_Linux_GroupDeactivate, 1, 2),
	LMD_IMPL(GuiCtrlFromHwnd, BIF_Linux_GuiCtrlFromHwnd, 1, 1),
	LMD_IMPL(GuiFromHwnd, BIF_Linux_GuiFromHwnd, 1, 2),
	LMD_IMPL(HotIf, BIF_Linux_HotIf, 0, 1),
	LMD_IMPL(HotIfWinActive, BIF_Linux_HotIfWinActive, 0, 2),
	LMD_IMPL(HotIfWinExist, BIF_Linux_HotIfWinExist, 0, 2),
	LMD_IMPL(HotIfWinNotActive, BIF_Linux_HotIfWinNotActive, 0, 2),
	LMD_IMPL(HotIfWinNotExist, BIF_Linux_HotIfWinNotExist, 0, 2),
	LMD_IMPL(Hotkey, BIF_Linux_Hotkey, 1, 3),
	LMD_IMPL(HotkeyBackendGet, BIF_Linux_HotkeyBackendGet, 0, 1),
	LMD_IMPL(ImeGetState, BIF_Linux_ImeGetState, 0, 0),
	LMD_IMPL(Hotstring, BIF_Linux_Hotstring, 1, 3),
	LMD_IMPL(IL_Add, BIF_Linux_IL_Add, 2, 4),
	LMD_IMPL(IL_Create, BIF_Linux_IL_Create, 0, 3),
	LMD_IMPL(IL_Destroy, BIF_Linux_IL_Destroy, 1, 1),
	LMD_IMPL(ImageSearch, BIF_Linux_ImageSearch, 7, 7, 1, 2),
	LMD_IMPL(IniDelete, BIF_Linux_IniDelete, 2, 3),
	LMD_IMPL(IniRead, BIF_Linux_IniRead, 1, 4),
	LMD_IMPL(IniWrite, BIF_Linux_IniWrite, 3, 4),
	LMD_IMPL(InputBox, BIF_Linux_InputBox, 0, 4),
	LMD_IMPL(InstallKeybdHook, BIF_Linux_InstallKeybdHook, 0, 2),
	LMD_IMPL(InstallMouseHook, BIF_Linux_InstallMouseHook, 0, 2),
	LMD_IMPL(IsLabel, BIF_Linux_IsLabel, 1, 1),
	LMD_IMPL(KeyHistory, BIF_Linux_KeyHistory, 0, 1),
	LMD_IMPL(KeyWait, BIF_Linux_KeyWait, 1, 2),
	LMD_IMPL(ListHotkeys, BIF_Linux_ListHotkeys, 0, 0),
	LMD_IMPL(ListLines, BIF_Linux_ListLines, 0, 1),
	LMD_IMPL(ListVars, BIF_Linux_ListVars, 0, 0),	LMD_IMPL(ListViewGetContent, BIF_Linux_ListViewGetContent, 1, 6),
	LMD_IMPL(LoadPicture, BIF_Linux_LoadPicture, 1, 3, 3),
	LMD_IMPL(MenuFromHandle, BIF_Linux_MenuFromHandle, 1, 1),
	LMD_IMPL(MenuSelect, BIF_Linux_MenuSelect, 3, 11),
	LMD_IMPL(MonitorGet, BIF_Linux_MonitorGet, 0, 5, 2, 3, 4, 5),
	LMD_IMPL(MonitorGetCount, BIF_Linux_MonitorGetCount, 0, 0),
	LMD_IMPL(MonitorGetName, BIF_Linux_MonitorGetName, 0, 1),
	LMD_IMPL(MonitorGetPrimary, BIF_Linux_MonitorGetPrimary, 0, 0),
	LMD_IMPL(MonitorGetWorkArea, BIF_Linux_MonitorGetWorkArea, 0, 5, 2, 3, 4, 5),
	LMD_IMPL(MouseClick, BIF_Linux_MouseClick, 0, 7),
	LMD_IMPL(MouseClickDrag, BIF_Linux_MouseClickDrag, 5, 7),
	LMD_IMPL(MouseGetPos, BIF_Linux_MouseGetPos, 0, 5, 1, 2, 3, 4),
	LMD_IMPL(MouseMove, BIF_Linux_MouseMove, 2, 4),
	LMD_IMPL(OnClipboardChange, BIF_Linux_OnClipboardChange, 1, 2),
	LMD_IMPL(OnError, BIF_Linux_OnError, 1, 2),
	LMD_IMPL(OnExit, BIF_Linux_OnExit, 1, 2),
	LMD_IMPL(OnMessage, BIF_Linux_OnMessage, 2, 3),
	LMD_IMPL(OutputDebug, BIF_Linux_OutputDebug, 1, 1),
	LMD_IMPL(Pause, BIF_Linux_Pause, 0, 1),
	LMD_IMPL(Persistent, BIF_Linux_Persistent, 0, 1),
	LMD_IMPL(PixelGetColor, BIF_Linux_PixelGetColor, 2, 3),
	LMD_IMPL(PixelSearch, BIF_Linux_PixelSearch, 7, 10, 1, 2),
	LMD_IMPL(PostMessage, BIF_Linux_PostMessage, 1, 8),
	LMD_IMPL(ProcessClose, BIF_Linux_ProcessClose, 1, 1),
	LMD_IMPL(ProcessExist, BIF_Linux_ProcessExist, 0, 1),
	LMD_IMPL(ProcessGetName, BIF_Linux_ProcessGetName, 0, 1),
	LMD_IMPL(ProcessGetParent, BIF_Linux_ProcessGetParent, 0, 1),
	LMD_IMPL(ProcessGetPath, BIF_Linux_ProcessGetPath, 0, 1),
	LMD_IMPL(ProcessSetPriority, BIF_Linux_ProcessSetPriority, 1, 2),
	LMD_IMPL(ProcessWait, BIF_Linux_ProcessWait, 1, 2),
	LMD_IMPL(ProcessWaitClose, BIF_Linux_ProcessWaitClose, 1, 2),
	LMD_IMPL(Reload, BIF_Linux_Reload, 0, 0),
	LMD_IMPL(Run, BIF_Linux_Run, 1, 4, 4),
	LMD_IMPL(RunAs, BIF_Linux_RunAs, 0, 3),
	LMD_IMPL(Send, BIF_Linux_Send, 1, 1),
	LMD_IMPL(SendEvent, BIF_Linux_SendEvent, 1, 1),
	LMD_IMPL(SendInput, BIF_Linux_SendInput, 1, 1),
	LMD_IMPL(SendLevel, BIF_Linux_SendLevel, 1, 1),
	LMD_IMPL(SendMessage, BIF_Linux_SendMessage, 1, 9),
	LMD_IMPL(SendMode, BIF_Linux_SendMode, 1, 1),
	LMD_IMPL(SendPlay, BIF_Linux_SendPlay, 1, 1),
	LMD_IMPL(SendText, BIF_Linux_SendText, 1, 1),
	LMD_IMPL(SetCapsLockState, BIF_Linux_SetCapsLockState, 0, 1),
	LMD_IMPL(SetControlDelay, BIF_Linux_SetControlDelay, 1, 1),
	LMD_IMPL(SetDefaultMouseSpeed, BIF_Linux_SetDefaultMouseSpeed, 1, 1),
	LMD_IMPL(SetKeyDelay, BIF_Linux_SetKeyDelay, 0, 3),
	LMD_IMPL(SetMouseDelay, BIF_Linux_SetMouseDelay, 1, 2),
	LMD_IMPL(SetNumLockState, BIF_Linux_SetNumLockState, 0, 1),
	LMD_IMPL(SetRegView, BIF_Linux_SetRegView, 1, 1),
	LMD_IMPL(SetScrollLockState, BIF_Linux_SetScrollLockState, 0, 1),
	LMD_IMPL(SetStoreCapsLockMode, BIF_Linux_SetStoreCapsLockMode, 1, 1),
	LMD_IMPL(SetTimer, BIF_Linux_SetTimer, 0, 3),
	LMD_IMPL(SetTitleMatchMode, BIF_Linux_SetTitleMatchMode, 1, 1),
	LMD_IMPL(SetWinDelay, BIF_Linux_SetWinDelay, 1, 1),
	LMD_IMPL(SetWorkingDir, BIF_Linux_SetWorkingDir, 1, 1),
	LMD_IMPL(Shutdown, BIF_Linux_Shutdown, 1, 1),
	LMD_IMPL(Sleep, BIF_Linux_Sleep, 1, 1),
	LMD_IMPL(SoundBeep, BIF_Linux_SoundBeep, 0, 2),
	LMD_IMPL(SoundPlay, BIF_Linux_SoundPlay, 1, 2),
	LMD_IMPL(StatusBarGetText, BIF_Linux_StatusBarGetText, 0, 5),
	LMD_IMPL(StatusBarWait, BIF_Linux_StatusBarWait, 0, 8),
	LMD_IMPL(StrSplit, BIF_Linux_StrSplit, 1, 4),
	LMD_IMPL(Suspend, BIF_Linux_Suspend, 0, 1),
	LMD_IMPL(SysGet, BIF_Linux_SysGet, 1, 1),
	LMD_IMPL(SysGetIPAddresses, BIF_Linux_SysGetIPAddresses, 0, 0),
	LMD_IMPL(Thread, BIF_Linux_Thread, 1, 3),
	LMD_IMPL(ToolTip, BIF_Linux_ToolTip, 0, 4),
	LMD_IMPL(TrayTip, BIF_Linux_TrayTip, 0, 3),
	LMD_IMPL(TraySetIcon, BIF_Linux_TraySetIcon, 0, 3),
	LMD_IMPL(WinActivate, BIF_Linux_WinActivate, 0, 4),
	LMD_IMPL(WinActivateBottom, BIF_Linux_WinActivateBottom, 0, 4),
	LMD_IMPL(WinClose, BIF_Linux_WinClose, 0, 5),
	LMD_IMPL(WinGetClass, BIF_Linux_WinGetClass, 0, 4),
	LMD_IMPL(WinGetClientPos, BIF_Linux_WinGetClientPos, 0, 8, 1, 2, 3, 4),
	LMD_IMPL(WinGetControls, BIF_Linux_WinGetControls, 0, 4),
	LMD_IMPL(WinGetControlsHwnd, BIF_Linux_WinGetControlsHwnd, 0, 4),
	LMD_IMPL(WinGetCount, BIF_Linux_WinGetCount, 0, 4),
	LMD_IMPL(WinGetExStyle, BIF_Linux_WinGetExStyle, 0, 4),
	LMD_IMPL(WinGetID, BIF_Linux_WinGetID, 0, 4),
	LMD_IMPL(WinGetIDLast, BIF_Linux_WinGetIDLast, 0, 4),
	LMD_IMPL(WinGetList, BIF_Linux_WinGetList, 0, 4),
	LMD_IMPL(WinGetMinMax, BIF_Linux_WinGetMinMax, 0, 4),
	LMD_IMPL(WinGetPID, BIF_Linux_WinGetPID, 0, 4),
	LMD_IMPL(WinGetPos, BIF_Linux_WinGetPos, 0, 8, 1, 2, 3, 4),
	LMD_IMPL(WinGetProcessName, BIF_Linux_WinGetProcessName, 0, 4),
	LMD_IMPL(WinGetProcessPath, BIF_Linux_WinGetProcessPath, 0, 4),
	LMD_IMPL(WinGetStyle, BIF_Linux_WinGetStyle, 0, 4),
	LMD_IMPL(WinGetText, BIF_Linux_WinGetText, 0, 4),
	LMD_IMPL(WinGetTitle, BIF_Linux_WinGetTitle, 0, 4),
	LMD_IMPL(WinGetTransColor, BIF_Linux_WinGetTransColor, 0, 4),
	LMD_IMPL(WinGetTransparent, BIF_Linux_WinGetTransparent, 0, 4),
	LMD_IMPL(WinHide, BIF_Linux_WinHide, 0, 4),
	LMD_IMPL(WinKill, BIF_Linux_WinKill, 0, 5),
	LMD_IMPL(WinMaximize, BIF_Linux_WinMaximize, 0, 4),
	LMD_IMPL(WinMinimize, BIF_Linux_WinMinimize, 0, 4),
	LMD_IMPL(WinMinimizeAll, BIF_Linux_WinMinimizeAll, 0, 0),
	LMD_IMPL(WinMinimizeAllUndo, BIF_Linux_WinMinimizeAllUndo, 0, 0),
	LMD_IMPL(WinMove, BIF_Linux_WinMove, 0, 8),
	LMD_IMPL(WinMoveBottom, BIF_Linux_WinMoveBottom, 0, 4),
	LMD_IMPL(WinMoveTop, BIF_Linux_WinMoveTop, 0, 4),
	LMD_IMPL(WinRedraw, BIF_Linux_WinRedraw, 0, 4),
	LMD_IMPL(WinRestore, BIF_Linux_WinRestore, 0, 4),
	LMD_IMPL(WinSetAlwaysOnTop, BIF_Linux_WinSetAlwaysOnTop, 0, 5),
	LMD_IMPL(WinSetEnabled, BIF_Linux_WinSetEnabled, 1, 5),
	LMD_IMPL(WinSetExStyle, BIF_Linux_WinSetExStyle, 1, 5),
	LMD_IMPL(WinSetRegion, BIF_Linux_WinSetRegion, 0, 5),
	LMD_IMPL(WinSetStyle, BIF_Linux_WinSetStyle, 1, 5),
	LMD_IMPL(WinSetTitle, BIF_Linux_WinSetTitle, 1, 5),
	LMD_IMPL(WinSetTransColor, BIF_Linux_WinSetTransColor, 1, 5),
	LMD_IMPL(WinSetTransparent, BIF_Linux_WinSetTransparent, 1, 5),
	LMD_IMPL(WinShow, BIF_Linux_WinShow, 0, 4),
	LMD_IMPL(WinWait, BIF_Linux_WinWait, 0, 5),
	LMD_IMPL(WinWaitActive, BIF_Linux_WinWaitActive, 0, 5),
	LMD_IMPL(WinWaitClose, BIF_Linux_WinWaitClose, 0, 5),
	LMD_IMPL(WinWaitNotActive, BIF_Linux_WinWaitNotActive, 0, 5),
};

#undef LMD_IMPL
#undef LMD_NI

// ---------------------------------------------------------------------------
// Strict parity (check_detail0821 §13): AHK_STRICT_PARITY=warn|error
// ---------------------------------------------------------------------------
// Migrating a Windows script can silently run on "fake-compatible" functions
// (P3 simulated / P4 unavailable).  With AHK_STRICT_PARITY=warn the first call
// of any P3/P4 function prints a one-line warning (level + note); with =error
// the first call raises instead.  Implemented by subclassing BuiltInFunc so
// the interpreter's normal call path is untouched.

// Returns 0 (off) / 1 (warn) / 2 (error), read once.
static int LinuxStrictParityMode()
{
	static int s_mode = -1;
	if (s_mode < 0)
	{
		s_mode = 0;
		const char *v = getenv("AHK_STRICT_PARITY");
		if (v && *v)
		{
			if (!strcasecmp(v, "warn") || !strcasecmp(v, "1")
				|| !strcasecmp(v, "true") || !strcasecmp(v, "yes") || !strcasecmp(v, "on"))
				s_mode = 1;
			else if (!strcasecmp(v, "error") || !strcasecmp(v, "2") || !strcasecmp(v, "strict"))
				s_mode = 2;
			else
				std::fprintf(stderr,
					"AHK warning: AHK_STRICT_PARITY=\"%s\" is not warn|error; "
					"strict parity disabled\n", v);
		}
	}
	return s_mode;
}

class LinuxBuiltInFunc : public BuiltInFunc
{
public:
	bool mStrictChecked = false; // First call already saw the strict check.
	LinuxBuiltInFunc(FuncEntry &aFe) : BuiltInFunc(aFe) {}
	bool Call(ResultToken &aResultToken, ExprTokenType *aParam[], int aParamCount) override;
};

bool LinuxBuiltInFunc::Call(ResultToken &aResultToken, ExprTokenType *aParam[], int aParamCount)
{
	if (!mStrictChecked)
	{
		mStrictChecked = true;
		// Consult the parity table first; the env cache is only primed when a
		// P3/P4 function is actually called (calling LinuxStrictParityMode()
		// on an unrelated P1 first call -- e.g. EnvSet setting the variable
		// itself -- would cache the pre-set value).
		char narrow[256];
		size_t n = wcstombs(narrow, mName, sizeof(narrow) - 1);
		if (n == (size_t)-1)
			n = 0;
		narrow[n] = 0;
		int level = 1;
		const char *note = LinuxParityLookup(narrow, level);
		if (level >= 3)
		{
			int mode = LinuxStrictParityMode();
			if (mode)
			{
				const char *label = level == 3 ? "simulated" : "unavailable";
				if (mode == 1)
					std::fprintf(stderr, "AHK strict-parity: %s is P%d %s%s%s\n"
						, narrow, level, label, note ? " -- " : "", note ? note : "");
				else
				{
					wchar_t buf[768];
					sntprintf(buf, _countof(buf)
						, _T("AHK_STRICT_PARITY=error: %hs is P%d %hs (%hs). See 'ahk --parity %hs'.")
						, narrow, level, label, note ? note : "", narrow);
					aResultToken.Error(buf, _T(""), ErrorPrototype::OS);
					return false;
				}
			}
		}
	}
	return BuiltInFunc::Call(aResultToken, aParam, aParamCount);
}

static BuiltInFunc *LinuxMakeBuiltInFunc(const LinuxMdFuncEntry &aEntry)
{
	// BuiltInFunc stores mOutputVars BY POINTER, so the FuncEntry must live in
	// stable storage (upstream g_BIF entries are static globals).  A std::list
	// never invalidates references to its elements.
	static std::list<FuncEntry> sEntries;
	sEntries.emplace_back();
	FuncEntry &fe = sEntries.back();
	fe.mName = aEntry.name;
	fe.mBIF = aEntry.bif;
	fe.mMinParams = aEntry.min_params;
	fe.mMaxParams = aEntry.max_params;
	fe.mID = 0;
	for (int i = 0; i < MAX_FUNC_OUTPUT_VAR; ++i)
		fe.mOutputVars[i] = aEntry.output_vars[i];
	return new LinuxBuiltInFunc(fe);
}

// Public factory used by Script::GetBuiltInFunc (g_BIF) and by
// Script::GetBuiltInMdFunc below, so strict parity covers both tables.
Func *LinuxNewBuiltInFunc(FuncEntry &aFe)
{
	return new LinuxBuiltInFunc(aFe);
}

Func *Script::GetBuiltInMdFunc(LPTSTR aFuncName)
{
	// Linear search: the table is small and this is only consulted at load
	// time for names not found in g_BIF.
	for (int i = 0; i < _countof(sLinuxMdFuncs); ++i)
		if (!_tcsicmp(aFuncName, sLinuxMdFuncs[i].name))
			return LinuxMakeBuiltInFunc(sLinuxMdFuncs[i]);
	return nullptr;
}
