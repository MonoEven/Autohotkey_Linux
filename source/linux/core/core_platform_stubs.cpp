// Linux platform stubs for GUI/hook/input/clipboard symbols that are not
// yet ported.  They let the core interpreter link on Linux; real X11/GTK
// implementations will replace them later.
#include "../../stdafx.h"
#include "../../application.h"
#include "../../clipboard.h"
#include "../../hook.h"
#include "../../keyboard_mouse.h"
#include "../../script.h"
#include "../../input_object.h"
#include "../../WinGroup.h"
#include "../../globaldata.h"
#include "core_timer_linux.h"
#include "core_win_linux.h"
#include "core_hotkey_linux.h"
#include <X11/Xlib.h>

// --- application/message pump ---
bool MsgSleep(int aDuration, MessageMode)
{
	// Real sleep so that Sleep() and other timing-sensitive code behaves
	// correctly on Linux.  When the script has enabled timers, fire due
	// timers during the wait (docs: timed functions run even while the
	// script is waiting for a window or busy with another task); hotkey
	// events are dispatched too.
	if (aDuration <= 0)
	{
		if (g_script.mTimerEnabledCount)
			LinuxCheckScriptTimers();
		if (Hotkey::sHotkeyCount)
			if (Display *d = LinuxX11Display())
				LinuxDispatchHotkeys(d);
		return true;
	}
	DWORD end = GetTickCount() + (DWORD)aDuration;
	for (;;)
	{
		if (g_script.mTimerEnabledCount)
			LinuxCheckScriptTimers();
		if (Hotkey::sHotkeyCount)
			if (Display *d = LinuxX11Display())
				LinuxDispatchHotkeys(d);
		DWORD now = GetTickCount();
		if (now >= end)
			break;
		DWORD slice = end - now > 10 ? 10 : end - now;
		struct timespec ts;
		ts.tv_sec = slice / 1000;
		ts.tv_nsec = (slice % 1000) * 1000000L;
		nanosleep(&ts, nullptr);
	}
	return true;
}
bool MsgMonitor(HWND, UINT, WPARAM, LPARAM, MSG *, LRESULT &aMsgReply) { aMsgReply = 0; return false; }

// ---------------------------------------------------------------------------
// Thread stack management (ports of application.cpp's InitNewThread /
// ResumeUnderlyingThread).  Required so that timer/hotkey subroutines run in
// their own quasi-thread with the default settings, like on Windows.
// ---------------------------------------------------------------------------

void InitNewThread(int aPriority, bool aSkipUninterruptible, bool aIncrementThreadCountAndUpdateTrayIcon
	, bool aIsCritical)
{
	if (aIncrementThreadCountAndUpdateTrayIcon)
	{
		++g_nThreads;
		++g;
	}
	// Copy only settings, not state, from the auto-execute thread.
	memcpy(static_cast<ScriptThreadSettings *>(g), static_cast<ScriptThreadSettings *>(&g_default)
		, sizeof(ScriptThreadSettings));
	global_struct &g_ = *::g;
	global_clear_state(g_);
	g_.Priority = aPriority;
	if (aIncrementThreadCountAndUpdateTrayIcon)
		g_script.UpdateTrayIcon();
	if (aSkipUninterruptible)
		return;
	if (!g_.ThreadIsCritical)
		g_.ThreadIsCritical = aIsCritical;
	if (g_script.mUninterruptibleTime && g_script.mUninterruptedLineCountMax // Both components must be non-zero.
		|| g_.ThreadIsCritical)
	{
		g_.PeekFrequency = UNINTERRUPTIBLE_PEEK_FREQUENCY;
		g_.AllowThreadToBeInterrupted = false;
		if (!g_.ThreadIsCritical)
		{
			if (g_script.mUninterruptibleTime < 0)
				g_.UninterruptibleDuration = -1;
			else
			{
				g_.ThreadStartTime = GetTickCount();
				g_.UninterruptibleDuration = g_script.mUninterruptibleTime;
			}
		}
	}
}

