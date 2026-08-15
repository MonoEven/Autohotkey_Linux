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
#include <sys/resource.h>
#include <unistd.h>

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
FResult DateAdd(StrArg aDateTime, double aTime, StrArg aTimeUnits, StrRet &aRetVal);
FResult DateDiff(StrArg aTime1, StrArg aTime2, StrArg aTimeUnits, __int64 &aRetVal);
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
	FResult fr = Reload();
	if (FAILED(fr))
		FResultToError(aResultToken, aParam, aParamCount, fr, 0);
}

BIF_DECL(BIF_Linux_TrayTip)
{
	TCHAR t1[512], t2[512], t3[128];
	optl<StrArg> text = (aParamCount > 0 && !ParamIndexIsOmitted(0))
		? LinuxOptStr(aParam, aParamCount, 0, t1, sizeof(t1)) : optl<StrArg>(nullptr);
	optl<StrArg> title = (aParamCount > 1 && !ParamIndexIsOmitted(1))
		? LinuxOptStr(aParam, aParamCount, 1, t2, sizeof(t2)) : optl<StrArg>(nullptr);
	optl<StrArg> opts = (aParamCount > 2 && !ParamIndexIsOmitted(2))
		? LinuxOptStr(aParam, aParamCount, 2, t3, sizeof(t3)) : optl<StrArg>(nullptr);
	FResult fr = TrayTip(text, title, opts);
	if (FAILED(fr))
		FResultToError(aResultToken, aParam, aParamCount, fr, 0);
}

