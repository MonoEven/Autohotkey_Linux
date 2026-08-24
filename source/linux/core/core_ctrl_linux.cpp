// Linux X11 control module (round 8): Control* built-in functions.
//
// On X11 a "control" is a child window of the target window.  Semantics
// follow docs-v2 (Return Value / Error Handling sections) and the upstream
// implementations in lib/win.cpp:
//   - Control identification (per "Control Identifiers"): HWND (integer or
//     "ahk_id N") > ClassNN (WM_CLASS + per-class sequence number, 1-based in
//     child order) > text (WM_NAME/_NET_WM_NAME, matched per
//     SetTitleMatchMode 1 prefix / 2 anywhere / 3 exact / RegEx;
//     case-sensitive).
//   - State that Windows exposes via messages (checkbox checked state,
//     combo/list entries, drop-down visibility) has no X11 equivalent for
//     foreign child windows and is tracked in a virtual per-control store;
//     everything else (text, geometry, visibility, focus, clicks, keys) is
//     a real X11 operation.
//   - ControlGetFocus returns the HWND of the focused control (docs), 0 if
//     the target window has no focused control.
//   - ControlClick reuses the XTEST mouse engine; ControlSend reuses the
//     XTEST Send engine after moving the input focus to the control.
//   - A SetControlDelay sleep follows functions that change a control
//     (docs: "a delay is done automatically after each use of a Control
//     function that changes a control, except ControlSetStyle/ExStyle").
#include "../../stdafx.h"
#include "../../script.h"
#include "../../globaldata.h"
#include "../../script_func_impl.h"
#include "core_ctrl_linux.h"
#include "core_win_linux.h"
#include "core_input_linux.h"
#include "core_atspi_linux.h" // AT-SPI fallback for pure-Wayland Control*
#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <X11/Xutil.h>
#include <cstdlib>
#include <cstring>
#include <cstdio>
#include <cwchar>
#include <cwctype>
#include <string>
#include <vector>
#include <map>
#include <algorithm>
#include <unistd.h> // getpid

void ScriptSleep(int aDelay);

enum LinuxControlSendMode { LCS_FOCUS = 0, LCS_ATSPI = 1 };
static LinuxControlSendMode sControlSendMode = LCS_FOCUS;

// Linux extension: process-wide mode for ControlSend/ControlSendText.
BIV_DECL_R(BIV_ControlSendMode)
{
	aResultToken.SetValue(sControlSendMode == LCS_ATSPI ? _T("atspi") : _T("focus"));
}

BIV_DECL_W(BIV_ControlSendMode_Set)
{
	auto value = BivRValueToString(nullptr);
	if (!_tcsicmp(value, _T("focus")))
		sControlSendMode = LCS_FOCUS;
	else if (!_tcsicmp(value, _T("atspi")))
		sControlSendMode = LCS_ATSPI;
	else
		_f_throw_value(ERR_INVALID_VALUE, value);
}

// Session-level Wayland detection for the Control* AT-SPI fallback.
// Unlike LinuxWaylandActive() this does NOT depend on whether an X display
// (including XWayland, which shares the desktop but never hosts Wayland
// clients) happens to be reachable: a GNOME-on-Wayland session is always a
// Wayland session even when XWayland's socket exists (check0820 - the old
// "no X display" gate silently fell through to the X11 path there).
static bool LinuxCtrlSessionIsWayland()
{
	// XDG_SESSION_TYPE is authoritative when present.  In particular, GNOME
	// still has an Xorg session: desktop name alone must never select AT-SPI.
	const char *st = getenv("XDG_SESSION_TYPE");
	if (st && strcmp(st, "wayland") == 0)
		return true;
	if (st && (strcmp(st, "x11") == 0 || strcmp(st, "xorg") == 0))
		return false;
	// SSH/systemd launch contexts commonly say "tty" or omit the variable;
	// in those cases a supplied Wayland socket is the authoritative fallback.
	// DISPLAY may coexist through XWayland.
	const char *wl = getenv("WAYLAND_DISPLAY");
	return wl && *wl;
}

static bool LinuxCtrlUtf8ToWide(const char *aText, size_t aLength, std::wstring &aOut)
{
	aOut.clear();
	if (!aText || !aLength)
		return true;
	if (aLength > (size_t)INT_MAX)
		return false;
	int needed = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, aText
		, (int)aLength, nullptr, 0);
	if (needed <= 0)
		return false;
	aOut.resize((size_t)needed);
	int written = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, aText
		, (int)aLength, &aOut[0], needed);
	if (written != needed)
	{
		aOut.clear();
		return false;
	}
	return true;
}

static bool LinuxCtrlWideToUtf8(const wchar_t *aText, std::string &aOut)
{
	aOut.clear();
	if (!aText)
		return false;
	int bytes = WideCharToMultiByte(CP_UTF8, 0, aText, -1, nullptr, 0, nullptr, nullptr);
	if (bytes <= 0)
		return false;
	aOut.resize((size_t)bytes);
	int written = WideCharToMultiByte(CP_UTF8, 0, aText, -1, &aOut[0], bytes, nullptr, nullptr);
	if (written != bytes)
	{
		aOut.clear();
		return false;
	}
	if (!aOut.empty() && aOut.back() == '\0')
		aOut.pop_back();
	return true;
}

static bool LinuxCtrlAtspiResolveNamed(ResultToken &aResultToken, ExprTokenType *aParam[]
	, int aParamCount, int aControlIdx, int aTitleIdx, std::string &aPath)
{
	TCHAR control_buf[1024], title_buf[1024];
	LPTSTR control = aControlIdx >= 0 && aControlIdx < aParamCount
		&& !ParamIndexIsOmitted(aControlIdx)
		? TokenToString(*aParam[aControlIdx], control_buf, nullptr) : nullptr;
	if (!control || !*control)
	{
		aResultToken.Error(_T("The specified AT-SPI control name is empty."), _T(""), ErrorPrototype::Target);
		return false;
	}
	if (!LinuxAtspiAvailable())
	{
		aResultToken.Error(_T("The AT-SPI accessibility bus is unavailable."), _T(""), ErrorPrototype::OS);
		return false;
	}
	LPTSTR title = aTitleIdx >= 0 && aTitleIdx < aParamCount
		&& !ParamIndexIsOmitted(aTitleIdx)
		? TokenToString(*aParam[aTitleIdx], title_buf, nullptr) : nullptr;
	std::string control_utf8, title_utf8;
	if (!LinuxCtrlWideToUtf8(control, control_utf8)
		|| (title && *title && !LinuxCtrlWideToUtf8(title, title_utf8)))
	{
		aResultToken.Error(_T("The AT-SPI control/title could not be encoded as UTF-8."), _T(""), ErrorPrototype::Value);
		return false;
	}
	LinuxAtspiRefresh();
	if (!LinuxAtspiFindByName(control_utf8.c_str(), aPath,
		title_utf8.empty() ? nullptr : title_utf8.c_str()))
	{
		aResultToken.Error(_T("The specified AT-SPI control could not be found."), _T(""), ErrorPrototype::Target);
		return false;
	}
	return true;
}

// ---------------------------------------------------------------------------
// Child-window ("control") enumeration and identification
// ---------------------------------------------------------------------------

struct LinuxCtrlEntry
{
	Window win;
	std::wstring cls;   // Class name (WM_CLASS 2nd string; "Window" fallback).
	std::wstring text;  // WM_NAME/_NET_WM_NAME.
};

// Class name of a window: the second (class) string of WM_CLASS.
static std::wstring LinuxCtrlClassOf(Display *d, Window w)
{
	Atom wm_class = XInternAtom(d, "WM_CLASS", False);
	Atom type = None;
	int fmt = 0;
	unsigned long nitems = 0, after = 0;
	unsigned char *prop = nullptr;
	std::wstring cls;
	if (XGetWindowProperty(d, w, wm_class, 0, 64, False, XA_STRING
		, &type, &fmt, &nitems, &after, &prop) == Success && prop && nitems)
	{
		const char *s = (const char *)prop;
		size_t l1 = strlen(s);
		const char *s2 = s + l1 + 1;
		if (l1 && *s2) // WM_CLASS is "instance\0class\0".
		{
			// Modern toolkits commonly store UTF-8 here despite XA_STRING; decode
			// it instead of replacing every non-ASCII byte with '?'.
			LinuxCtrlUtf8ToWide(s2, strlen(s2), cls);
		}
		XFree(prop);
	}
	if (cls.empty())
		cls = L"Window";
	return cls;
}

// Depth-first enumeration of every descendant (upstream EnumChildWindows).
static void LinuxCtrlCollect(Display *d, Window aParent, std::vector<LinuxCtrlEntry> &aOut)
{
	Window root_ret = 0, parent_ret = 0;
	Window *children = nullptr;
	unsigned int n = 0;
	if (!XQueryTree(d, aParent, &root_ret, &parent_ret, &children, &n) || !children)
		return;
	for (unsigned int i = 0; i < n; ++i)
	{
		LinuxCtrlEntry e;
		e.win = children[i];
		e.cls = LinuxCtrlClassOf(d, children[i]);
		LinuxWinTitleEx(d, children[i], e.text);
		aOut.push_back(e);
		LinuxCtrlCollect(d, children[i], aOut);
	}
	XFree(children);
}

// First descendant whose class name contains aClassPart (case-insensitive),
// depth-first; used by StatusBarGetText/Wait (docs: the standard status bar
// is the msctls_statusbar32 common control).  Returns 0 if none.
Window LinuxFindDescendantByClass(Display *d, Window aParent, const wchar_t *aClassPart)
{
	std::vector<LinuxCtrlEntry> ctrls;
	LinuxCtrlCollect(d, aParent, ctrls);
	std::wstring part(aClassPart ? aClassPart : L"");
	for (auto &e : ctrls)
		if (wcsstr(e.cls.c_str(), part.c_str()) != nullptr)
			return e.win;
	return 0;
}

// ClassNN (class + per-class sequence number) of aControl within aWindow.
// The class comparison is case-insensitive and the sequence number is
// compared as a decimal string, mirroring upstream EnumControlFind().
static bool LinuxCtrlClassNN(Display *d, Window aWindow, Window aControl, std::wstring &aOut)
{
	std::vector<LinuxCtrlEntry> ctrls;
	LinuxCtrlCollect(d, aWindow, ctrls);
	std::wstring cls = LinuxCtrlClassOf(d, aControl);
	int seq = 0;
	for (auto &e : ctrls)
	{
		if (!_tcsicmp(e.cls.c_str(), cls.c_str()))
		{
			++seq;
			if (e.win == aControl)
			{
				wchar_t num[32];
				swprintf(num, 32, L"%d", seq);
				aOut = cls + num;
				return true;
			}
		}
	}
	return false;
}