void ResumeUnderlyingThread()
{
	if (g->ThrownToken)
		g_script.FreeExceptionToken(g->ThrownToken);
	--g_nThreads;
	--g;
	g_script.UpdateTrayIcon();
	if (!g_nThreads)
	{
		if (!g_OnExitIsRunning)
			g_script.ExitIfNotPersistent(EXIT_EXIT);
		g_script.mPendingExitCode = 0;
	}
}

BOOL IsInterruptible() { return 1; }
VOID CALLBACK MsgBoxTimeout(HWND, UINT, UINT_PTR, DWORD) {}
VOID CALLBACK RefreshInterruptibility(HWND, UINT, UINT_PTR, DWORD) {}

// --- keyboard / mouse / hook ---
void AddRemoveHooks(HookType, bool) {}
void ChangeHookState(Hotkey **, int, HookType, HookType) {}
bool HookAdjustMaxHotkeys(Hotkey **&aArray, int &aMax, int aNewMax)
{
	// Real realloc: the upstream hook code grows the hotkey array through
	// this function; the old stub returned false which surfaced as a bogus
	// "Out of memory" in Hotkey::AddHotkey.
	auto new_array = (Hotkey **)realloc(aArray, (size_t)aNewMax * sizeof(Hotkey *));
	if (!new_array)
		return false;
	aArray = new_array;
	aMax = aNewMax;
	return true;
}
HookType GetActiveHooks() { return (HookType)0; }
void GetHookStatus(LPTSTR aBuf, int aBufSize) { if (aBuf && aBufSize > 0) aBuf[0] = 0; }
void WaitHookIdle() {}
void SendKeys(LPCTSTR, SendRawModes, SendModes, HWND) {}
void KeyEvent(KeyEventTypes, vk_type, sc_type, HWND, bool, UINT) {}
void KeyEventMenuMask(KeyEventTypes, DWORD) {}
void SetKeyHistoryMax(int) {}
ToggleValueType ToggleKeyState(vk_type, ToggleValueType aToggleValue) { return aToggleValue; }
void SetModifierLRState(modLR_type, modLR_type, HWND, bool, bool, UINT) {}
modLR_type GetModifierLRState(bool) { return 0; }
modLR_type KeyToModifiersLR(vk_type, sc_type, bool *) { return 0; }
modLR_type ConvertModifiers(mod_type) { return 0; }
mod_type ConvertModifiersLR(modLR_type) { return 0; }
LPTSTR ModifiersLRToText(modLR_type, LPTSTR aBuf) { if (aBuf) aBuf[0] = 0; return aBuf; }

// ---------------------------------------------------------------------------
// Key-name table (GetKeyName/GetKeyVK/GetKeySC/GetKeyState on Linux).
// Covers the common AutoHotkey key names with their Win32 VK codes and
// (set 1) scancodes; letters, digits, F1-F24 and Numpad0-9 are handled
// arithmetically in LinuxKeyByName()/LinuxKeyNameFromVK().
// ---------------------------------------------------------------------------
struct LinuxKeyEntry { const wchar_t *name; vk_type vk; sc_type sc; };