BIF_DECL(BIF_Linux_TraySetIcon)
{
	TCHAR fbuf[1024];
	optl<StrArg> file = (aParamCount > 0 && !ParamIndexIsOmitted(0))
		? LinuxOptStr(aParam, aParamCount, 0, fbuf, sizeof(fbuf)) : optl<StrArg>(nullptr);
	int slot1 = 0, slot2 = 0;
	optl<int> number = LinuxOptInt(slot1, aParam, aParamCount, 1);
	optl<int> freeze = LinuxOptInt(slot2, aParam, aParamCount, 2);
	FResult fr = TraySetIcon(file, number, freeze);
	if (FAILED(fr))
		FResultToError(aResultToken, aParam, aParamCount, fr, 0);
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
	pid_t pid = LinuxFindProcess(LinuxNarrowOf(aParam, aParamCount, 1, name_buf, sizeof(name_buf)).c_str());
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
// A_Clipboard: process-internal clipboard (X11 selection integration later)
// ---------------------------------------------------------------------------

static std::wstring &LinuxClipboardStorage()
{
	static std::wstring sClipboard;
	return sClipboard;
}

void BIV_Clipboard(ResultToken &aResultToken, LPTSTR aVarName)
{
	(void)aVarName;
	const std::wstring &c = LinuxClipboardStorage();
	LPTSTR persistent = (LPTSTR)SimpleHeap::Alloc((c.size() + 1) * sizeof(TCHAR));
	tmemcpy(persistent, c.c_str(), c.size() + 1);
	aResultToken.SetValue(persistent, c.size());
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
	LinuxClipboardStorage().assign(s ? s : L"");
}

BIF_DECL(BIF_Linux_ClipWait)
{
	double timeout = aParamCount > 0 ? TokenToDouble(*aParam[0]) : 0;
	double waited = 0;
	bool non_empty = !LinuxClipboardStorage().empty();
	while (!non_empty && (timeout <= 0 || waited < timeout))
	{
		ScriptSleep(50);
		waited += 0.05;
		non_empty = !LinuxClipboardStorage().empty();
	}
	aResultToken.SetValue(non_empty ? 1 : 0);
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
	LMD_NI(BlockInput, 1, 1),
	LMD_IMPL(ClipWait, BIF_Linux_ClipWait, 0, 2),
	LMD_NI(ControlAddItem, 5, 5),
	LMD_NI(ControlChooseIndex, 5, 5),
	LMD_NI(ControlChooseString, 5, 5),
	LMD_NI(ControlClick, 0, 8),
	LMD_NI(ControlDeleteItem, 5, 5),
	LMD_NI(ControlFindItem, 5, 5),
	LMD_NI(ControlFocus, 4, 4),
	LMD_NI(ControlGetChecked, 4, 4),
	LMD_NI(ControlGetChoice, 4, 4),
	LMD_NI(ControlGetClassNN, 4, 4),
	LMD_NI(ControlGetEnabled, 4, 4),
	LMD_NI(ControlGetExStyle, 4, 4),
	LMD_NI(ControlGetFocus, 0, 4),
	LMD_NI(ControlGetHwnd, 0, 4),
	LMD_NI(ControlGetIndex, 5, 5),
	LMD_NI(ControlGetItems, 4, 4),
	LMD_NI(ControlGetPos, 0, 8),
	LMD_NI(ControlGetStyle, 4, 4),
	LMD_NI(ControlGetText, 4, 4),
	LMD_NI(ControlGetVisible, 4, 4),
	LMD_NI(ControlHide, 4, 4),
	LMD_NI(ControlHideDropDown, 4, 4),
	LMD_NI(ControlMove, 0, 8),
	LMD_NI(ControlSend, 1, 5),
	LMD_NI(ControlSendText, 1, 5),
	LMD_NI(ControlSetChecked, 5, 5),
	LMD_NI(ControlSetEnabled, 5, 5),
	LMD_NI(ControlSetExStyle, 5, 5),
	LMD_NI(ControlSetStyle, 5, 5),
	LMD_NI(ControlSetText, 5, 5),
	LMD_NI(ControlShow, 4, 4),
	LMD_NI(ControlShowDropDown, 4, 4),
	LMD_NI(CoordMode, 1, 2),
	LMD_IMPL(Critical, BIF_Linux_Critical, 0, 1),
	LMD_IMPL(DateAdd, BIF_Linux_DateAdd, 3, 3),
	LMD_IMPL(DateDiff, BIF_Linux_DateDiff, 3, 3),
	LMD_NI(DetectHiddenText, 0, 1),
	LMD_NI(DetectHiddenWindows, 0, 1),
	LMD_IMPL(DirCopy, BIF_Linux_DirCopy, 2, 3),
	LMD_IMPL(DirMove, BIF_Linux_DirMove, 2, 3),
	LMD_NI(DirSelect, 0, 3),
	LMD_NI(Download, 2, 2),
	LMD_NI(DriveEject, 0, 1),
	LMD_IMPL(DriveGetCapacity, BIF_Linux_DriveGetCapacity, 1, 1),
	LMD_IMPL(DriveGetFilesystem, BIF_Linux_DriveGetFilesystem, 1, 1),
	LMD_IMPL(DriveGetLabel, BIF_Linux_DriveGetLabel, 1, 1),
	LMD_IMPL(DriveGetList, BIF_Linux_DriveGetList, 0, 1),
	LMD_IMPL(DriveGetSerial, BIF_Linux_DriveGetSerial, 1, 1),
	LMD_IMPL(DriveGetSpaceFree, BIF_Linux_DriveGetSpaceFree, 1, 1),
	LMD_IMPL(DriveGetStatus, BIF_Linux_DriveGetStatus, 1, 1),
	LMD_IMPL(DriveGetStatusCD, BIF_Linux_DriveGetStatusCD, 0, 1),
	LMD_IMPL(DriveGetType, BIF_Linux_DriveGetType, 1, 1),
	LMD_NI(DriveLock, 1, 1),
	LMD_NI(DriveRetract, 0, 1),
	LMD_NI(DriveSetLabel, 1, 2),
	LMD_NI(DriveUnlock, 1, 1),
	LMD_NI(Edit, 0, 0),
	LMD_NI(EditGetCurrentCol, 4, 4),
	LMD_NI(EditGetCurrentLine, 4, 4),
	LMD_NI(EditGetLine, 5, 5),
	LMD_NI(EditGetLineCount, 4, 4),
	LMD_NI(EditGetSelectedText, 4, 4),
	LMD_NI(EditPaste, 5, 5),
	LMD_IMPL(EnvGet, BIF_Linux_EnvGet, 1, 1),
	LMD_IMPL(EnvSet, BIF_Linux_EnvSet, 1, 2),
	LMD_IMPL(Exit, BIF_Linux_Exit, 0, 1),
	LMD_IMPL(ExitApp, BIF_Linux_ExitApp, 0, 1),
	LMD_NI(FileCopy, 2, 3),
	LMD_NI(FileCreateShortcut, 2, 8),
	LMD_NI(FileEncoding, 0, 1),
	LMD_NI(FileGetAttrib, 0, 1),
	LMD_NI(FileGetShortcut, 1, 8),
	LMD_NI(FileGetSize, 0, 2),
	LMD_NI(FileGetTime, 0, 2),
	LMD_NI(FileGetVersion, 0, 1),
	LMD_NI(FileInstall, 2, 3),
	LMD_NI(FileMove, 2, 3),
	LMD_NI(FileRecycle, 1, 1),
	LMD_NI(FileRecycleEmpty, 0, 1),
	LMD_NI(FileSelect, 0, 4),
	LMD_NI(FileSetAttrib, 1, 3),
	LMD_NI(FileSetTime, 0, 4),
	LMD_IMPL(GetKeyName, BIF_Linux_GetKeyName, 1, 1),
	LMD_IMPL(GetKeySC, BIF_Linux_GetKeySC, 1, 1),
	LMD_IMPL(GetKeyState, BIF_Linux_GetKeyState, 1, 2),
	LMD_IMPL(GetKeyVK, BIF_Linux_GetKeyVK, 1, 1),
	LMD_NI(GroupActivate, 1, 2),
	LMD_NI(GroupAdd, 1, 5),
	LMD_NI(GroupClose, 1, 2),
	LMD_NI(GroupDeactivate, 1, 2),
	LMD_NI(GuiCtrlFromHwnd, 1, 1),
	LMD_NI(GuiFromHwnd, 1, 2),
	LMD_IMPL(HotIf, BIF_Linux_HotIf, 0, 1),
	LMD_IMPL(HotIfWinActive, BIF_Linux_HotIfWinActive, 0, 2),
	LMD_IMPL(HotIfWinExist, BIF_Linux_HotIfWinExist, 0, 2),
	LMD_IMPL(HotIfWinNotActive, BIF_Linux_HotIfWinNotActive, 0, 2),
	LMD_IMPL(HotIfWinNotExist, BIF_Linux_HotIfWinNotExist, 0, 2),
	LMD_NI(Hotkey, 1, 3),
	LMD_NI(Hotstring, 1, 3),
	LMD_NI(IL_Add, 2, 4),
	LMD_NI(IL_Create, 0, 3),
	LMD_NI(IL_Destroy, 1, 1),
	LMD_NI(ImageSearch, 7, 7),
	LMD_IMPL(IniDelete, BIF_Linux_IniDelete, 2, 3),
	LMD_IMPL(IniRead, BIF_Linux_IniRead, 1, 4),
	LMD_IMPL(IniWrite, BIF_Linux_IniWrite, 3, 4),
	LMD_IMPL(InputBox, BIF_Linux_InputBox, 0, 4),
	LMD_NI(InstallKeybdHook, 0, 2),
	LMD_NI(InstallMouseHook, 0, 2),
	LMD_IMPL(IsLabel, BIF_Linux_IsLabel, 1, 1),
	LMD_NI(KeyHistory, 0, 1),
	LMD_NI(KeyWait, 1, 2),
	LMD_NI(ListHotkeys, 0, 0),
	LMD_IMPL(ListLines, BIF_Linux_ListLines, 0, 1),
	LMD_NI(ListVars, 0, 0),
	LMD_NI(ListViewGetContent, 0, 5),
	LMD_NI(LoadPicture, 1, 3),
	LMD_NI(MenuFromHandle, 1, 1),
	LMD_NI(MenuSelect, 1, 10),
	LMD_NI(MonitorGet, 0, 5),
	LMD_NI(MonitorGetCount, 0, 0),
	LMD_NI(MonitorGetName, 0, 1),
	LMD_NI(MonitorGetPrimary, 0, 0),
	LMD_NI(MonitorGetWorkArea, 0, 5),
	LMD_NI(MouseClick, 0, 6),
	LMD_NI(MouseClickDrag, 2, 7),
	LMD_NI(MouseGetPos, 0, 4),
	LMD_NI(MouseMove, 2, 4),
	LMD_IMPL(OnClipboardChange, BIF_Linux_OnClipboardChange, 1, 2),
	LMD_IMPL(OnError, BIF_Linux_OnError, 1, 2),
	LMD_IMPL(OnExit, BIF_Linux_OnExit, 1, 2),
	LMD_NI(OnMessage, 2, 3),
	LMD_IMPL(OutputDebug, BIF_Linux_OutputDebug, 1, 1),
	LMD_IMPL(Pause, BIF_Linux_Pause, 0, 1),
	LMD_IMPL(Persistent, BIF_Linux_Persistent, 0, 1),
	LMD_NI(PixelGetColor, 2, 3),
	LMD_NI(PixelSearch, 7, 8),
	LMD_NI(PostMessage, 2, 8),
	LMD_IMPL(ProcessClose, BIF_Linux_ProcessClose, 1, 1),
	LMD_IMPL(ProcessExist, BIF_Linux_ProcessExist, 0, 1),
	LMD_NI(ProcessGetName, 0, 1),
	LMD_NI(ProcessGetParent, 0, 1),
	LMD_NI(ProcessGetPath, 0, 1),
	LMD_IMPL(ProcessSetPriority, BIF_Linux_ProcessSetPriority, 1, 2),
	LMD_IMPL(ProcessWait, BIF_Linux_ProcessWait, 1, 2),
	LMD_IMPL(ProcessWaitClose, BIF_Linux_ProcessWaitClose, 1, 2),
	LMD_IMPL(Reload, BIF_Linux_Reload, 0, 0),
	LMD_IMPL(Run, BIF_Linux_Run, 1, 4, 4),
	LMD_NI(RunAs, 0, 3),
	LMD_NI(Send, 1, 1),
	LMD_NI(SendEvent, 1, 1),
	LMD_NI(SendInput, 1, 1),
	LMD_NI(SendLevel, 1, 1),
	LMD_NI(SendMessage, 3, 9),
	LMD_NI(SendMode, 1, 1),
	LMD_NI(SendPlay, 1, 1),
	LMD_NI(SendText, 1, 1),
	LMD_NI(SetCapsLockState, 0, 1),
	LMD_NI(SetControlDelay, 1, 1),
	LMD_NI(SetDefaultMouseSpeed, 1, 1),
	LMD_NI(SetKeyDelay, 0, 3),
	LMD_NI(SetMouseDelay, 1, 2),
	LMD_NI(SetNumLockState, 0, 1),
	LMD_NI(SetRegView, 1, 1),
	LMD_NI(SetScrollLockState, 0, 1),
	LMD_NI(SetStoreCapsLockMode, 0, 1),
	LMD_NI(SetTimer, 0, 3),
	LMD_NI(SetTitleMatchMode, 1, 1),
	LMD_NI(SetWinDelay, 1, 1),
	LMD_IMPL(SetWorkingDir, BIF_Linux_SetWorkingDir, 1, 1),
	LMD_NI(Shutdown, 1, 1),
	LMD_IMPL(Sleep, BIF_Linux_Sleep, 1, 1),
	LMD_IMPL(SoundBeep, BIF_Linux_SoundBeep, 0, 2),
	LMD_NI(SoundPlay, 1, 2),
	LMD_NI(StatusBarGetText, 0, 5),
	LMD_NI(StatusBarWait, 0, 8),
	LMD_IMPL(StrSplit, BIF_Linux_StrSplit, 1, 4),
	LMD_IMPL(Suspend, BIF_Linux_Suspend, 0, 1),
	LMD_NI(SysGet, 1, 1),
	LMD_NI(SysGetIPAddresses, 0, 0),
	LMD_IMPL(Thread, BIF_Linux_Thread, 1, 3),
	LMD_NI(ToolTip, 0, 4),
	LMD_IMPL(TraySetIcon, BIF_Linux_TraySetIcon, 0, 3),
	LMD_IMPL(TrayTip, BIF_Linux_TrayTip, 0, 3),
	LMD_NI(WinActivate, 0, 4),
	LMD_NI(WinActivateBottom, 0, 4),
	LMD_NI(WinClose, 0, 5),
	LMD_NI(WinGetClass, 0, 4),
	LMD_NI(WinGetClientPos, 0, 8),
	LMD_NI(WinGetControls, 0, 4),
	LMD_NI(WinGetControlsHwnd, 0, 4),
	LMD_NI(WinGetCount, 0, 4),
	LMD_NI(WinGetExStyle, 0, 4),
	LMD_NI(WinGetID, 0, 4),
	LMD_NI(WinGetIDLast, 0, 4),
	LMD_NI(WinGetList, 0, 4),
	LMD_NI(WinGetMinMax, 0, 4),
	LMD_NI(WinGetPID, 0, 4),
	LMD_NI(WinGetPos, 0, 8),
	LMD_NI(WinGetProcessName, 0, 4),
	LMD_NI(WinGetProcessPath, 0, 4),
	LMD_NI(WinGetStyle, 0, 4),
	LMD_NI(WinGetText, 0, 4),
	LMD_NI(WinGetTitle, 0, 4),
	LMD_NI(WinGetTransColor, 0, 4),
	LMD_NI(WinGetTransparent, 0, 4),
	LMD_NI(WinHide, 0, 4),
	LMD_NI(WinKill, 0, 5),
	LMD_NI(WinMaximize, 0, 4),
	LMD_NI(WinMinimize, 0, 4),
	LMD_NI(WinMinimizeAll, 0, 0),
	LMD_NI(WinMinimizeAllUndo, 0, 0),
	LMD_NI(WinMove, 0, 8),
	LMD_NI(WinMoveBottom, 0, 4),
	LMD_NI(WinMoveTop, 0, 4),
	LMD_NI(WinRedraw, 0, 4),
	LMD_NI(WinRestore, 0, 4),
	LMD_NI(WinSetAlwaysOnTop, 0, 5),
	LMD_NI(WinSetEnabled, 1, 5),
	LMD_NI(WinSetExStyle, 1, 5),
	LMD_NI(WinSetRegion, 0, 5),
	LMD_NI(WinSetStyle, 1, 5),
	LMD_NI(WinSetTitle, 1, 5),
	LMD_NI(WinSetTransColor, 1, 5),
	LMD_NI(WinSetTransparent, 1, 5),
	LMD_NI(WinShow, 0, 4),
	LMD_NI(WinWait, 0, 5),
	LMD_NI(WinWaitActive, 0, 5),
	LMD_NI(WinWaitClose, 0, 5),
	LMD_NI(WinWaitNotActive, 0, 5),
};

#undef LMD_IMPL
#undef LMD_NI

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
	return new BuiltInFunc(fe);
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