// Text matching per SetTitleMatchMode (1 prefix / 2 anywhere / 3 exact /
// RegEx); case-sensitive by default like window titles.
static bool LinuxCtrlTextMatch(ScriptThreadSettings &aSettings, const std::wstring &aSpec, const std::wstring &aText)
{
	if (aText.empty())
		return aSpec.empty();
	switch (aSettings.TitleMatchMode)
	{
	case 1:
		return aText.size() >= aSpec.size() && wcsncmp(aText.c_str(), aSpec.c_str(), aSpec.size()) == 0;
	case 2:
		return aText.find(aSpec) != std::wstring::npos;
	case 3:
		return aText == aSpec;
	default: // RegEx (TitleMatchMode 4 / RegEx).
		return RegExMatch(aText.c_str(), aSpec.c_str()) != nullptr;
	}
}

// Resolve the Control parameter against the children of aTargetWin:
// HWND (integer or "ahk_id N") > ClassNN > text (upstream ControlExist:
// ClassNN is tried when the spec ends in a digit; text fallback matches
// per TitleMatchMode, case-sensitive like window titles).
static Window LinuxCtrlFind(Display *d, Window aTargetWin, const std::wstring &aSpec
	, ScriptThreadSettings &aSettings, bool aSpecIsHwnd, Window aHwnd)
{
	if (aSpecIsHwnd)
		return aHwnd;
	std::vector<LinuxCtrlEntry> ctrls;
	LinuxCtrlCollect(d, aTargetWin, ctrls);
	// ClassNN, upstream EnumControlFind() semantics: each child whose class
	// name matches the criterion's leading part (case-insensitive) is
	// counted, and the count is compared as a decimal string against the
	// criterion's remainder.  This also handles class names that end in
	// digits (e.g. "msctls_statusbar32" + 1 -> "msctls_statusbar321").
	if (!aSpec.empty() && iswdigit(aSpec.back()))
	{
		int seq = 0;
		for (auto &e : ctrls)
		{
			if (e.cls.size() < aSpec.size()
				&& !_tcsnicmp(aSpec.c_str(), e.cls.c_str(), e.cls.size()))
			{
				++seq;
				wchar_t num[32];
				swprintf(num, 32, L"%d", seq);
				if (!_tcscmp(num, aSpec.c_str() + e.cls.size()))
					return e.win;
			}
		}
	}
	// Text: first control whose text matches per TitleMatchMode.
	for (auto &e : ctrls)
		if (LinuxCtrlTextMatch(aSettings, aSpec, e.text))
			return e.win;
	return 0;
}

// ---------------------------------------------------------------------------
// Virtual per-control state (Windows exposes these via messages)
// ---------------------------------------------------------------------------

struct LinuxCtrlState
{
	DWORD style;      // ControlSetStyle/ControlGetStyle.
	DWORD exstyle;    // ControlSetExStyle/ControlGetExStyle.
	bool enabled;     // ControlSetEnabled/ControlGetEnabled (default true).
	bool checked;     // ControlSetChecked/ControlGetChecked (default false).
	bool dropdown;    // ControlShowDropDown/ControlHideDropDown.
	std::vector<std::wstring> items; // Combo/List entries.
	int cur_index;    // 0 = none selected.
	// Edit-control caret/selection as zero-based character offsets into the
	// control's text; equal values mean "caret, no selection".  Windows
	// exposes these via EM_GETSEL; on X11 they are virtual like the
	// Combo/List entries above.  ControlSetText resets them to (0,0)
	// (WM_SETTEXT semantics); EditPaste replaces the selection and leaves
	// the caret at the end of the pasted string (EM_REPLACESEL semantics).
	int sel_start;
	int sel_end;
	// ListView virtual rows and column count.  On Windows a script creates
	// ListView content with Gui.Add("ListView"); the port has no Gui, so
	// ControlAddItem on a ListView-class control appends a row (a documented
	// Linux extension -- on Windows LB_ADDSTRING is a no-op for a ListView).
	// lv_cols is -1 ("undetermined", i.e. no header) until the first row is
	// added, then 1, mirroring the docs' "Count Col" semantics.
	std::vector<std::vector<std::wstring>> lv_rows;
	int lv_cols;

	LinuxCtrlState() : style(0), exstyle(0), enabled(true), checked(false)
		, dropdown(false), cur_index(0), sel_start(0), sel_end(0), lv_cols(-1) {}
};

static std::map<Window, LinuxCtrlState> &LinuxCtrlStates()
{
	static std::map<Window, LinuxCtrlState> sMap;
	return sMap;
}

static LinuxCtrlState &LinuxCtrlStateOf(Window w)
{
	auto &m = LinuxCtrlStates();
	auto it = m.find(w);
	if (it == m.end())
		it = m.emplace(w, LinuxCtrlState()).first;
	return it->second;
}

// M5-B: virtual control state (style/exstyle/enabled/checked, Combo/List
// entries, Edit caret/selection, ListView rows) has no real X11 effect on
// foreign windows.  Only windows created by this process may use the
// process-local shadow (script-owned GUI semantics); everything else must
// fail explicitly instead of pretending success.  A window is "ours" when its
// top-level ancestor carries _NET_WM_PID equal to this process.
static bool LinuxCtrlIsOwnProcess(Display *d, Window control)
{
	Window w = control;
	Window root = DefaultRootWindow(d);
	for (;;)
	{
		Window root_ret = 0, parent_ret = 0;
		Window *children = nullptr;
		unsigned int n = 0;
		if (!XQueryTree(d, w, &root_ret, &parent_ret, &children, &n))
			return false;
		if (children)
			XFree(children);
		if (!parent_ret || parent_ret == root_ret)
			break;
		w = parent_ret;
	}
	Atom net_pid = XInternAtom(d, "_NET_WM_PID", False);
	Atom type = None;
	int fmt = 0;
	unsigned long nitems = 0, after = 0;
	unsigned char *data = nullptr;
	if (net_pid == None
		|| XGetWindowProperty(d, w, net_pid, 0, 1, False, XA_CARDINAL,
			&type, &fmt, &nitems, &after, &data) != Success
		|| fmt != 32 || nitems < 1 || !data)
	{
		if (data)
			XFree(data);
		return false;
	}
	long pid = *(long *)data;
	XFree(data);
	return pid == (long)getpid();
}

static bool LinuxCtrlRequireOwnProcess(ResultToken &aResultToken, Window control)
{
	Display *d = LinuxX11Display();
	if (d && LinuxCtrlIsOwnProcess(d, control))
		return true;
	aResultToken.Error(_T("NotSupported on Linux for external windows: the operation needs Windows control messages which have no X11 equivalent; only windows created by this script can be modified this way."), _T(""), ErrorPrototype::OS);
	return false;
}

// Docs: ControlAddItem/ChooseIndex/ChooseString/DeleteItem/FindItem/
// GetChoice/GetIndex/GetItems require the control class to contain
// "Combo" or "List" (ChooseIndex/GetIndex also allow "Tab").
static bool LinuxCtrlIsListClass(Display *d, Window w, bool aAllowTab)
{
	std::wstring cls = LinuxCtrlClassOf(d, w);
	auto has = [&](const wchar_t *s) -> bool {
		return wcsstr(cls.c_str(), s) != nullptr;
	};
	return has(L"Combo") || has(L"List") || (aAllowTab && has(L"Tab"));
}

// ---------------------------------------------------------------------------
// Common target-window/control resolution (docs "Control Identifiers")
// ---------------------------------------------------------------------------

// Parse a Control parameter value: HWND (integer token, "ahk_id N", or a
// pure-digit string) > ClassNN/text string.  Docs precedence: HWND first.
static void LinuxCtrlParseSpec(ExprTokenType *aTok, bool &aIsHwnd, Window &aHwnd, std::wstring &aSpec)
{
	aIsHwnd = false;
	aHwnd = 0;
	aSpec.clear();
	if (!aTok)
		return;
	if (aTok->symbol == SYM_INTEGER)
	{
		aIsHwnd = true;
		aHwnd = (Window)TokenToInt64(*aTok);
		return;
	}
	if (aTok->symbol == SYM_OBJECT)
		return; // Object with Hwnd property: unsupported without a GUI.
	TCHAR spec_buf[4096];
	aSpec = TokenToString(*aTok, spec_buf, nullptr);
	if (aSpec.compare(0, 7, L"ahk_id ") == 0)
	{
		aIsHwnd = true;
		aHwnd = (Window)wcstoull(aSpec.c_str() + 7, nullptr, 10);
		return;
	}
	bool all_digits = !aSpec.empty();
	for (wchar_t c : aSpec)
		if (!iswdigit(c))
		{
			all_digits = false;
			break;
		}
	if (all_digits)
	{
		aIsHwnd = true;
		aHwnd = (Window)wcstoull(aSpec.c_str(), nullptr, 10);
	}
}

// Resolve the target window and control for the Control* BIFs.
// aControlIdx: parameter index of the Control value (or -1 when absent).
// Window params start at aTitleIdx (WinTitle), then WinText, ExcludeTitle,
// ExcludeText.  Returns 0 on failure (aResultToken already holds the error);
// on success aControl is the control window and aTarget the top-level window.
static Window LinuxCtrlTarget(ResultToken &aResultToken, ExprTokenType *aParam[], int aParamCount
	, ScriptThreadSettings &aSettings, int aControlIdx, int aTitleIdx, Window &aTarget, Window &aControl)
{
	Display *d = LinuxX11Display();
	if (!d)
	{
		aResultToken.Error(_T("No X display is available."), _T(""), ErrorPrototype::Target);
		return 0;
	}
	aTarget = 0;
	aControl = 0;

	// HWND given directly (integer or "ahk_id N"): window params are ignored.
	bool spec_is_hwnd = false;
	Window spec_hwnd = 0;
	std::wstring spec;
	if (aControlIdx >= 0 && aControlIdx < aParamCount && !ParamIndexIsOmitted(aControlIdx))
		LinuxCtrlParseSpec(aParam[aControlIdx], spec_is_hwnd, spec_hwnd, spec);

	if (spec_is_hwnd)
	{
		if (!spec_hwnd)
		{
			aResultToken.Error(_T("The specified control could not be found."), _T(""), ErrorPrototype::Target);
			return 0;
		}
		aControl = spec_hwnd;
		// Target window = the top-level ancestor (parent chain).
		Window root_ret = 0, parent_ret = 0;
		Window *children = nullptr;
		unsigned int n = 0;
		XQueryTree(d, aControl, &root_ret, &parent_ret, &children, &n);
		if (children)
			XFree(children);
		Window w = aControl;
		while (parent_ret && parent_ret != root_ret)
		{
			w = parent_ret;
			XQueryTree(d, w, &root_ret, &parent_ret, &children, &n);
			if (children)
				XFree(children);
		}
		aTarget = w;
		return aControl;
	}

	aTarget = LinuxWinFindTargetEx(aResultToken, aParam, aParamCount, aSettings, aTitleIdx, aTitleIdx + 1, aTitleIdx + 2);
	if (!aTarget)
		return 0;

	// No Control value: for ControlSend/ControlSendText the target window
	// itself is the "control" (docs); for the others a Control is required,
	// which the interpreter enforces via min-args.
	if (spec.empty())
	{
		aControl = aTarget;
		return aTarget;
	}
	aControl = LinuxCtrlFind(d, aTarget, spec, aSettings, false, 0);
	if (!aControl)
	{
		aResultToken.Error(_T("The specified control could not be found."), _T(""), ErrorPrototype::Target);
		return 0;
	}
	return aTarget;
}