static const LinuxKeyEntry sLinuxKeys[] =
{
	{ L"Escape", 0x1B, 0x01 }, { L"Esc", 0x1B, 0x01 },
	{ L"Enter", 0x0D, 0x1C }, { L"Return", 0x0D, 0x1C },
	{ L"Tab", 0x09, 0x0F },
	{ L"Space", 0x20, 0x39 },
	{ L"Backspace", 0x08, 0x0E }, { L"BS", 0x08, 0x0E },
	{ L"Delete", 0x2E, 0x53 }, { L"Del", 0x2E, 0x53 },
	{ L"Insert", 0x2D, 0x52 }, { L"Ins", 0x2D, 0x52 },
	{ L"Home", 0x24, 0x47 }, { L"End", 0x23, 0x4F },
	{ L"PgUp", 0x21, 0x49 }, { L"PgDn", 0x22, 0x51 },
	{ L"Up", 0x26, 0x48 }, { L"Down", 0x28, 0x50 },
	{ L"Left", 0x25, 0x4B }, { L"Right", 0x27, 0x4D },
	{ L"Shift", 0x10, 0x2A }, { L"LShift", 0xA0, 0x2A }, { L"RShift", 0xA1, 0x36 },
	{ L"Ctrl", 0x11, 0x1D }, { L"Control", 0x11, 0x1D }, { L"LCtrl", 0xA2, 0x1D }, { L"RCtrl", 0xA3, 0x1D },
	{ L"Alt", 0x12, 0x38 }, { L"LAlt", 0xA4, 0x38 }, { L"RAlt", 0xA5, 0x38 },
	{ L"Win", 0x5B, 0x5B }, { L"LWin", 0x5B, 0x5B }, { L"RWin", 0x5C, 0x5C },
	{ L"AppsKey", 0x5D, 0x5D }, { L"Menu", 0x5D, 0x5D },
	{ L"PrintScreen", 0x2C, 0x37 }, { L"PrtSc", 0x2C, 0x37 },
	{ L"ScrollLock", 0x91, 0x46 },
	{ L"Pause", 0x13, 0x45 }, { L"Break", 0x13, 0x45 },
	{ L"CapsLock", 0x14, 0x3A },
	{ L"NumLock", 0x90, 0x45 },
	{ L"Numpad0", 0x60, 0x52 }, { L"Numpad1", 0x61, 0x4F }, { L"Numpad2", 0x62, 0x50 },
	{ L"Numpad3", 0x63, 0x51 }, { L"Numpad4", 0x64, 0x4B }, { L"Numpad5", 0x65, 0x4C },
	{ L"Numpad6", 0x66, 0x4D }, { L"Numpad7", 0x67, 0x47 }, { L"Numpad8", 0x68, 0x48 },
	{ L"Numpad9", 0x69, 0x49 },
	{ L"NumpadDiv", 0x6F, 0x35 }, { L"NumpadMult", 0x6A, 0x37 }, { L"NumpadAdd", 0x6B, 0x4E },
	{ L"NumpadSub", 0x6D, 0x4A }, { L"NumpadDot", 0x6E, 0x53 }, { L"NumpadEnter", 0x0D, 0x1C },
	{ L"NumpadIns", 0x60, 0x52 }, { L"NumpadDel", 0x2E, 0x53 }, { L"NumpadHome", 0x24, 0x47 },
	{ L"NumpadEnd", 0x23, 0x4F }, { L"NumpadPgUp", 0x21, 0x49 }, { L"NumpadPgDn", 0x22, 0x51 },
	{ L"NumpadUp", 0x26, 0x48 }, { L"NumpadDown", 0x28, 0x50 }, { L"NumpadLeft", 0x25, 0x4B },
	{ L"NumpadRight", 0x27, 0x4D }, { L"NumpadClear", 0x0C, 0x4C },
	{ L"LButton", 0x01, 0 }, { L"RButton", 0x02, 0 }, { L"MButton", 0x04, 0 },
	{ L"XButton1", 0x05, 0 }, { L"XButton2", 0x06, 0 },
	{ L"WheelUp", 0, 0 }, { L"WheelDown", 0, 0 }, { L"WheelLeft", 0, 0 }, { L"WheelRight", 0, 0 },
	{ L"-", 0xBD, 0x0C }, { L"=", 0xBB, 0x0D }, { L"[", 0xDB, 0x1A }, { L"]", 0xDD, 0x1B },
	{ L"\\", 0xDC, 0x2B }, { L";", 0xBA, 0x27 }, { L"'", 0xDE, 0x28 },
	{ L",", 0xBC, 0x33 }, { L".", 0xBE, 0x34 }, { L"/", 0xBF, 0x35 },
	{ L"`", 0xC0, 0x29 },
};