// Exported wrapper for the message BIFs (SendMessage/PostMessage).
bool LinuxCtrlTargetEx(ResultToken &aResultToken, ExprTokenType *aParam[], int aParamCount
	, ScriptThreadSettings &aSettings, int aControlIdx, int aTitleIdx, Window &aTarget, Window &aControl)
{
	return LinuxCtrlTarget(aResultToken, aParam, aParamCount, aSettings, aControlIdx, aTitleIdx
		, aTarget, aControl);
}

// SetControlDelay sleep after changing functions (except SetStyle/ExStyle).
static void LinuxCtrlDelay()
{
	ScriptSleep(g->ControlDelay);
}

// ---------------------------------------------------------------------------
// ControlGetText / ControlSetText / ControlGetPos / ControlMove /
// ControlGetHwnd / ControlGetClassNN / ControlFocus / ControlGetFocus
// ---------------------------------------------------------------------------

BIF_DECL(BIF_Linux_ControlGetText)
{
	// Native-Wayland fallback (check0820): in a Wayland session (even when
	// XWayland exposes an X display that a Wayland app is NOT on) the control
	// is an AT-SPI accessible name; read it on the at-spi bus.  On a real
	// X11 session (no Wayland) the X11 path below is used.
	if (LinuxCtrlSessionIsWayland())
	{
		// AT-SPI control lookup: the CONTROL parameter is param 0
		// (ControlGetText(Control, WinTitle,...)); param 1 is the WinTitle,
		// which is not an accessible name.
		TCHAR nb[1024];
		LPTSTR name = aParamCount > 0 ? TokenToString(*aParam[0], nb, nullptr) : nullptr;
		if (!name || !*name)
			name = (LPTSTR)_T("");
		if (name && *name && LinuxAtspiAvailable())
		{
			char nb[1024];
			wcstombs(nb, name, sizeof(nb) - 1);
			nb[sizeof(nb) - 1] = 0;
			// M5-C WinTitle limiting: the WinTitle (param 1) selects the
			// application subtree so same-named controls in other apps do
			// not cross-match.
			TCHAR wt_buf[1024];
			LPTSTR wintitle = (aParamCount > 1 && !ParamIndexIsOmitted(1))
				? TokenToString(*aParam[1], wt_buf, nullptr) : nullptr;
			char wt[1024] = { 0 };
			if (wintitle && *wintitle)
				wcstombs(wt, wintitle, sizeof(wt) - 1);
			LinuxAtspiRefresh();
			std::string path, t;
			bool read = false;
			if (LinuxAtspiFindByName(nb, path, wt))
			{
				read = LinuxAtspiGetText(path.c_str(), t);
				if (!read)
				{
					double value = 0.0;
					read = LinuxAtspiGetValue(path.c_str(), value, &t);
				}
			}
			if (read)
			{
				std::wstring w;
				if (!LinuxCtrlUtf8ToWide(t.data(), t.size(), w))
				{
					aResultToken.Error(_T("ControlGetText: AT-SPI returned invalid UTF-8."));
					return;
				}
				LinuxWinSetPersistentEx(aResultToken, w);
				return;
			}
		}
		LinuxWinSetPersistentEx(aResultToken, std::wstring());
		return;
	}
	Window target = 0, control = 0;
	if (!LinuxCtrlTarget(aResultToken, aParam, aParamCount, *g, 0, 1, target, control))
		return;
	std::wstring text;
	LinuxWinTitleEx(LinuxX11Display(), control, text);
	LinuxWinSetPersistentEx(aResultToken, text);
}

// Write a control's text as _NET_WM_NAME (UTF-8) + WM_NAME (legacy);
// shared by ControlSetText and EditPaste.
static void LinuxCtrlWriteText(Display *d, Window control, const wchar_t *text)
{
	Atom utf8 = XInternAtom(d, "UTF8_STRING", False);
	Atom net_wm_name = XInternAtom(d, "_NET_WM_NAME", False);
	char utf8buf[131072];
	int len = WideCharToUTF8(text, utf8buf, (int)sizeof(utf8buf));
	if (len > 0)
	{
		XChangeProperty(d, control, net_wm_name, utf8, 8, PropModeReplace
			, (const unsigned char *)utf8buf, (unsigned long)(len - 1));
		XStoreName(d, control, utf8buf);
		XFlush(d);
	}
}

BIF_DECL(BIF_Linux_ControlSetText)
{
	// Native-Wayland fallback (check0820): in a Wayland session (even when
	// XWayland exposes an X display that a Wayland app is NOT on) use AT-SPI
	// EditableText.SetTextContents on the accessible named by the Control.
	// When the at-spi bus is unavailable the call fails soft (no error):
	// there is no other control path on a pure-Wayland session.
	if (LinuxCtrlSessionIsWayland())
	{
		// Standard v2 signature: ControlSetText(NewText, Control, WinTitle,...).
		// The former Wayland branch had these first two parameters reversed,
		// silently targeting the new text as an accessible name.
		TCHAR tb[65536], cb[1024];
		LPTSTR text = aParamCount > 0 ? TokenToString(*aParam[0], tb, nullptr) : nullptr;
		LPTSTR ctrl = aParamCount > 1 ? TokenToString(*aParam[1], cb, nullptr) : nullptr;
		if (ctrl && *ctrl && text && LinuxAtspiAvailable())
		{
			char nb[1024], nb2[65536];
			wcstombs(nb, ctrl, sizeof(nb) - 1);
			nb[sizeof(nb) - 1] = 0;
			wcstombs(nb2, text, sizeof(nb2) - 1);
			nb2[sizeof(nb2) - 1] = 0;
			LinuxAtspiRefresh();
			std::string path;
			TCHAR wt_buf[1024];
			LPTSTR wintitle = (aParamCount > 2 && !ParamIndexIsOmitted(2))
				? TokenToString(*aParam[2], wt_buf, nullptr) : nullptr;
			char wt[1024] = { 0 };
			if (wintitle && *wintitle)
				wcstombs(wt, wintitle, sizeof(wt) - 1);
			if (LinuxAtspiFindByName(nb, path, wt))
			{
				double current_value = 0.0;
				if (LinuxAtspiGetValue(path.c_str(), current_value, nullptr))
				{
					wchar_t *end = nullptr;
					double value = wcstod(text, &end);
					while (end && iswspace(*end)) ++end;
					if (!end || end == text || *end)
					{
						aResultToken.Error(_T("A numeric value is required for this AT-SPI Value control."), _T(""), ErrorPrototype::Value);
						return;
					}
					if (!LinuxAtspiSetValue(path.c_str(), value))
					{
						aResultToken.Error(_T("The AT-SPI CurrentValue property could not be changed."), _T(""), ErrorPrototype::OS);
						return;
					}
					LinuxCtrlDelay();
					return;
				}
				if (LinuxAtspiSetText(path.c_str(), nb2))
				{
					LinuxCtrlDelay();
					return;
				}
			}
		}
		LinuxCtrlDelay();
		return; // Fail soft: no at-SPI match (or no bus) on Wayland.
	}
	Window target = 0, control = 0;
	if (!LinuxCtrlTarget(aResultToken, aParam, aParamCount, *g, 1, 2, target, control))
		return;
	Display *d = LinuxX11Display();
	TCHAR text_buf[65536];
	LPTSTR text = TokenToString(*aParam[0], text_buf, nullptr);
	LinuxCtrlWriteText(d, control, text ? text : text_buf);
	// WM_SETTEXT moves the caret to the start and clears the selection.
	LinuxCtrlState &s = LinuxCtrlStateOf(control);
	s.sel_start = 0;
	s.sel_end = 0;
	LinuxCtrlDelay();
}

BIF_DECL(BIF_Linux_ControlGetPos)
{
	// Docs: the Control parameter is required (unlike the outputs, which are
	// all optional) -- the LMD table marks all params optional so this is
	// enforced here.
	if (aParamCount <= 4 || ParamIndexIsOmitted(4))
	{
		aResultToken.Error(ERR_PARAM_REQUIRED, _T(""), ErrorPrototype::Error);
		return;
	}
	Window target = 0, control = 0;
	if (!LinuxCtrlTarget(aResultToken, aParam, aParamCount, *g, 4, 5, target, control))
		return;
	Display *d = LinuxX11Display();
	// Position relative to the target window's upper-left corner.
	// XTranslateCoordinates yields the interior origin; the control's outer
	// upper-left corner (what the docs describe) is offset by the border.
	int rx = 0, ry = 0;
	Window child_ret = 0;
	XTranslateCoordinates(d, control, target, 0, 0, &rx, &ry, &child_ret);
	XWindowAttributes attrs;
	int w = 0, h = 0;
	if (XGetWindowAttributes(d, control, &attrs))
	{
		rx -= attrs.border_width;
		ry -= attrs.border_width;
		w = attrs.width;
		h = attrs.height;
	}
	Var *out;
	if (aParamCount > 0 && (out = TokenToOutputVar(*aParam[0]))) out->Assign((__int64)rx);
	if (aParamCount > 1 && (out = TokenToOutputVar(*aParam[1]))) out->Assign((__int64)ry);
	if (aParamCount > 2 && (out = TokenToOutputVar(*aParam[2]))) out->Assign((__int64)w);
	if (aParamCount > 3 && (out = TokenToOutputVar(*aParam[3]))) out->Assign((__int64)h);
}