static bool LinuxKeyByName(LPCTSTR aName, vk_type &aVK, sc_type &aSC)
{
	aVK = 0; aSC = 0;
	if (!aName || !*aName)
		return false;
	for (auto &e : sLinuxKeys)
		if (!_tcsicmp(e.name, aName))
		{
			aVK = e.vk; aSC = e.sc;
			return true;
		}
	if (aName[0] >= 'A' && aName[0] <= 'Z' && !aName[1]) // A-Z
	{
		aVK = (vk_type)(0x41 + (aName[0] - 'A'));
		aSC = (sc_type)(0x1E + (aName[0] - 'A'));
		return true;
	}
	if (aName[0] >= 'a' && aName[0] <= 'z' && !aName[1]) // a-z
	{
		aVK = (vk_type)(0x41 + (aName[0] - 'a'));
		aSC = (sc_type)(0x1E + (aName[0] - 'a'));
		return true;
	}
	if (aName[0] >= '0' && aName[0] <= '9' && !aName[1]) // 0-9
	{
		int d = aName[0] - '0';
		aVK = (vk_type)(0x30 + d);
		aSC = (sc_type)(d ? 0x01 + d : 0x0B); // 1->0x02 ... 9->0x0A, 0->0x0B
		return true;
	}
	if (aName[0] == 'F' || aName[0] == 'f') // F1-F24
	{
		int n = _ttoi(aName + 1);
		if (n >= 1 && n <= 24 && _tcslen(aName) == (n < 10 ? 2 : 3))
		{
			aVK = (vk_type)(0x70 + n - 1);
			aSC = (sc_type)(n <= 10 ? 0x3A + n : n <= 12 ? (n == 11 ? 0x57 : 0x58) : 0x63 + n);
			return true;
		}
	}
	if (!_tcsnicmp(aName, _T("vk"), 2) && _tcslen(aName) == 4) // vkXX
	{
		aVK = (vk_type)wcstoul(aName + 2, nullptr, 16);
		return aVK != 0;
	}
	if (!_tcsnicmp(aName, _T("sc"), 2) && _tcslen(aName) >= 4) // scXXX
	{
		aSC = (sc_type)wcstoul(aName + 2, nullptr, 16);
		return aSC != 0;
	}
	return false;
}

// Non-static accessor for the input module (core_input_linux.cpp).
bool LinuxLookupKey(LPCTSTR aName, vk_type &aVK, sc_type &aSC)
{
	return LinuxKeyByName(aName, aVK, aSC);
}

static LPCTSTR LinuxKeyNameFromVK(vk_type aVK)
{
	for (auto &e : sLinuxKeys)
		if (e.vk == aVK)
			return e.name;
	if (aVK >= 0x41 && aVK <= 0x5A) // A-Z
		return nullptr; // Computed by caller into aBuf.
	return nullptr;
}

static LPCTSTR LinuxKeyNameFromSC(sc_type aSC)
{
	for (auto &e : sLinuxKeys)
		if (e.sc == aSC)
			return e.name;
	return nullptr;
}

vk_type TextToVK(LPCTSTR aKeyName, modLR_type *, bool, bool, HKL)
{
	vk_type vk; sc_type sc;
	if (!LinuxKeyByName(aKeyName, vk, sc))
		return 0;
	return vk ? vk : sc_to_vk(sc);
}

sc_type TextToSC(LPCTSTR aText, bool *aSpecifiedByNumber)
{
	vk_type vk; sc_type sc;
	if (aSpecifiedByNumber)
		*aSpecifiedByNumber = false;
	if (!LinuxKeyByName(aText, vk, sc))
		return 0;
	if (aSpecifiedByNumber && (!_tcsnicmp(aText, _T("sc"), 2) || !_tcsnicmp(aText, _T("vk"), 2)))
		*aSpecifiedByNumber = true;
	return sc;
}

bool TextToVKandSC(LPCTSTR aKeyName, vk_type &aVK, sc_type &aSC, modLR_type *, HKL)
{
	if (!LinuxKeyByName(aKeyName, aVK, aSC))
		return false;
	return aVK != 0 || aSC != 0;
}