BIF_DECL(BIF_Linux_ControlMove)
{
	// Docs: the Control parameter is required; X/Y/Width/Height are optional.
	if (aParamCount <= 4 || ParamIndexIsOmitted(4))
	{
		aResultToken.Error(ERR_PARAM_REQUIRED, _T(""), ErrorPrototype::Error);
		return;
	}
	Window target = 0, control = 0;
	if (!LinuxCtrlTarget(aResultToken, aParam, aParamCount, *g, 4, 5, target, control))
		return;
	Display *d = LinuxX11Display();
	// Current position relative to the target window (outer upper-left
	// corner, like ControlGetPos: subtract the border from the translation).
	int rx = 0, ry = 0;
	Window child_ret = 0;
	XTranslateCoordinates(d, control, target, 0, 0, &rx, &ry, &child_ret);
	XWindowAttributes attrs;
	int w = 0, h = 0;
	if (XGetWindowAttributes(d, control, &attrs))
	{
		rx -= attrs.border_width;
		ry -= attrs.border_width;
		w = attrs.width;
		h = attrs.height;
	}
	int x = rx, y = ry;
	if (aParamCount > 0 && !ParamIndexIsOmitted(0)) x = (int)TokenToInt64(*aParam[0]);
	if (aParamCount > 1 && !ParamIndexIsOmitted(1)) y = (int)TokenToInt64(*aParam[1]);
	if (aParamCount > 2 && !ParamIndexIsOmitted(2)) w = (int)TokenToInt64(*aParam[2]);
	if (aParamCount > 3 && !ParamIndexIsOmitted(3)) h = (int)TokenToInt64(*aParam[3]);
	XWindowAttributes cattrs;
	if (XGetWindowAttributes(d, control, &cattrs))
	{
		// attrs.x/y are relative to the control's parent; apply the delta.
		XMoveResizeWindow(d, control, cattrs.x + (x - rx), cattrs.y + (y - ry)
			, (unsigned)w, (unsigned)h);
		XSync(d, False);
	}
	LinuxCtrlDelay();
}

BIF_DECL(BIF_Linux_ControlGetHwnd)
{
	Window target = 0, control = 0;
	if (!LinuxCtrlTarget(aResultToken, aParam, aParamCount, *g, 0, 1, target, control))
		return;
	aResultToken.SetValue((__int64)control);
}

BIF_DECL(BIF_Linux_ControlGetClassNN)
{
	Window target = 0, control = 0;
	if (!LinuxCtrlTarget(aResultToken, aParam, aParamCount, *g, 0, 1, target, control))
		return;
	Display *d = LinuxX11Display();
	// A top-level window itself has no ClassNN (upstream uses the parent
	// window, which on X11 is the root); return an empty string.
	std::wstring classnn;
	if (target != control && LinuxCtrlClassNN(d, target, control, classnn))
		LinuxWinSetPersistentEx(aResultToken, classnn);
	else
		aResultToken.SetValue(_T(""));
}

BIF_DECL(BIF_Linux_ControlFocus)
{
	Window target = 0, control = 0;
	if (!LinuxCtrlTarget(aResultToken, aParam, aParamCount, *g, 0, 1, target, control))
		return;
	Display *d = LinuxX11Display();
	XSetInputFocus(d, control, RevertToParent, CurrentTime);
	XSync(d, False);
	LinuxCtrlDelay();
}

BIF_DECL(BIF_Linux_ControlGetFocus)
{
	// Docs: returns the HWND of the focused control; 0 if the target
	// window has no focused control.
	Display *d = LinuxX11Display();
	if (!d)
	{
		aResultToken.Error(_T("No X display is available."), _T(""), ErrorPrototype::Target);
		return;
	}
	Window target = LinuxWinFindTargetEx(aResultToken, aParam, aParamCount, *g, 0, 1, 2);
	if (!target)
		return;
	Window focus = 0;
	int revert = 0;
	if (!XGetInputFocus(d, &focus, &revert))
	{
		aResultToken.Error(_T("Unable to determine the keyboard focus."), _T(""), ErrorPrototype::OS);
		return;
	}
	if (!focus || focus == PointerRoot || focus == None)
	{
		aResultToken.SetValue((__int64)0);
		return;
	}
	// The focused control must be a descendant of the target window.
	Window root_ret = 0, parent_ret = 0;
	Window *children = nullptr;
	unsigned int n = 0;
	Window w = focus;
	bool is_child = false;
	while (!is_child)
	{
		if (w == target)
		{
			is_child = true;
			break;
		}
		if (!XQueryTree(d, w, &root_ret, &parent_ret, &children, &n))
			break;
		if (children)
			XFree(children);
		if (!parent_ret || parent_ret == root_ret)
			break;
		w = parent_ret;
	}
	aResultToken.SetValue((__int64)(is_child ? (Window)focus : 0));
}

// ---------------------------------------------------------------------------
// ControlGetStyle/ExStyle/SetStyle/ExStyle, ControlGet/SetEnabled,
// ControlGet/SetChecked, ControlGetVisible/Show/Hide
// ---------------------------------------------------------------------------

BIF_DECL(BIF_Linux_ControlGetStyle)
{
	Window target = 0, control = 0;
	if (!LinuxCtrlTarget(aResultToken, aParam, aParamCount, *g, 0, 1, target, control))
		return;
	if (!LinuxCtrlRequireOwnProcess(aResultToken, control))
		return;
	aResultToken.SetValue((__int64)(ULONG_PTR)LinuxCtrlStateOf(control).style);
}

BIF_DECL(BIF_Linux_ControlGetExStyle)
{
	Window target = 0, control = 0;
	if (!LinuxCtrlTarget(aResultToken, aParam, aParamCount, *g, 0, 1, target, control))
		return;
	if (!LinuxCtrlRequireOwnProcess(aResultToken, control))
		return;
	aResultToken.SetValue((__int64)(ULONG_PTR)LinuxCtrlStateOf(control).exstyle);
}

// Value: "0x80" replaces, "+0x80" adds, "-0x80" removes, "^0x80" toggles
// (docs ControlSetStyle).
static void LinuxCtrlSetStyleBits(ResultToken &aResultToken, ExprTokenType *aParam[], int aParamCount
	, int aValueIdx, Window control, bool aExStyle)
{
	TCHAR val_buf[64];
	LPTSTR val = aValueIdx < aParamCount ? TokenToString(*aParam[aValueIdx], val_buf, nullptr) : nullptr;
	LinuxCtrlState &s = LinuxCtrlStateOf(control);
	DWORD &bits = aExStyle ? s.exstyle : s.style;
	const wchar_t *p = val ? val : L"0";
	wchar_t op = 0;
	if (*p == L'+' || *p == L'-' || *p == L'^')
		op = *p++;
	DWORD mask = (DWORD)wcstoul(p, nullptr, 0);
	if (op == L'+')
		bits |= mask;
	else if (op == L'-')
		bits &= ~mask;
	else if (op == L'^')
		bits ^= mask;
	else
		bits = mask;
}

BIF_DECL(BIF_Linux_ControlSetStyle)
{
	Window target = 0, control = 0;
	if (!LinuxCtrlTarget(aResultToken, aParam, aParamCount, *g, 1, 2, target, control))
		return;
	if (!LinuxCtrlRequireOwnProcess(aResultToken, control))
		return;
	LinuxCtrlSetStyleBits(aResultToken, aParam, aParamCount, 0, control, false);
}

BIF_DECL(BIF_Linux_ControlSetExStyle)
{
	Window target = 0, control = 0;
	if (!LinuxCtrlTarget(aResultToken, aParam, aParamCount, *g, 1, 2, target, control))
		return;
	if (!LinuxCtrlRequireOwnProcess(aResultToken, control))
		return;
	LinuxCtrlSetStyleBits(aResultToken, aParam, aParamCount, 0, control, true);
}

BIF_DECL(BIF_Linux_ControlGetEnabled)
{
	Window target = 0, control = 0;
	if (!LinuxCtrlTarget(aResultToken, aParam, aParamCount, *g, 0, 1, target, control))
		return;
	if (!LinuxCtrlRequireOwnProcess(aResultToken, control))
		return;
	aResultToken.SetValue((__int64)(LinuxCtrlStateOf(control).enabled ? 1 : 0));
}

BIF_DECL(BIF_Linux_ControlSetEnabled)
{
	Window target = 0, control = 0;
	if (!LinuxCtrlTarget(aResultToken, aParam, aParamCount, *g, 1, 2, target, control))
		return;
	if (!LinuxCtrlRequireOwnProcess(aResultToken, control))
		return;
	LinuxCtrlState &s = LinuxCtrlStateOf(control);
	__int64 v = TokenToInt64(*aParam[0]);
	s.enabled = v == -1 ? !s.enabled : (v != 0);
}

BIF_DECL(BIF_Linux_ControlGetChecked)
{
	Window target = 0, control = 0;
	if (!LinuxCtrlTarget(aResultToken, aParam, aParamCount, *g, 0, 1, target, control))
		return;
	if (!LinuxCtrlRequireOwnProcess(aResultToken, control))
		return;
	aResultToken.SetValue((__int64)(LinuxCtrlStateOf(control).checked ? 1 : 0));
}

BIF_DECL(BIF_Linux_ControlSetChecked)
{
	Window target = 0, control = 0;
	if (!LinuxCtrlTarget(aResultToken, aParam, aParamCount, *g, 1, 2, target, control))
		return;
	if (!LinuxCtrlRequireOwnProcess(aResultToken, control))
		return;
	LinuxCtrlState &s = LinuxCtrlStateOf(control);
	__int64 v = TokenToInt64(*aParam[0]);
	s.checked = v == -1 ? !s.checked : (v != 0);
}

BIF_DECL(BIF_Linux_ControlGetVisible)
{
	Window target = 0, control = 0;
	if (!LinuxCtrlTarget(aResultToken, aParam, aParamCount, *g, 0, 1, target, control))
		return;
	Display *d = LinuxX11Display();
	XWindowAttributes attrs;
	aResultToken.SetValue((__int64)(XGetWindowAttributes(d, control, &attrs) && attrs.map_state == IsViewable ? 1 : 0));
}

BIF_DECL(BIF_Linux_ControlShow)
{
	Window target = 0, control = 0;
	if (!LinuxCtrlTarget(aResultToken, aParam, aParamCount, *g, 0, 1, target, control))
		return;
	Display *d = LinuxX11Display();
	XMapRaised(d, control);
	XSync(d, False);
	LinuxCtrlDelay();
}

BIF_DECL(BIF_Linux_ControlHide)
{
	Window target = 0, control = 0;
	if (!LinuxCtrlTarget(aResultToken, aParam, aParamCount, *g, 0, 1, target, control))
		return;
	Display *d = LinuxX11Display();
	XUnmapWindow(d, control);
	XSync(d, False);
	LinuxCtrlDelay();
}

// ---------------------------------------------------------------------------
// ControlClick
// ---------------------------------------------------------------------------

BIF_DECL(BIF_Linux_ControlClick)
{
	// ControlID-or-Pos: omitted -> click the target window itself;
	// "X55 Y33" -> client-area coordinates; ClassNN/text/HWND -> the control.
	Display *d = LinuxX11Display();
	if (!d || LinuxCtrlSessionIsWayland())
	{
		// Native-Wayland fallback (check0820): in a Wayland session (even
		// when XWayland exposes an X display that a Wayland app is NOT on)
		// the control is an AT-SPI accessible name and "click" is its
		// Action[0] (GTK/Qt/Electron buttons expose "click"/"activate"
		// actions).  Only the plain name form is supported here (no
		// position/HWND/coordinate mode): those need a real display and fail
		// below with the same honest error as before.
		if (aParamCount > 0 && !ParamIndexIsOmitted(0) && LinuxAtspiAvailable())
		{
			bool spec_is_hwnd = false;
			Window spec_hwnd = 0;
			std::wstring spec;
			LinuxCtrlParseSpec(aParam[0], spec_is_hwnd, spec_hwnd, spec);
			if (!spec_is_hwnd && !spec.empty())
			{
				// Skip obvious position mode ("X55 Y33": X + digits, a space,
				// then Y + digits) - those need a real display just like X11.
				size_t sp = spec.find_first_of(L" \t");
				bool pos_like = (spec[0] == L'x' || spec[0] == L'X') && iswdigit(spec[1])
					&& sp != std::wstring::npos && sp + 2 < spec.size()
					&& (spec[sp + 1] == L'y' || spec[sp + 1] == L'Y') && iswdigit(spec[sp + 2]);
				if (!pos_like)
				{
					char nb[1024];
					wcstombs(nb, spec.c_str(), sizeof(nb) - 1);
					nb[sizeof(nb) - 1] = 0;
					LinuxAtspiRefresh();
					std::string path;
					TCHAR wt_buf[1024];
					LPTSTR wintitle = (aParamCount > 1 && !ParamIndexIsOmitted(1))
						? TokenToString(*aParam[1], wt_buf, nullptr) : nullptr;
					char wt[1024] = { 0 };
					if (wintitle && *wintitle)
						wcstombs(wt, wintitle, sizeof(wt) - 1);
					// Prefer the "click" action by name (robust across role
					// naming), then fall back to Action[0] ("push button" etc).
					if (LinuxAtspiFindByName(nb, path, wt)
						&& (LinuxAtspiDoAction(path.c_str(), -1, "click")
							|| LinuxAtspiDoAction(path.c_str(), 0, nullptr)))
					{
						LinuxCtrlDelay();
						return;
					}
				}
			}
		}
		if (!d)
		{
			aResultToken.Error(_T("No X display is available."), _T(""), ErrorPrototype::Target);
			return;
		}
		// XWayland target not present on the AT-SPI tree (or Action returned
		// false): continue to the real X11/XTEST path below.
	}
	// Parse Options (param 5): "NA", "D", "U", "xN yN" (relative to the
	// control).  The X/Y option letters are ignored in position mode.
	bool down_only = false, up_only = false;
	int opt_x = -1, opt_y = -1;
	if (aParamCount > 5 && !ParamIndexIsOmitted(5))
	{
		TCHAR opt_buf[256];
		LPTSTR opts = TokenToString(*aParam[5], opt_buf, nullptr);
		for (const wchar_t *p = opts; *p; )
		{
			while (*p == L' ' || *p == L'\t') ++p;
			if ((*p == L'D' || *p == L'd') && (p[1] == 0 || p[1] == L' ' || p[1] == L'\t'))
			{
				down_only = true; ++p;
			}
			else if ((*p == L'U' || *p == L'u') && (p[1] == 0 || p[1] == L' ' || p[1] == L'\t'))
			{
				up_only = true; ++p;
			}
			else if ((*p == L'x' || *p == L'X') && iswdigit(p[1]))
			{
				opt_x = (int)wcstol(p + 1, nullptr, 10);
				while (iswdigit(*p)) ++p;
			}
			else if ((*p == L'y' || *p == L'Y') && iswdigit(p[1]))
			{
				opt_y = (int)wcstol(p + 1, nullptr, 10);
				while (iswdigit(*p)) ++p;
			}
			else
				++p; // "NA" and unknown tokens are ignored.
		}
	}

	bool pos_mode = false;
	int pos_x = 0, pos_y = 0;
	std::wstring spec;
	bool spec_is_hwnd = false;
	Window spec_hwnd = 0;
	if (aParamCount > 0 && !ParamIndexIsOmitted(0))
	{
		LinuxCtrlParseSpec(aParam[0], spec_is_hwnd, spec_hwnd, spec);
		if (!spec_is_hwnd && !spec.empty())
		{
			// "X55 Y33": X before Y, separated by space/tab (position mode).
			size_t sp = spec.find_first_of(L" \t");
			if (sp != std::wstring::npos && (spec[0] == L'x' || spec[0] == L'X')
				&& iswdigit(spec[1]) && (spec[sp + 1] == L'y' || spec[sp + 1] == L'Y')
				&& iswdigit(spec[sp + 2]))
			{
				pos_mode = true;
				pos_x = (int)wcstol(spec.c_str() + 1, nullptr, 10);
				pos_y = (int)wcstol(spec.c_str() + sp + 2, nullptr, 10);
			}
		}
	}

	Window target = 0, control = 0;
	if (spec_is_hwnd)
	{
		if (!LinuxCtrlTarget(aResultToken, aParam, aParamCount, *g, 0, 1, target, control))
			return;
	}
	else if (!pos_mode && !spec.empty())
	{
		if (!LinuxCtrlTarget(aResultToken, aParam, aParamCount, *g, 0, 1, target, control))
			return;
	}
	else
	{
		// Position mode (or omitted): the target window itself.
		target = LinuxWinFindTargetEx(aResultToken, aParam, aParamCount, *g, 1, 2, 3);
		if (!target)
			return;
		control = target;
	}

	// Click coordinates in root space.
	int root_x = 0, root_y = 0;
	Window child_ret = 0;
	if (pos_mode || control == target)
	{
		XTranslateCoordinates(d, target, DefaultRootWindow(d), pos_x, pos_y, &root_x, &root_y, &child_ret);
	}
	else
	{
		XWindowAttributes attrs;
		if (!XGetWindowAttributes(d, control, &attrs))
		{
			aResultToken.Error(_T("The specified control could not be found."), _T(""), ErrorPrototype::Target);
			return;
		}
		int cx = attrs.width / 2, cy = attrs.height / 2;
		if (opt_x >= 0) cx = opt_x;
		if (opt_y >= 0) cy = opt_y;
		XTranslateCoordinates(d, control, DefaultRootWindow(d), cx, cy, &root_x, &root_y, &child_ret);
	}

	// WhichButton (param 3, default Left), ClickCount (param 4, default 1).
	unsigned int btn = 1;
	if (aParamCount > 3 && !ParamIndexIsOmitted(3))
	{
		TCHAR btn_buf[32];
		LPTSTR bname = TokenToString(*aParam[3], btn_buf, nullptr);
		if (bname && *bname)
		{
			std::wstring b(bname);
			// Docs accept "Left"/"Right"/"Middle"/"X1"/"X2" and their
			// single-letter forms; Wheel* buttons are not simulated.
			if (b.size() == 1)
			{
				if (b[0] == L'L' || b[0] == L'l') btn = 1;
				else if (b[0] == L'R' || b[0] == L'r') btn = 3;
				else if (b[0] == L'M' || b[0] == L'm') btn = 2;
			}
			else if (!LinuxButtonFromNameEx(bname, btn))
			{
				aResultToken.Error(_T("Invalid button name."), _T(""), ErrorPrototype::Value);
				return;
			}
		}
	}
	int count = aParamCount > 4 && !ParamIndexIsOmitted(4) ? (int)TokenToInt64(*aParam[4]) : 1;
	if (count < 1)
		count = 1;

	LinuxFakeMotionEvent(d, root_x, root_y);
	XSync(d, False);
	if (up_only)
		LinuxFakeButtonEvent(d, btn, false);
	else if (down_only)
		LinuxFakeButtonEvent(d, btn, true);
	else
		for (int i = 0; i < count; ++i)
		{
			LinuxFakeButtonEvent(d, btn, true);
			LinuxFakeButtonEvent(d, btn, false);
		}
	LinuxCtrlDelay();
}

// ---------------------------------------------------------------------------
// ControlSend / ControlSendText
// ---------------------------------------------------------------------------