LPTSTR GetKeyName(vk_type aVK, sc_type aSC, LPTSTR aBuf, int aBufSize, LPTSTR aDefault)
{
	if (!aBuf || aBufSize <= 0)
		return aDefault ? aDefault : _T("");
	aBuf[0] = _T('\0');
	LPCTSTR name = nullptr;
	if (aVK)
		name = LinuxKeyNameFromVK(aVK);
	if (!name && aSC)
		name = LinuxKeyNameFromSC(aSC);
	if (!name && aVK) // Computed ranges: A-Z, 0-9, F1-F24.
	{
		if (aVK >= 0x41 && aVK <= 0x5A)
		{
			aBuf[0] = (TCHAR)aVK;
			aBuf[1] = _T('\0');
			return aBuf;
		}
		if (aVK >= 0x30 && aVK <= 0x39)
		{
			aBuf[0] = (TCHAR)aVK;
			aBuf[1] = _T('\0');
			return aBuf;
		}
		if (aVK >= 0x70 && aVK <= 0x87)
		{
			sntprintf(aBuf, aBufSize, _T("F%d"), aVK - 0x70 + 1);
			return aBuf;
		}
		if (aVK >= 0x60 && aVK <= 0x69)
		{
			sntprintf(aBuf, aBufSize, _T("Numpad%d"), aVK - 0x60);
			return aBuf;
		}
	}
	if (name)
	{
		_tcsncpy(aBuf, name, aBufSize - 1);
		return aBuf;
	}
	return aDefault ? aDefault : aBuf;
}

sc_type vk_to_sc(vk_type aVK, bool)
{
	for (auto &e : sLinuxKeys)
		if (e.vk == aVK)
			return e.sc;
	return 0;
}

vk_type sc_to_vk(sc_type aSC)
{
	for (auto &e : sLinuxKeys)
		if (e.sc == aSC)
			return e.vk;
	return 0;
}

// --- clipboard ---
ResultType Clipboard::Open() { return FAIL; }
HANDLE Clipboard::GetClipboardDataTimeout(UINT, BOOL *) { return nullptr; }
ResultType Clipboard::Close(LPTSTR) { return OK; }

// --- COM ---
void DefineComPrototypeMembers() {}
BIF_DECL(ComValue_Call) { (void)aParam; (void)aParamCount; }
BIF_DECL(ComObject_Call) { (void)aParam; (void)aParamCount; }
BIF_DECL(ComObjArray_Call) { (void)aParam; (void)aParamCount; }

// --- object/runtime ---
void DefineFileClassLinuxOnPrototype(Object *aPrototype); // core_file_linux.cpp

Object *Object::DefineMetadataMembers(Object *aObj, LPCTSTR aClassName, ObjectMemberMd *, int)
{
	// Linux port: the DynaCall x64 marshaler is unavailable, so built-in
	// classes register their members through hand-written BIF wrappers.
	if (aClassName && !_tcsicmp(aClassName, _T("File")))
	{
		aObj->mFlags |= NativeClassPrototype;
		DefineFileClassLinuxOnPrototype(aObj);
	}
	return aObj;
}
LPTSTR GetExitReasonString(ExitReasons) { return _T(""); }
void *GetDllProcAddress(LPCTSTR, HMODULE *) { return nullptr; }
DWORD GetProcessName(DWORD, LPTSTR aBuf, DWORD aBufSize, bool) { if (aBuf && aBufSize) aBuf[0] = 0; return 0; }