static void LinuxCtrlSendAtspi(ResultToken &aResultToken, ExprTokenType *aParam[]
	, int aParamCount, bool aRaw)
{
	TCHAR keys_buf[65536], ctrl_buf[1024], title_buf[1024];
	LPTSTR keys = aParamCount > 0 ? TokenToString(*aParam[0], keys_buf, nullptr) : nullptr;
	LPTSTR ctrl = (aParamCount > 1 && !ParamIndexIsOmitted(1))
		? TokenToString(*aParam[1], ctrl_buf, nullptr) : nullptr;
	LPTSTR title = (aParamCount > 2 && !ParamIndexIsOmitted(2))
		? TokenToString(*aParam[2], title_buf, nullptr) : nullptr;
	if (!ctrl || !*ctrl)
	{
		aResultToken.Error(_T("A_ControlSendMode=atspi requires a named control."), _T(""), ErrorPrototype::Target);
		return;
	}
	if (!LinuxAtspiAvailable())
	{
		aResultToken.Error(_T("A_ControlSendMode=atspi requires an available AT-SPI accessibility bus."), _T(""), ErrorPrototype::OS);
		return;
	}
	char control_name[4096], window_name[4096] = { 0 };
	if (WideCharToUTF8(ctrl, control_name, (int)sizeof(control_name)) <= 0)
	{
		aResultToken.Error(_T("ControlSend: invalid control name."), _T(""), ErrorPrototype::Value);
		return;
	}
	if (title && *title)
		WideCharToUTF8(title, window_name, (int)sizeof(window_name));
	LinuxAtspiRefresh();
	std::string path;
	if (!LinuxAtspiFindByName(control_name, path, window_name))
	{
		aResultToken.Error(_T("ControlSend: the AT-SPI control or WinTitle could not be found."), _T(""), ErrorPrototype::Target);
		return;
	}
	if (!keys)
		keys = keys_buf;
	if (!aRaw && (!_tcsicmp(keys, _T("{Enter}")) || !_tcsicmp(keys, _T("{Space}"))))
	{
		if (!(LinuxAtspiDoAction(path.c_str(), -1, "click")
			|| LinuxAtspiDoAction(path.c_str(), 0, nullptr)))
			aResultToken.Error(_T("ControlSend: the AT-SPI control exposes no usable Action."), _T(""), ErrorPrototype::OS);
		else
			LinuxCtrlDelay();
		return;
	}
	if (!aRaw && wcspbrk(keys, L"{}^!+#"))
	{
		aResultToken.Error(_T("ControlSendMode=atspi supports plain text or {Enter}/{Space}; complex Send syntax is NotSupported."), _T(""), ErrorPrototype::OS);
		return;
	}
	std::string current;
	if (!LinuxAtspiGetText(path.c_str(), current))
	{
		aResultToken.Error(_T("ControlSendMode=atspi requires an EditableText control."), _T(""), ErrorPrototype::OS);
		return;
	}
	char append[131072];
	int len = WideCharToUTF8(keys, append, (int)sizeof(append));
	if (len <= 0)
	{
		aResultToken.Error(_T("ControlSend: text could not be encoded as UTF-8."), _T(""), ErrorPrototype::Value);
		return;
	}
	current.append(append, (size_t)(len - 1));
	if (!LinuxAtspiSetText(path.c_str(), current.c_str()))
	{
		aResultToken.Error(_T("ControlSendMode=atspi could not update the EditableText control."), _T(""), ErrorPrototype::OS);
		return;
	}
	LinuxCtrlDelay();
}

static void LinuxCtrlSendImpl(ResultToken &aResultToken, ExprTokenType *aParam[], int aParamCount, bool aRaw)
{
	if (sControlSendMode == LCS_ATSPI)
	{
		LinuxCtrlSendAtspi(aResultToken, aParam, aParamCount, aRaw);
		return;
	}
	Display *d = LinuxX11Display();
	if (!d)
	{
		aResultToken.Error(_T("No X display is available."), _T(""), ErrorPrototype::Target);
		return;
	}
	// Keys = param 0; Control = param 1 (optional -> the target window).
	Window target = 0, control = 0;
	if (!LinuxCtrlTarget(aResultToken, aParam, aParamCount, *g, 1, 2, target, control))
		return;
	// Focus the control so the XTEST events reach it, then restore focus.
	Window prev_focus = 0;
	int prev_revert = 0;
	XGetInputFocus(d, &prev_focus, &prev_revert);
	XSetInputFocus(d, control, RevertToParent, CurrentTime);
	XSync(d, False);
	TCHAR keys_buf[65536];
	LPTSTR keys = aParamCount > 0 ? TokenToString(*aParam[0], keys_buf, nullptr) : nullptr;
	if (keys)
	{
		if (aRaw)
			LinuxSendCharsString(d, keys);
		else
			LinuxSendKeysString(d, keys);
	}
	if (prev_focus && prev_focus != PointerRoot && prev_focus != None)
	{
		XSetInputFocus(d, prev_focus, RevertToParent, CurrentTime);
		XSync(d, False);
	}
	LinuxCtrlDelay();
}

BIF_DECL(BIF_Linux_ControlSend)     { LinuxCtrlSendImpl(aResultToken, aParam, aParamCount, false); }
BIF_DECL(BIF_Linux_ControlSendText) { LinuxCtrlSendImpl(aResultToken, aParam, aParamCount, true); }

// ---------------------------------------------------------------------------
// Combo/List virtual operations
// ---------------------------------------------------------------------------

// Common list-target resolution: the control class must contain "Combo" or
// "List" (docs); ChooseIndex/GetIndex also accept "Tab".
static bool LinuxCtrlListTarget(ResultToken &aResultToken, ExprTokenType *aParam[], int aParamCount
	, bool aAllowTab, Window &aControl, int aControlIdx = 1, int aTitleIdx = 2)
{
	Window target = 0, control = 0;
	if (!LinuxCtrlTarget(aResultToken, aParam, aParamCount, *g, aControlIdx, aTitleIdx, target, control))
		return false;
	Display *d = LinuxX11Display();
	if (!LinuxCtrlIsListClass(d, control, aAllowTab))
	{
		aResultToken.Error(_T("The specified control is not a ComboBox or ListBox."), _T(""), ErrorPrototype::Target);
		return false;
	}
	if (!LinuxCtrlRequireOwnProcess(aResultToken, control))
		return false;
	aControl = control;
	return true;
}

BIF_DECL(BIF_Linux_ControlAddItem)
{
	Window control = 0;
	if (!LinuxCtrlListTarget(aResultToken, aParam, aParamCount, false, control))
		return;
	TCHAR item_buf[65536];
	LPTSTR item = TokenToString(*aParam[0], item_buf, nullptr);
	LinuxCtrlState &s = LinuxCtrlStateOf(control);
	// Linux extension: on a ListView-class control the entry becomes a new
	// row (column 1) in the virtual ListView store used by
	// ListViewGetContent.  (On Windows LB_ADDSTRING is not handled by a
	// ListView, so upstream ControlAddItem is a silent no-op there; the
	// port has no Gui with which a script could otherwise create ListView
	// content, hence this extension.)
	if (wcsstr(LinuxCtrlClassOf(LinuxX11Display(), control).c_str(), L"ListView"))
	{
		std::vector<std::wstring> row(1, item ? item : L"");
		s.lv_rows.push_back(std::move(row));
		if (s.lv_cols < 1)
			s.lv_cols = 1; // A row exists, so the header has one column.
		aResultToken.SetValue((__int64)s.lv_rows.size()); // 1-based row index.
		return;
	}
	s.items.push_back(item ? item : L"");
	aResultToken.SetValue((__int64)s.items.size()); // Docs: index of the new entry.
}

BIF_DECL(BIF_Linux_ControlDeleteItem)
{
	Window control = 0;
	if (!LinuxCtrlListTarget(aResultToken, aParam, aParamCount, false, control))
		return;
	// Docs: N = the index of the entry (1-based).
	int idx = (int)TokenToInt64(*aParam[0]);
	LinuxCtrlState &s = LinuxCtrlStateOf(control);
	if (wcsstr(LinuxCtrlClassOf(LinuxX11Display(), control).c_str(), L"ListView"))
	{
		// Linux extension (see ControlAddItem): delete a ListView row.
		if (idx < 1 || (size_t)idx > s.lv_rows.size())
		{
			aResultToken.Error(_T("The entry could not be deleted."), _T(""), ErrorPrototype::Error);
			return;
		}
		s.lv_rows.erase(s.lv_rows.begin() + (idx - 1));
		return;
	}
	if (idx < 1 || (size_t)idx > s.items.size())
	{
		aResultToken.Error(_T("The entry could not be deleted."), _T(""), ErrorPrototype::Error);
		return;
	}
	s.items.erase(s.items.begin() + (idx - 1));
	if (s.cur_index == idx)
		s.cur_index = 0;
	else if (s.cur_index > idx)
		--s.cur_index;
}

BIF_DECL(BIF_Linux_ControlFindItem)
{
	if (LinuxCtrlSessionIsWayland())
	{
		std::string path;
		if (!LinuxCtrlAtspiResolveNamed(aResultToken, aParam, aParamCount, 1, 2, path))
			return;
		std::vector<std::string> items;
		if (!LinuxAtspiSelectionGetItems(path.c_str(), items))
		{
			aResultToken.Error(_T("The AT-SPI control does not expose Selection children."), _T(""), ErrorPrototype::OS);
			return;
		}
		TCHAR item_buf[65536];
		LPTSTR wanted = TokenToString(*aParam[0], item_buf, nullptr);
		for (size_t i = 0; i < items.size(); ++i)
		{
			std::wstring item;
			if (LinuxCtrlUtf8ToWide(items[i].data(), items[i].size(), item)
				&& !_tcsicmp(item.c_str(), wanted))
			{
				aResultToken.SetValue((__int64)(i + 1));
				return;
			}
		}
		aResultToken.Error(_T("The entry could not be found."), _T(""), ErrorPrototype::Error);
		return;
	}
	Window control = 0;
	if (!LinuxCtrlListTarget(aResultToken, aParam, aParamCount, false, control))
		return;
	// Docs: full-text, case-insensitive search; throws Error if not found.
	TCHAR item_buf[65536];
	LPTSTR item = TokenToString(*aParam[0], item_buf, nullptr);
	std::wstring want(item ? item : L"");
	LinuxCtrlState &s = LinuxCtrlStateOf(control);
	for (size_t i = 0; i < s.items.size(); ++i)
		if (!_tcsicmp(s.items[i].c_str(), want.c_str()))
		{
			aResultToken.SetValue((__int64)(i + 1));
			return;
		}
	aResultToken.Error(_T("The entry could not be found."), _T(""), ErrorPrototype::Error);
}

BIF_DECL(BIF_Linux_ControlChooseIndex)
{
	if (LinuxCtrlSessionIsWayland())
	{
		std::string path;
		if (!LinuxCtrlAtspiResolveNamed(aResultToken, aParam, aParamCount, 1, 2, path))
			return;
		int idx = (int)TokenToInt64(*aParam[0]);
		std::vector<std::string> items;
		if (!LinuxAtspiSelectionGetItems(path.c_str(), items)
			|| idx < 0 || (size_t)idx > items.size())
		{
			aResultToken.Error(_T("The selection index could not be applied."), _T(""), ErrorPrototype::Error);
			return;
		}
		if (!LinuxAtspiSelectionSelect(path.c_str(), idx ? idx - 1 : -1))
		{
			aResultToken.Error(_T("The AT-SPI Selection operation failed."), _T(""), ErrorPrototype::OS);
			return;
		}
		LinuxCtrlDelay();
		return;
	}
	Window control = 0;
	if (!LinuxCtrlListTarget(aResultToken, aParam, aParamCount, true, control))
		return;
	int idx = (int)TokenToInt64(*aParam[0]);
	LinuxCtrlState &s = LinuxCtrlStateOf(control);
	if (idx < 0 || (size_t)idx > s.items.size())
	{
		aResultToken.Error(_T("The change could not be applied."), _T(""), ErrorPrototype::Error);
		return;
	}
	s.cur_index = idx; // 0 = deselect all (docs).
}

BIF_DECL(BIF_Linux_ControlChooseString)
{
	if (LinuxCtrlSessionIsWayland())
	{
		std::string path;
		if (!LinuxCtrlAtspiResolveNamed(aResultToken, aParam, aParamCount, 1, 2, path))
			return;
		std::vector<std::string> items;
		if (!LinuxAtspiSelectionGetItems(path.c_str(), items))
		{
			aResultToken.Error(_T("The AT-SPI control does not expose Selection children."), _T(""), ErrorPrototype::OS);
			return;
		}
		TCHAR item_buf[65536];
		LPTSTR wanted = TokenToString(*aParam[0], item_buf, nullptr);
		size_t wanted_length = _tcslen(wanted);
		for (size_t i = 0; i < items.size(); ++i)
		{
			std::wstring item;
			if (LinuxCtrlUtf8ToWide(items[i].data(), items[i].size(), item)
				&& !_tcsnicmp(item.c_str(), wanted, wanted_length))
			{
				if (!LinuxAtspiSelectionSelect(path.c_str(), (int)i))
				{
					aResultToken.Error(_T("The AT-SPI Selection operation failed."), _T(""), ErrorPrototype::OS);
					return;
				}
				aResultToken.SetValue((__int64)(i + 1));
				LinuxCtrlDelay();
				return;
			}
		}
		aResultToken.Error(_T("The entry could not be found."), _T(""), ErrorPrototype::Error);
		return;
	}
	Window control = 0;
	if (!LinuxCtrlListTarget(aResultToken, aParam, aParamCount, false, control))
		return;
	// Docs: full text or leading part, case-insensitive, first match;
	// returns the index of the selected entry.
	TCHAR item_buf[65536];
	LPTSTR item = TokenToString(*aParam[0], item_buf, nullptr);
	std::wstring want(item ? item : L"");
	LinuxCtrlState &s = LinuxCtrlStateOf(control);
	for (size_t i = 0; i < s.items.size(); ++i)
		if (_tcsnicmp(s.items[i].c_str(), want.c_str(), want.size()) == 0)
		{
			s.cur_index = (int)(i + 1);
			aResultToken.SetValue((__int64)(i + 1));
			return;
		}
	aResultToken.Error(_T("The entry could not be found."), _T(""), ErrorPrototype::Error);
}

BIF_DECL(BIF_Linux_ControlGetChoice)
{
	if (LinuxCtrlSessionIsWayland())
	{
		std::string path;
		if (!LinuxCtrlAtspiResolveNamed(aResultToken, aParam, aParamCount, 0, 1, path))
			return;
		int index = -1;
		std::string name;
		if (!LinuxAtspiSelectionGetSelected(path.c_str(), index, name) || index < 0)
		{
			aResultToken.Error(_T("No AT-SPI entry is selected."), _T(""), ErrorPrototype::Error);
			return;
		}
		std::wstring wide;
		if (!LinuxCtrlUtf8ToWide(name.data(), name.size(), wide))
		{
			aResultToken.Error(_T("AT-SPI returned invalid UTF-8."));
			return;
		}
		LinuxWinSetPersistentEx(aResultToken, wide);
		return;
	}
	Window control = 0;
	if (!LinuxCtrlListTarget(aResultToken, aParam, aParamCount, false, control, 0, 1))
		return;
	LinuxCtrlState &s = LinuxCtrlStateOf(control);
	if (s.cur_index < 1 || (size_t)s.cur_index > s.items.size())
	{
		aResultToken.Error(_T("No entry is selected."), _T(""), ErrorPrototype::Error);
		return;
	}
	LinuxWinSetPersistentEx(aResultToken, s.items[s.cur_index - 1]);
}

BIF_DECL(BIF_Linux_ControlGetIndex)
{
	if (LinuxCtrlSessionIsWayland())
	{
		std::string path;
		if (!LinuxCtrlAtspiResolveNamed(aResultToken, aParam, aParamCount, 0, 1, path))
			return;
		int index = -1;
		std::string name;
		if (!LinuxAtspiSelectionGetSelected(path.c_str(), index, name))
		{
			aResultToken.Error(_T("The AT-SPI control does not expose Selection."), _T(""), ErrorPrototype::OS);
			return;
		}
		aResultToken.SetValue((__int64)(index + 1)); // 0 when no selection.
		return;
	}
	Window control = 0;
	if (!LinuxCtrlListTarget(aResultToken, aParam, aParamCount, true, control, 0, 1))
		return;
	aResultToken.SetValue((__int64)LinuxCtrlStateOf(control).cur_index);
}

BIF_DECL(BIF_Linux_ControlGetItems)
{
	if (LinuxCtrlSessionIsWayland())
	{
		std::string path;
		if (!LinuxCtrlAtspiResolveNamed(aResultToken, aParam, aParamCount, 0, 1, path))
			return;
		std::vector<std::string> items;
		if (!LinuxAtspiSelectionGetItems(path.c_str(), items))
		{
			aResultToken.Error(_T("The AT-SPI control does not expose Selection children."), _T(""), ErrorPrototype::OS);
			return;
		}
		Array *array = Array::Create();
		if (!array)
		{
			aResultToken.SetValue(_T(""));
			return;
		}
		for (const auto &item : items)
		{
			std::wstring wide;
			if (!LinuxCtrlUtf8ToWide(item.data(), item.size(), wide))
			{
				array->Release();
				aResultToken.Error(_T("AT-SPI returned invalid UTF-8."));
				return;
			}
			array->Append(wide.c_str());
		}
		aResultToken.SetValue(array);
		return;
	}
	Window control = 0;
	if (!LinuxCtrlListTarget(aResultToken, aParam, aParamCount, false, control, 0, 1))
		return;
	Array *arr = Array::Create();
	LinuxCtrlState &s = LinuxCtrlStateOf(control);
	if (arr)
	{
		for (auto &item : s.items)
			arr->Append(item.c_str());
		aResultToken.SetValue(arr);
	}
	else
		aResultToken.SetValue(_T(""));
}

BIF_DECL(BIF_Linux_ControlShowDropDown)
{
	Window target = 0, control = 0;
	if (!LinuxCtrlTarget(aResultToken, aParam, aParamCount, *g, 0, 1, target, control))
		return;
	if (!LinuxCtrlRequireOwnProcess(aResultToken, control))
		return;
	LinuxCtrlStateOf(control).dropdown = true;
}

BIF_DECL(BIF_Linux_ControlHideDropDown)
{
	Window target = 0, control = 0;
	if (!LinuxCtrlTarget(aResultToken, aParam, aParamCount, *g, 0, 1, target, control))
		return;
	if (!LinuxCtrlRequireOwnProcess(aResultToken, control))
		return;
	LinuxCtrlStateOf(control).dropdown = false;
}

// ---------------------------------------------------------------------------
// WinGetControls / WinGetControlsHwnd (real implementations; the old stubs
// in core_win_linux.cpp returned an empty array)
// ---------------------------------------------------------------------------

static void LinuxWinGetControlsImpl(ResultToken &aResultToken, ExprTokenType *aParam[], int aParamCount, bool aHwnds)
{
	Display *d = LinuxX11Display();
	if (!d)
	{
		aResultToken.Error(_T("No X display is available."), _T(""), ErrorPrototype::Target);
		return;
	}
	Window w = LinuxWinFindTargetEx(aResultToken, aParam, aParamCount, *g, 0, 1, 2);
	if (!w)
		return;
	std::vector<LinuxCtrlEntry> ctrls;
	LinuxCtrlCollect(d, w, ctrls);
	Array *arr = Array::Create();
	if (!arr)
	{
		aResultToken.SetValue(_T(""));
		return;
	}
	// Sequence numbers are per-class over the whole descendant set.
	std::map<std::wstring, int> seq;
	for (auto &e : ctrls)
	{
		if (aHwnds)
			arr->Append((__int64)e.win);
		else
		{
			int n = ++seq[e.cls];
			wchar_t num[32];
			swprintf(num, 32, L"%d", n);
			std::wstring classnn = e.cls + num;
			arr->Append(classnn.c_str());
		}
	}
	aResultToken.SetValue(arr);
}

BIF_DECL(BIF_Linux_WinGetControls)
{
	LinuxWinGetControlsImpl(aResultToken, aParam, aParamCount, false);
}

BIF_DECL(BIF_Linux_WinGetControlsHwnd)
{
	LinuxWinGetControlsImpl(aResultToken, aParam, aParamCount, true);
}

// ---------------------------------------------------------------------------
// Edit / EditGet* / EditPaste / ListViewGetContent
// ---------------------------------------------------------------------------
//
// Windows implements these via EM_* / LVM_* messages to the control.  On
// X11 the control's text is real (WM_NAME/_NET_WM_NAME, so ControlSetText/
// ControlGetText/EditPaste round-trip), while the caret/selection
// (EM_GETSEL) and ListView rows (LVM_*) are tracked in the virtual
// per-control store, like the Combo/List entries above.  Error handling
// follows docs-v2: TargetError when the window/control is not found,
// ValueError when an index/option is invalid.

// The control's text (real X11 property).
static std::wstring LinuxCtrlTextOf(Window control)
{
	std::wstring text;
	LinuxWinTitleEx(LinuxX11Display(), control, text);
	return text;
}

// Split on '\n'; lines keep a trailing '\r' for CRLF text, mirroring the
// docs: "Depending on the nature of the control, the string might end in a
// carriage return (`r) or a carriage return + linefeed (`r`n)."
static void LinuxCtrlSplitLines(const std::wstring &aText, std::vector<std::wstring> &aLines)
{
	aLines.clear();
	size_t start = 0;
	for (size_t i = 0; i <= aText.size(); ++i)
		if (i == aText.size() || aText[i] == L'\n')
		{
			aLines.push_back(aText.substr(start, i - start));
			start = i + 1;
		}
}

// Docs (EditGetLineCount): "All edit controls have at least one line, even
// if the control is empty."
static int LinuxCtrlLineCount(const std::wstring &aText)
{
	int n = 1;
	for (wchar_t c : aText)
		if (c == L'\n')
			++n;
	return n;
}