// --- script ---
LineNumberType Script::CurrentLine()
{
	// Mirrors lib/vars.cpp: the line number of the line currently executing.
	return mCurrLine ? mCurrLine->mLineNumber : mCombinedLineNumber;
}
LPTSTR Script::CurrentFile()
{
	// Mirrors lib/vars.cpp: the file of the line currently executing.
	return Line::sSourceFile[mCurrLine ? mCurrLine->mFileIndex : mCurrFileIndex];
}
// Linux has no logon API (CreateProcessWithLogonW).  RunAs() still stores
// the credentials (docs: "Specifies a set of user credentials to use for
// all subsequent Run and RunWait functions"), but launching with them must
// fail like upstream does when the logon call fails: with aDisplayErrors
// the documented "Launch Error (possibly related to RunAs)" is raised,
// otherwise FAIL is returned.
ResultType Script::DoRunAs(LPTSTR aCommandLine, LPCTSTR aWorkingDir, bool aDisplayErrors, WORD
	, PROCESS_INFORMATION &, bool &aSuccess, HANDLE &, DWORD &)
{
	aSuccess = false;
	if (aDisplayErrors)
	{
		TCHAR error_text[2048];
		sntprintf(error_text, _countof(error_text)
			, _T("Launch Error (possibly related to RunAs):\nRunAs is not supported on Linux (no logon API).")
			_T("\nAction: <%-0.400s%s>"), aCommandLine ? aCommandLine : _T("")
			, aCommandLine && _tcslen(aCommandLine) > 400 ? _T("...") : _T(""));
		return RuntimeError(error_text, _T(""), FAIL_OR_OK);
	}
	return FAIL;
}
UserMenu *Script::FindMenu(HMENU) { return nullptr; }

// --- window/group ---
WindowSpec *WinGroup::IsMember(HWND, ScriptThreadSettings &) { return nullptr; }
HKEY Line::RegConvertRootKeyType(LPTSTR) { return nullptr; }
LPTSTR Var::ObjectToText(LPTSTR, LPTSTR aBuf, int aBufSize) { if (aBuf && aBufSize > 0) aBuf[0] = 0; return aBuf; }
void Util_WinKill(HWND) {}
FResult ControlGetClassNN(HWND, HWND, LPTSTR aBuf, int aBufSize) { if (aBuf && aBufSize > 0) aBuf[0] = 0; return OK; }

// --- InputObject ---
Object *InputObject::Create() { return nullptr; }
Object *InputObject::sPrototype = nullptr;
ObjectMemberMd InputObject::sMembers[] = {};
int InputObject::sMemberCount = 0;

// --- GuiType ---
bool GuiType::Delete() { delete this; return true; }
FResult GuiType::Destroy() { return OK; }
void GuiType::DestroyIconsIfUnused(HICON, HICON) {}
GuiType *GuiType::FindGui(HWND, bool) { return nullptr; }
int GuiType::FindOrCreateFont(LPCTSTR, LPCTSTR, FontType *, COLORREF *) { return 0; }
FontType *GuiType::sFont = nullptr;
int GuiType::sFontCount = 0;
ObjectMemberMd GuiType::sMembers[] = {};
int GuiType::sMemberCount = 0;

// --- GuiControlType ---
void GuiControlType::DefineControlClasses() {}

// --- UserMenu ---
UserMenu::UserMenu(MenuTypeType) : Object() {}
UserMenu::~UserMenu() {}
void UserMenu::Dispose() {}
ResultType UserMenu::AppendStandardItems() { return OK; }
ResultType UserMenu::EnableStandardOpenItem(bool) { return OK; }
ResultType UserMenu::Display(bool, int, int) { return OK; }
UserMenuItem *UserMenu::FindItemByID(UINT) { return nullptr; }
ObjectMemberMd UserMenu::sMembers[] = {};
int UserMenu::sMemberCount = 0;

// --- file utilities (POSIX implementations for lib/file.cpp) ---
static bool WideToPath(LPCTSTR aWide, char *aBuf, size_t aBufSize)
{
	if (!aWide || wcstombs(aBuf, aWide, aBufSize) == (size_t)-1)
		return false;
	return true;
}

bool Line::Util_CopyDir(LPCTSTR aSrc, LPCTSTR aDst, int, bool aMove)
{
	char src[4096], dst[4096];
	if (!WideToPath(aSrc, src, sizeof(src)) || !WideToPath(aDst, dst, sizeof(dst)))
		return false;
	try
	{
		std::filesystem::copy(src, dst, std::filesystem::copy_options::recursive
			| std::filesystem::copy_options::overwrite_existing);
		if (aMove)
			std::filesystem::remove_all(src);
		return true;
	}
	catch (...)
	{
		return false;
	}
}