// 1-based line number containing character offset aPos (EM_LINEFROMCHAR).
static int LinuxCtrlLineOf(const std::wstring &aText, int aPos)
{
	int line = 1;
	int end = aPos < (int)aText.size() ? aPos : (int)aText.size();
	for (int i = 0; i < end; ++i)
		if (aText[i] == L'\n')
			++line;
	return line;
}

// 1-based column of aPos within its line (EM_LINEINDEX based).
static int LinuxCtrlColOf(const std::wstring &aText, int aPos)
{
	int line_start = 0;
	int end = aPos < (int)aText.size() ? aPos : (int)aText.size();
	for (int i = 0; i < end; ++i)
		if (aText[i] == L'\n')
			line_start = i + 1;
	return aPos - line_start + 1;
}

// Docs (Edit): "Opens the current script for editing."  Scripts loaded from
// stdin have no file to open (upstream Script::Edit returns OK for those).
// On Linux the editor is taken from $VISUAL/$EDITOR when set (spawned
// asynchronously as `$EDITOR "script"` so a terminal editor does not block
// the script); otherwise the upstream path applies: reuse an already-open
// editor window if one exists, else open the file with the system default
// handler (xdg-open on Linux).
BIF_DECL(BIF_Linux_Edit)
{
	if (g_script.mKind != Script::ScriptKindFile)
		return;
	// $VISUAL takes precedence over $EDITOR (usual convention); both are
	// byte strings, converted here for the wide CreateProcess shim.
	const char *editor_env = getenv("VISUAL");
	if (!editor_env || !*editor_env)
		editor_env = getenv("EDITOR");
	if (editor_env && *editor_env)
	{
		wchar_t editor_wide[4096];
		if (mbstowcs(editor_wide, editor_env, _countof(editor_wide)) != (size_t)-1)
		{
			std::wstring cmd(editor_wide);
			cmd += L" \"";
			cmd += g_script.mFileSpec ? g_script.mFileSpec : L"";
			cmd += L"\"";
			PROCESS_INFORMATION pi = {0};
			if (CreateProcess(nullptr, &cmd[0], nullptr, nullptr, FALSE, 0, nullptr
				, nullptr, nullptr, &pi))
			{
				if (pi.hProcess)
					CloseHandle(pi.hProcess);
				return;
			}
		}
	}
	g_script.Edit(nullptr);
}

BIF_DECL(BIF_Linux_EditGetLineCount)
{
	Window target = 0, control = 0;
	if (!LinuxCtrlTarget(aResultToken, aParam, aParamCount, *g, 0, 1, target, control))
		return;
	aResultToken.SetValue((__int64)LinuxCtrlLineCount(LinuxCtrlTextOf(control)));
}

BIF_DECL(BIF_Linux_EditGetCurrentLine)
{
	Window target = 0, control = 0;
	if (!LinuxCtrlTarget(aResultToken, aParam, aParamCount, *g, 0, 1, target, control))
		return;
	if (!LinuxCtrlRequireOwnProcess(aResultToken, control))
		return;
	// Docs: "If there is text selected in the control, the return value is
	// the line number where the selection begins."
	LinuxCtrlState &s = LinuxCtrlStateOf(control);
	aResultToken.SetValue((__int64)LinuxCtrlLineOf(LinuxCtrlTextOf(control), s.sel_start));
}

BIF_DECL(BIF_Linux_EditGetCurrentCol)
{
	Window target = 0, control = 0;
	if (!LinuxCtrlTarget(aResultToken, aParam, aParamCount, *g, 0, 1, target, control))
		return;
	if (!LinuxCtrlRequireOwnProcess(aResultToken, control))
		return;
	LinuxCtrlState &s = LinuxCtrlStateOf(control);
	aResultToken.SetValue((__int64)LinuxCtrlColOf(LinuxCtrlTextOf(control), s.sel_start));
}

BIF_DECL(BIF_Linux_EditGetLine)
{
	// Docs: ValueError if N is out of range, TargetError if the control
	// could not be found.
	Window target = 0, control = 0;
	if (!LinuxCtrlTarget(aResultToken, aParam, aParamCount, *g, 1, 2, target, control))
		return;
	std::vector<std::wstring> lines;
	LinuxCtrlSplitLines(LinuxCtrlTextOf(control), lines);
	int n = (int)TokenToInt64(*aParam[0]);
	if (n < 1 || (size_t)n > lines.size())
	{
		FResultToError(aResultToken, aParam, aParamCount, FR_E_ARG(0), 0);
		return;
	}
	LinuxWinSetPersistentEx(aResultToken, lines[n - 1]);
}

BIF_DECL(BIF_Linux_EditGetSelectedText)
{
	Window target = 0, control = 0;
	if (!LinuxCtrlTarget(aResultToken, aParam, aParamCount, *g, 0, 1, target, control))
		return;
	LinuxCtrlState &s = LinuxCtrlStateOf(control);
	// Docs: "If no text is selected, an empty string is returned."
	if (s.sel_start == s.sel_end)
	{
		aResultToken.SetValue(_T(""));
		return;
	}
	std::wstring text = LinuxCtrlTextOf(control);
	size_t start = (size_t)s.sel_start;
	size_t len = (size_t)(s.sel_end - s.sel_start);
	if (start + len > text.size())
	{
		// Safety net; the state invariants make this unreachable.
		aResultToken.SetValue(_T(""));
		return;
	}
	LinuxWinSetPersistentEx(aResultToken, text.substr(start, len));
}

BIF_DECL(BIF_Linux_EditPaste)
{
	// Docs: "Pastes the specified string into an Edit control, replacing
	// any selected text."  EM_REPLACESEL semantics: with no selection the
	// string is inserted at the caret; the caret ends up after the pasted
	// string.
	Window target = 0, control = 0;
	if (!LinuxCtrlTarget(aResultToken, aParam, aParamCount, *g, 1, 2, target, control))
		return;
	TCHAR paste_buf[65536];
	LPTSTR paste = TokenToString(*aParam[0], paste_buf, nullptr);
	std::wstring text = LinuxCtrlTextOf(control);
	LinuxCtrlState &s = LinuxCtrlStateOf(control);
	int start = s.sel_start, end = s.sel_end;
	if (start > end)
	{
		int t = start; start = end; end = t;
	}
	size_t plen = wcslen(paste);
	std::wstring newtext = text.substr(0, (size_t)start) + paste
		+ text.substr((size_t)end);
	LinuxCtrlWriteText(LinuxX11Display(), control, newtext.c_str());
	s.sel_start = s.sel_end = start + (int)plen;
	LinuxCtrlDelay();
}

// Case-insensitive substring search (upstream tcscasestr).
static const wchar_t *LinuxStriStr(const wchar_t *aHay, const wchar_t *aNeedle)
{
	if (!aHay || !*aNeedle)
		return aHay;
	for (const wchar_t *p = aHay; *p; ++p)
	{
		const wchar_t *h = p, *n = aNeedle;
		while (*h && *n && towlower(*h) == towlower(*n))
		{
			++h; ++n;
		}
		if (!*n)
			return p;
	}
	return nullptr;
}

BIF_DECL(BIF_Linux_ListViewGetContent)
{
	Window target = 0, control = 0;
	if (!LinuxCtrlTarget(aResultToken, aParam, aParamCount, *g, 1, 2, target, control))
		return;
	TCHAR opt_buf[512];
	LPTSTR options = (aParamCount > 0 && !ParamIndexIsOmitted(0))
		? TokenToString(*aParam[0], opt_buf, nullptr) : nullptr;
	if (!options)
		options = opt_buf;
	LinuxCtrlState &s = LinuxCtrlStateOf(control);
	int row_count = (int)s.lv_rows.size();
	int col_count = s.lv_cols; // -1 = "undetermined" (no header, per docs).

	// Simple option parse (upstream: "a simple vs. strict method is used to
	// reduce code size"); option matching is case-insensitive.
	bool get_count = LinuxStriStr(options, _T("Count")) != nullptr;
	bool include_selected_only = LinuxStriStr(options, _T("Selected")) != nullptr;
	bool include_focused_only = LinuxStriStr(options, _T("Focused")) != nullptr;
	const wchar_t *col_option = LinuxStriStr(options, _T("Col"));
	int requested_col = col_option ? (int)wcstol(col_option + 3, nullptr, 10) - 1 : -1;
	// Docs: ColN -- N is the column number (1-based); an invalid
	// specification (or a column beyond the control's count) throws a
	// ValueError.  With an undetermined column count any ColN is attempted.
	if (col_option && (get_count
			? (col_option[3] && !iswspace(col_option[3]))
			: (requested_col < 0 || (col_count > -1 && requested_col >= col_count))))
	{
		FResultToError(aResultToken, aParam, aParamCount, FR_E_ARG(0), 0);
		return;
	}
	if (get_count)
	{
		// Docs: "Count" = number of rows; "Count Col" = number of columns
		// (-1 if undetermined); "Count Selected" / "Count Focused" = 0 on
		// a control with no selected/focused row (the port has no way to
		// mark a row selected or focused; on Windows these read
		// LVM_GETSELECTEDCOUNT / LVM_GETNEXTITEM-LVNI_FOCUSED).
		int result = include_focused_only ? 0
			: include_selected_only ? 0
			: col_option ? col_count
			: row_count;
		aResultToken.SetValue((__int64)result);
		return;
	}
	// Upstream: "No text in the control, so indicate success."
	if (row_count < 1 || !col_count)
	{
		LinuxWinSetPersistentEx(aResultToken, std::wstring());
		return;
	}
	// Selective modes: no row is ever selected/focused on the port, so the
	// result is empty (upstream stops at the first LVM_GETNEXTITEM -1).
	if (include_focused_only || include_selected_only)
	{
		LinuxWinSetPersistentEx(aResultToken, std::wstring());
		return;
	}
	// Build the text: rows are delimited by linefeeds, fields by tabs
	// (docs).  single_col_mode fetches one column per row (the requested
	// one, or column 1 when the column count is undetermined).
	bool single_col_mode = requested_col > -1 || col_count == -1;
	std::wstring out;
	for (int i = 0; i < row_count; ++i)
	{
		if (i)
			out += L'\n';
		if (single_col_mode)
		{
			int c = requested_col > -1 ? requested_col : 0;
			if ((size_t)c < s.lv_rows[i].size())
				out += s.lv_rows[i][c];
		}
		else
		{
			for (int c = 0; c < col_count; ++c)
			{
				if (c)
					out += L'\t';
				if ((size_t)c < s.lv_rows[i].size())
					out += s.lv_rows[i][c];
			}
		}
	}
	LinuxWinSetPersistentEx(aResultToken, out);
}