bool Line::Util_RemoveDir(LPCTSTR aSrc, bool)
{
	char src[4096];
	if (!WideToPath(aSrc, src, sizeof(src)))
		return false;
	try
	{
		std::filesystem::remove_all(src);
		return true;
	}
	catch (...)
	{
		return false;
	}
}

// Expand a destination pattern with a matched source filename (port of
// Line::Util_ExpandFilenameWildcard/Part from script_autoit.cpp):
//   copy one.two.three  *.txt     = one.two   .txt
//   copy one.two.three  *.*.txt   = one.two.  .txt  (extra asterisks removed)
//   copy one.two        test      = test
static void LinuxExpandDestWildcard(const std::wstring &aSrcName, const std::wstring &aDestPattern, std::wstring &aOut)
{
	if (aDestPattern.find(L'*') == std::wstring::npos)
	{
		aOut = aDestPattern;
		return;
	}
	auto split_ext = [](const std::wstring &s, std::wstring &file, std::wstring &ext)
	{
		size_t dot = s.find_last_of(L'.');
		size_t slash = s.find_last_of(L'/');
		if (dot != std::wstring::npos && (slash == std::wstring::npos || dot > slash))
		{
			file = s.substr(0, dot);
			ext = s.substr(dot + 1);
		}
		else
		{
			file = s;
			ext.clear();
		}
	};
	// Replace the first '*' with the source part, remove any other '*'.
	auto expand_part = [](const std::wstring &src, const std::wstring &dst) -> std::wstring
	{
		size_t star = dst.find(L'*');
		if (star == std::wstring::npos)
			return dst;
		std::wstring out = dst.substr(0, star);
		out += src;
		for (size_t i = star + 1; i < dst.size(); ++i)
			if (dst[i] != L'*')
				out += dst[i];
		return out;
	};
	std::wstring src_file, src_ext, dst_file, dst_ext;
	split_ext(aSrcName, src_file, src_ext);
	split_ext(aDestPattern, dst_file, dst_ext);
	std::wstring expanded = expand_part(src_file, dst_file);
	if (!src_ext.empty() || !dst_ext.empty())
	{
		// Always include the source extension if the destination extension is blank.
		std::wstring dst_ext2 = dst_ext.empty() ? L"*" : dst_ext;
		std::wstring ext_expanded = expand_part(src_ext, dst_ext2);
		if (!ext_expanded.empty())
		{
			expanded += L'.';
			expanded += ext_expanded;
		}
	}
	aOut = expanded;
}

// Copy or move files with wildcard support (mirrors Line::Util_CopyFile in
// script_autoit.cpp).  Returns the number of files that failed.
int Line::Util_CopyFile(LPCTSTR aSrc, LPCTSTR aDst, bool aOverwrite, bool aMove, DWORD &aLastError)
{
	if (!aSrc || !aDst || !*aSrc || !*aDst)
	{
		aLastError = ERROR_INVALID_PARAMETER;
		return 0;
	}
	std::wstring src(aSrc), dst(aDst);
	for (auto &c : src)
		if (c == L'\\')
			c = L'/';
	for (auto &c : dst)
		if (c == L'\\')
			c = L'/';
	// Strip trailing slashes (keep root "/").
	while (src.size() > 1 && src.back() == L'/')
		src.pop_back();
	while (dst.size() > 1 && dst.back() == L'/')
		dst.pop_back();
	// If the source or dest is a directory, append "/*" (upstream appends \*.*).
	auto is_dir = [](const std::wstring &p) -> bool
	{
		char narrow[4096];
		if (wcstombs(narrow, p.c_str(), sizeof(narrow)) == (size_t)-1)
			return false;
		struct stat st;
		return stat(narrow, &st) == 0 && S_ISDIR(st.st_mode);
	};
	if (is_dir(src))
		src += L"/*";
	if (is_dir(dst))
		dst += L"/*";

	size_t src_slash = src.find_last_of(L'/');
	std::wstring src_dir = src_slash == std::wstring::npos ? L"." : src.substr(0, src_slash);
	if (src_dir.empty())
		src_dir = L"/";
	std::wstring src_pattern = src_slash == std::wstring::npos ? src : src.substr(src_slash + 1);
	size_t dst_slash = dst.find_last_of(L'/');
	std::wstring dst_dir = dst_slash == std::wstring::npos ? L"." : dst.substr(0, dst_slash);
	if (dst_dir.empty())
		dst_dir = L"/";
	std::wstring dst_pattern = dst_slash == std::wstring::npos ? dst : dst.substr(dst_slash + 1);

	WIN32_FIND_DATA findData;
	HANDLE hSearch = FindFirstFile(src.c_str(), &findData);
	if (hSearch == INVALID_HANDLE_VALUE)
	{
		aLastError = 2; // ERROR_FILE_NOT_FOUND.
		// Indicate failure only if there were no wildcards (docs: copying a
		// wildcard pattern that matches nothing is a success).
		return src_pattern.find_first_of(L"?*") == std::wstring::npos ? 1 : 0;
	}
	aLastError = 0; // Set default; overridden only when a failure occurs.

	auto narrow_of = [](const std::wstring &w, char *aBuf, size_t aBufSize) -> bool
	{
		if (wcstombs(aBuf, w.c_str(), aBufSize) == (size_t)-1)
		{
			aBuf[0] = '\0';
			return false;
		}
		aBuf[aBufSize - 1] = '\0';
		return true;
	};

	int failure_count = 0;
	do
	{
		if (findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
			continue; // Only files are copied/moved.
		std::wstring srcname = findData.cFileName;
		std::wstring src_full = src_dir + L"/" + srcname;
		std::wstring dest_full;
		if (dst_pattern.find_first_of(L"?*") == std::wstring::npos)
			dest_full = dst; // No wildcard in dest: use it verbatim.
		else
		{
			std::wstring expanded;
			LinuxExpandDestWildcard(srcname, dst_pattern, expanded);
			dest_full = dst_dir + L"/" + expanded;
		}
		char s8[4096], d8[4096];
		if (!narrow_of(src_full, s8, sizeof(s8)) || !narrow_of(dest_full, d8, sizeof(d8)))
		{
			aLastError = ERROR_INVALID_PARAMETER;
			++failure_count;
			continue;
		}
		try
		{
			// Copy/move onto itself: report success (equivalent() throws if
			// either path does not exist, so guard it).
			bool same_file = false;
			try
			{
				same_file = std::filesystem::equivalent(s8, d8);
			}
			catch (...)
			{
				same_file = false;
			}
			if (same_file)
				continue;
			if (aMove)
			{
				// rename() first (works for same-volume moves); fall back to
				// copy+remove for cross-device moves.
				bool did_rename = false;
				try
				{
					if (std::filesystem::exists(d8) && !aOverwrite)
						throw std::runtime_error("exists");
					std::filesystem::rename(s8, d8);
					did_rename = true;
				}
				catch (const std::filesystem::filesystem_error &)
				{
					// EXDEV or similar: fall through to copy + remove.
				}
				if (!did_rename)
				{
					if (std::filesystem::exists(d8) && !aOverwrite)
						throw std::runtime_error("exists");
					auto opts = std::filesystem::copy_options::overwrite_existing;
					std::filesystem::copy_file(s8, d8, opts);
					std::filesystem::remove(s8);
				}
			}
			else
			{
				auto opts = aOverwrite ? std::filesystem::copy_options::overwrite_existing
					: std::filesystem::copy_options::none;
				std::filesystem::copy_file(s8, d8, opts);
			}
		}
		catch (...)
		{
			aLastError = ERROR_ALREADY_EXISTS;
			++failure_count;
		}
	} while (FindNextFile(hSearch, &findData));
	FindClose(hSearch);
	return failure_count;
}