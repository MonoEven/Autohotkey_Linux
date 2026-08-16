// Linux X11 window module (round 6): Win* / Group* built-in functions.
//
// Semantics follow docs-v2 (Return Value / Error Handling sections) and the
// upstream implementations in window.cpp / lib/win.cpp / lib/wait.cpp:
//   - HWND == X11 Window id (decimal in scripts, like "ahk_id 12345").
//   - WinTitle criteria: plain title + ahk_id/ahk_pid/ahk_class/ahk_exe/
//     ahk_group, separated by spaces; title matching follows
//     SetTitleMatchMode (1 prefix / 2 anywhere / 3 exact / RegEx);
//     titles and classes are case-sensitive, ahk_exe is case-insensitive
//     (matching the upstream _tcsstr/_tcscmp/_tcsicmp calls).
//   - DetectHiddenWindows gates visibility (X map state).
//   - WinSet* style/ex-style/transparency/always-on-top are recorded in a
//     virtual state map AND published via the corresponding EWMH properties
//     so that real window managers honour them.
#include "../../stdafx.h"
#include "../../script.h"
#include "../../globaldata.h"
#include "../../script_func_impl.h"
#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <X11/Xutil.h>
#include <X11/extensions/shape.h>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <cstdio>
#include <cwctype>
#include <string>
#include <vector>
#include <map>
#include <list>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <unistd.h>
#include <sys/stat.h>

void ScriptSleep(int aDelay);

// ---------------------------------------------------------------------------
// Display access (cached on success; retried on failure).
// ---------------------------------------------------------------------------

// X11 error handler: windows can vanish at any time (XGetInputFocus may even
// return PointerRoot/None), and the default Xlib handler would terminate the
// process.  Ignore protocol errors; every caller checks return values.
static int LinuxXErrorHandler(Display *, XErrorEvent *)
{
	return 0;
}

static Display *LinuxWinDisplay()
{
	static Display *sDpy = nullptr;
	if (!sDpy)
	{
		sDpy = XOpenDisplay(nullptr);
		if (sDpy)
			XSetErrorHandler(LinuxXErrorHandler);
	}
	return sDpy;
}

// ---------------------------------------------------------------------------
// Virtual window state (no WM/compositor under X11 without one, so state that
// Windows exposes via GetWindowLong etc. is tracked here; EWMH properties are
// still set so real WMs behave the same way).
// ---------------------------------------------------------------------------

struct LinuxWinState
{
	int minmax;          // -1 minimized, 0 normal, 1 maximized.
	DWORD style;         // Virtual style bits (WinSetStyle).
	DWORD exstyle;       // Virtual extended style bits (WinSetExStyle + WS_EX_TOPMOST).
	bool enabled;        // WinSetEnabled.
	bool topmost;        // WinSetAlwaysOnTop.
	int transparent;     // -1 = not set, else 0..255.
	std::wstring trans_color; // WinSetTransColor (no X11 equivalent; stored only).
	int saved_x, saved_y;
	unsigned saved_w, saved_h; // For restore-after-maximize.
	bool has_saved_geom;

	LinuxWinState()
		: minmax(0), style(0), exstyle(0), enabled(true), topmost(false)
		, transparent(-1), has_saved_geom(false) {}
};

static std::map<Window, LinuxWinState> &LinuxWinStates()
{
	static std::map<Window, LinuxWinState> sMap;
	return sMap;
}

static LinuxWinState &LinuxWinStateOf(Window aWin)
{
	auto &m = LinuxWinStates();
	auto it = m.find(aWin);
	if (it == m.end())
		it = m.emplace(aWin, LinuxWinState()).first;
	return it->second;
}

// ---------------------------------------------------------------------------
// Window information helpers
// ---------------------------------------------------------------------------

static std::wstring LinuxNarrowToWide(const char *aUtf8, size_t aLen = (size_t)-1)
{
	std::wstring w;
	if (!aUtf8)
		return w;
	if (aLen == (size_t)-1)
		aLen = strlen(aUtf8);
	size_t i = 0;
	while (i < aLen)
	{
		unsigned char c = (unsigned char)aUtf8[i];
		if (c < 0x80) { w += (wchar_t)c; ++i; }
		else if ((c & 0xE0) == 0xC0 && i + 1 < aLen) { w += (wchar_t)(((c & 0x1F) << 6) | ((unsigned char)aUtf8[i + 1] & 0x3F)); i += 2; }
		else if ((c & 0xF0) == 0xE0 && i + 2 < aLen) { w += (wchar_t)(((c & 0x0F) << 12) | (((unsigned char)aUtf8[i + 1] & 0x3F) << 6) | ((unsigned char)aUtf8[i + 2] & 0x3F)); i += 3; }
		else if ((c & 0xF8) == 0xF0 && i + 3 < aLen) { w += (wchar_t)(((c & 0x07) << 18) | (((unsigned char)aUtf8[i + 1] & 0x3F) << 12) | (((unsigned char)aUtf8[i + 2] & 0x3F) << 6) | ((unsigned char)aUtf8[i + 3] & 0x3F)); i += 4; }
		else { w += L'?'; ++i; }
	}
	return w;
}

static bool LinuxWinExists(Display *d, Window aWin)
{
	XWindowAttributes attrs;
	return XGetWindowAttributes(d, aWin, &attrs) != 0;
}

// Returns true and fills the title ("" if the window has none).
static bool LinuxWinTitle(Display *d, Window aWin, std::wstring &aTitle)
{
	aTitle.clear();
	Atom utf8 = XInternAtom(d, "UTF8_STRING", False);
	Atom net_wm_name = XInternAtom(d, "_NET_WM_NAME", False);
	Atom type = None;
	int fmt = 0;
	unsigned long nitems = 0, after = 0;
	unsigned char *prop = nullptr;
	if (XGetWindowProperty(d, aWin, net_wm_name, 0, 1024, False, utf8
		, &type, &fmt, &nitems, &after, &prop) == Success && prop && nitems)
	{
		aTitle = LinuxNarrowToWide((const char *)prop, nitems);
		XFree(prop);
		return true;
	}
	if (prop)
		XFree(prop);
	char *name = nullptr;
	if (XFetchName(d, aWin, &name) && name)
	{
		aTitle = LinuxNarrowToWide(name);
		XFree(name);
		return true;
	}
	if (name)
		XFree(name);
	return false;
}

static bool LinuxWinClass(Display *d, Window aWin, std::wstring &aClass, std::wstring &aInstance)
{
	aClass.clear();
	aInstance.clear();
	XClassHint hint;
	if (!XGetClassHint(d, aWin, &hint))
		return false;
	if (hint.res_class)
	{
		aClass = LinuxNarrowToWide(hint.res_class);
		XFree(hint.res_class);
	}
	if (hint.res_name)
	{
		aInstance = LinuxNarrowToWide(hint.res_name);
		XFree(hint.res_name);
	}
	return true;
}

static bool LinuxWinPid(Display *d, Window aWin, pid_t &aPid)
{
	aPid = 0;
	Atom net_wm_pid = XInternAtom(d, "_NET_WM_PID", False);
	Atom type = None;
	int fmt = 0;
	unsigned long nitems = 0, after = 0;
	unsigned char *prop = nullptr;
	if (XGetWindowProperty(d, aWin, net_wm_pid, 0, 1, False, XA_CARDINAL
		, &type, &fmt, &nitems, &after, &prop) == Success && prop && nitems)
	{
		aPid = (pid_t)((unsigned long *)prop)[0];
		XFree(prop);
		return true;
	}
	if (prop)
		XFree(prop);
	return false;
}

static bool LinuxWinVisible(Display *d, Window aWin)
{
	XWindowAttributes attrs;
	if (!XGetWindowAttributes(d, aWin, &attrs))
		return false;
	return attrs.map_state == IsViewable;
}

static bool LinuxWinGeom(Display *d, Window aWin, int &aX, int &aY, unsigned &aW, unsigned &aH)
{
	Window root = DefaultRootWindow(d);
	Window child_ret;
	XWindowAttributes attrs;
	if (!XGetWindowAttributes(d, aWin, &attrs))
		return false;
	int root_x = 0, root_y = 0;
	if (!XTranslateCoordinates(d, aWin, root, 0, 0, &root_x, &root_y, &child_ret))
		return false;
	aX = root_x;
	aY = root_y;
	aW = attrs.width;
	aH = attrs.height;
	return true;
}

static bool LinuxWinProcess(pid_t aPid, std::string &aName, std::string &aPath)
{
	if (aPid <= 0)
		return false;
	std::ifstream f((std::string("/proc/") + std::to_string(aPid) + "/comm").c_str());
	if (f)
		std::getline(f, aName);
	char buf[4096];
	ssize_t n = readlink((std::string("/proc/") + std::to_string(aPid) + "/exe").c_str(), buf, sizeof(buf) - 1);
	if (n > 0)
	{
		buf[n] = '\0';
		aPath = buf;
	}
	return !aName.empty() || !aPath.empty();
}

// Top-level windows: direct children of the root window.
static void LinuxEnumTopWindows(Display *d, std::vector<Window> &aOut)
{
	Window root = DefaultRootWindow(d);
	Window root_ret, parent_ret;
	Window *children = nullptr;
	unsigned int n = 0;
	if (XQueryTree(d, root, &root_ret, &parent_ret, &children, &n) && children)
	{
		for (unsigned int i = 0; i < n; ++i)
			aOut.push_back(children[i]);
		XFree(children);
	}
}

// ---------------------------------------------------------------------------
// WinTitle criteria (mirrors WindowSearch::SetCriteria/IsMatch in window.cpp)
// ---------------------------------------------------------------------------

struct LinuxWinCriteria
{
	bool has_title;
	std::wstring title;
	bool has_class;
	std::wstring cls;
	bool has_exe;
	std::wstring exe; // name only unless it contains a slash.
	bool has_pid;
	pid_t pid;
	bool has_id;
	Window id;
	bool has_group;
	std::wstring group;
	std::wstring exclude_title;
	TitleMatchModes mode;
	bool detect_hidden;

	LinuxWinCriteria() : has_title(false), has_class(false), has_exe(false)
		, has_pid(false), has_id(false), has_group(false), id(0), pid(0)
		, mode(FIND_ANYWHERE), detect_hidden(false) {}
};

static bool LinuxIsSpaceTab(wchar_t c)
{
	return c == L' ' || c == L'\t';
}

// Parse "ahk_..." criteria out of aTitle.  Returns false on invalid criteria
// (bad ahk_id, unknown ahk_group).
static bool LinuxParseCriteria(ScriptThreadSettings &aSettings, LPCTSTR aTitle, LPCTSTR aExcludeTitle, LinuxWinCriteria &aOut)
{
	aOut = LinuxWinCriteria();
	aOut.mode = aSettings.TitleMatchMode;
	aOut.detect_hidden = aSettings.DetectHiddenWindows;
	if (aExcludeTitle)
		aOut.exclude_title = aExcludeTitle;

	std::wstring title(aTitle ? aTitle : L"");
	// Split into segments: the first is the title; each "ahk_xxx value" is a criterion.
	std::vector<std::wstring> segments;
	size_t pos = 0;
	while (pos <= title.size())
	{
		// Find the next "ahk_" preceded by start or space/tab.
		size_t next = std::wstring::npos;
		for (size_t i = pos; i + 4 <= title.size(); ++i)
		{
			if ((i == pos || LinuxIsSpaceTab(title[i - 1]))
				&& (title[i] == L'a' || title[i] == L'A')
				&& (title[i + 1] == L'h' || title[i + 1] == L'H')
				&& (title[i + 2] == L'k' || title[i + 2] == L'K')
				&& title[i + 3] == L'_')
			{
				next = i;
				break;
			}
		}
		if (next == std::wstring::npos)
		{
			segments.push_back(title.substr(pos));
			break;
		}
		segments.push_back(title.substr(pos, next - pos));
		pos = next + 4; // Skip past "ahk_"; the value runs until the next "ahk_".
	}

	bool first = true;
	for (size_t s = 0; s < segments.size(); ++s)
	{
		std::wstring seg = segments[s];
		if (first)
		{
			first = false;
			// Leading whitespace is omitted for the title (upstream omit_leading_whitespace).
			size_t b = seg.find_first_not_of(L" \t");
			seg = b == std::wstring::npos ? L"" : seg.substr(b);
			if (!seg.empty())
			{
				// Trailing space before the next criterion is the delimiter; drop it.
				if (s + 1 < segments.size() && !seg.empty() && LinuxIsSpaceTab(seg.back()))
					seg.pop_back();
				if (!seg.empty())
				{
					aOut.has_title = true;
					aOut.title = seg;
				}
			}
			if (s + 1 >= segments.size())
				break;
			continue;
		}
		// This segment starts right after "ahk_" (the keyword begins at 0;
		// the 'ahk_' prefix itself was consumed by the segment splitter).
		std::wstring key;
		size_t value_start = std::wstring::npos;
		for (size_t i = 0; i < seg.size(); ++i)
		{
			if (LinuxIsSpaceTab(seg[i]))
			{
				key = seg.substr(0, i);
				size_t b = seg.find_first_not_of(L" \t", i);
				if (b != std::wstring::npos)
					value_start = b;
				break;
			}
		}
		if (value_start == std::wstring::npos)
			key = seg; // No space: the whole segment is the keyword, value empty.
		std::wstring value = value_start == std::wstring::npos ? L"" : seg.substr(value_start);
		// Drop the delimiting space before the next criterion.
		if (s + 1 < segments.size() && !value.empty() && LinuxIsSpaceTab(value.back()))
			value.pop_back();

		if (key == L"id" || key == L"ID" || key == L"Id")
		{
			aOut.has_id = true;
			aOut.id = (Window)wcstoull(value.c_str(), nullptr, 10);
		}
		else if (key == L"pid" || key == L"PID" || key == L"Pid")
		{
			aOut.has_pid = true;
			aOut.pid = (pid_t)wcstoll(value.c_str(), nullptr, 10);
		}
		else if (key == L"class" || key == L"CLASS" || key == L"Class")
		{
			aOut.has_class = true;
			aOut.cls = value;
		}
		else if (key == L"exe" || key == L"EXE" || key == L"Exe")
		{
			aOut.has_exe = true;
			aOut.exe = value;
		}
		else if (key == L"group" || key == L"GROUP" || key == L"Group")
		{
			aOut.has_group = true;
			aOut.group = value;
		}
		else
		{
			// Unknown ahk_ keyword: treat as part of the title (upstream `continue`s).
			// Upstream actually skips unknown keywords; keep the value as title text.
			aOut.has_title = true;
			aOut.title = seg;
		}
	}
	return true;
}

// Match one window against the criteria (upstream WindowSearch::IsMatch).
static bool LinuxGroupContains(LPCTSTR aName, Window aWin, TitleMatchModes aMode, bool aDetectHidden);

static bool LinuxWinMatches(Display *d, Window aWin, const LinuxWinCriteria &c)
{
	std::wstring title;
	if (c.has_title)
	{
		LinuxWinTitle(d, aWin, title);
		switch (c.mode)
		{
		case FIND_ANYWHERE:
			if (title.find(c.title) == std::wstring::npos)
				return false;
			break;
		case FIND_IN_LEADING_PART:
			if (title.compare(0, c.title.size(), c.title) != 0)
				return false;
			break;
		case FIND_REGEX:
			if (!RegExMatch(title.c_str(), c.title.c_str()))
				return false;
			break;
		default: // Exact.
			if (title != c.title)
				return false;
		}
	}
	if (c.has_class)
	{
		std::wstring cls, inst;
		LinuxWinClass(d, aWin, cls, inst);
		if (c.mode == FIND_REGEX)
		{
			if (!RegExMatch(cls.c_str(), c.cls.c_str()))
				return false;
		}
		else if (cls != c.cls) // Exact match for all other modes.
			return false;
	}
	if (c.has_pid)
	{
		pid_t pid = 0;
		LinuxWinPid(d, aWin, pid);
		if (pid != c.pid)
			return false;
	}
	if (c.has_exe)
	{
		pid_t pid = 0;
		LinuxWinPid(d, aWin, pid);
		std::string name, path;
		LinuxWinProcess(pid, name, path);
		if (c.mode == FIND_REGEX)
		{
			std::wstring full = LinuxNarrowToWide(path.c_str());
			if (!RegExMatch(full.c_str(), c.exe.c_str()))
				return false;
		}
		else
		{
			// Docs/upstream: without a slash, match the process name only.
			std::wstring cmp = c.exe.find(L'/') == std::wstring::npos
				? LinuxNarrowToWide(name.c_str()) : LinuxNarrowToWide(path.c_str());
			// Case-insensitive (_tcsicmp).
			std::wstring a = cmp, b = c.exe;
			std::transform(a.begin(), a.end(), a.begin(), ::towlower);
			std::transform(b.begin(), b.end(), b.begin(), ::towlower);
			if (a != b)
				return false;
		}
	}
	if (c.has_id && aWin != c.id)
		return false;
	if (c.has_group && !LinuxGroupContains(c.group.c_str(), aWin, c.mode, c.detect_hidden))
		return false;
	if (!c.exclude_title.empty())
	{
		LinuxWinTitle(d, aWin, title);
		switch (c.mode)
		{
		case FIND_ANYWHERE:
			if (title.find(c.exclude_title) != std::wstring::npos)
				return false;
			break;
		case FIND_IN_LEADING_PART:
			if (title.compare(0, c.exclude_title.size(), c.exclude_title) == 0)
				return false;
			break;
		case FIND_REGEX:
			if (RegExMatch(title.c_str(), c.exclude_title.c_str()))
				return false;
			break;
		default:
			if (title == c.exclude_title)
				return false;
		}
	}
	return true;
}

// DetectWindow(): visible, or hidden-but-DetectHiddenWindows.
static bool LinuxWindowDetectable(Display *d, Window aWin, bool aDetectHidden)
{
	if (!LinuxWinExists(d, aWin))
		return false;
	if (aDetectHidden)
		return true;
	return LinuxWinVisible(d, aWin);
}

// ---------------------------------------------------------------------------
// Window groups (GroupAdd/GroupActivate/GroupClose/GroupDeactivate; the
// WinGroup class from WinGroup.cpp is not compiled, so a small registry).
// ---------------------------------------------------------------------------

struct LinuxWinGroup
{
	std::wstring name;
	std::list<LinuxWinCriteria> criteria;
	Window last_activated;
	std::vector<Window> visited; // Windows activated since the group stopped being active.
};

static std::map<std::wstring, LinuxWinGroup> &LinuxGroups()
{
	static std::map<std::wstring, LinuxWinGroup> sGroups;
	return sGroups;
}

static bool LinuxInVisited(const std::vector<Window> &aVisited, Window aWin)
{
	return std::find(aVisited.begin(), aVisited.end(), aWin) != aVisited.end();
}

static std::wstring LinuxGroupKey(LPCTSTR aName)
{
	std::wstring k(aName ? aName : L"");
	std::transform(k.begin(), k.end(), k.begin(), ::towlower);
	return k;
}

static LinuxWinGroup *LinuxFindGroup(LPCTSTR aName)
{
	auto &m = LinuxGroups();
	auto it = m.find(LinuxGroupKey(aName));
	return it == m.end() ? nullptr : &it->second;
}

// Is aWin a member of the group (docs: membership is tested when the group
// is activated/used, i.e. live windows matching the stored criteria)?
static bool LinuxGroupContains(LPCTSTR aName, Window aWin, TitleMatchModes aMode, bool aDetectHidden)
{
	LinuxWinGroup *g = LinuxFindGroup(aName);
	if (!g)
		return false;
	Display *d = LinuxWinDisplay();
	if (!d)
		return false;
	for (auto &c : g->criteria)
	{
		// A group is "stale" (never matches) if its criteria were deleted by
		// an empty GroupAdd; keep it simple: re-parse stored title strings.
		if (LinuxWinMatches(d, aWin, c))
			return true;
	}
	return false;
}

// ---------------------------------------------------------------------------
// Search: find first / last matching window, or collect all matches.
// ---------------------------------------------------------------------------

static bool LinuxWinFind(Display *d, const LinuxWinCriteria &c, bool aFindLast
	, Window &aFound, std::vector<Window> *aAll = nullptr)
{
	aFound = 0;
	if (c.has_id)
	{
		if (LinuxWindowDetectable(d, c.id, c.detect_hidden) && LinuxWinMatches(d, c.id, c))
		{
			aFound = c.id;
			if (aAll)
				aAll->push_back(c.id);
			return true;
		}
		return false;
	}
	std::vector<Window> wins;
	LinuxEnumTopWindows(d, wins);
	for (auto w : wins)
	{
		if (!LinuxWindowDetectable(d, w, c.detect_hidden))
			continue;
		if (LinuxWinMatches(d, w, c))
		{
			if (aAll)
				aAll->push_back(w);
			if (!aFindLast)
			{
				aFound = w;
				return true;
			}
			aFound = w;
		}
	}
	return aFound != 0;
}

// Resolve WinTitle/WinText/ExcludeTitle/ExcludeText BIF params into criteria.
// WinText is ignored (X11 has no control text; a non-empty WinText simply
// cannot match, matching upstream behaviour when no control has that text).
static void LinuxResolveCriteria(ExprTokenType *aParam[], int aParamCount
	, ScriptThreadSettings &aSettings, LinuxWinCriteria &aOut, bool &aHasWinText)
{
	aHasWinText = false;
	TCHAR title_buf[4096], exclude_buf[4096], text_buf[4096];
	title_buf[0] = L'\0';
	exclude_buf[0] = L'\0';
	text_buf[0] = L'\0';
	LPTSTR title = _T("");
	LPTSTR exclude = _T("");
	if (aParamCount > 0 && !ParamIndexIsOmitted(0))
		title = TokenToString(*aParam[0], title_buf, nullptr);
	// WinText can never match on X11; a blank WinText is ignored (docs:
	// "If this is blank or omitted, ...").
	if (aParamCount > 1 && !ParamIndexIsOmitted(1))
		if (*TokenToString(*aParam[1], text_buf, nullptr))
			aHasWinText = true;
	if (aParamCount > 2 && !ParamIndexIsOmitted(2))
		exclude = TokenToString(*aParam[2], exclude_buf, nullptr);
	LinuxParseCriteria(aSettings, title, exclude, aOut);
}

// The current "active" window: the top-level ancestor of the input focus.
static Window LinuxActiveWindow(Display *d)
{
	Window focus = 0;
	int revert = 0;
	XGetInputFocus(d, &focus, &revert);
	if (!focus || focus == (Window)PointerRoot || focus == (Window)None)
		return 0;
	Window root = DefaultRootWindow(d);
	Window w = focus;
	// Walk up until we reach a direct child of the root (or the root itself).
	Window top = 0;
	while (w && w != root)
	{
		Window r, p;
		Window *children = nullptr;
		unsigned int n = 0;
		if (!XQueryTree(d, w, &r, &p, &children, &n))
			break;
		if (children)
			XFree(children);
		if (p == root)
		{
			top = w;
			break;
		}
		w = p;
	}
	return top;
}

// Shared accessor for other modules (input coordinates etc.).
Window LinuxX11ActiveWindow()
{
	Display *d = LinuxWinDisplay();
	return d ? LinuxActiveWindow(d) : 0;
}

// ---------------------------------------------------------------------------
// Window actions (EWMH + virtual state)
// ---------------------------------------------------------------------------

static void LinuxWinActivateWin(Display *d, Window aWin)
{
	XRaiseWindow(d, aWin);
	// Try EWMH _NET_ACTIVE_WINDOW first (WM honours it), then direct focus.
	Atom net_active = XInternAtom(d, "_NET_ACTIVE_WINDOW", False);
	XEvent ev;
	memset(&ev, 0, sizeof(ev));
	ev.xclient.type = ClientMessage;
	ev.xclient.window = aWin;
	ev.xclient.message_type = net_active;
	ev.xclient.format = 32;
	ev.xclient.data.l[0] = 2; // Source indication: pager.
	ev.xclient.data.l[1] = CurrentTime;
	XSendEvent(d, DefaultRootWindow(d), False, SubstructureRedirectMask | SubstructureNotifyMask, &ev);
	XSetInputFocus(d, aWin, RevertToParent, CurrentTime);
	XSync(d, False);
}

static void LinuxWinSetStateEwmh(Display *d, Window aWin, const char *aState, bool aAdd)
{
	Atom wm_state = XInternAtom(d, "_NET_WM_STATE", False);
	Atom state = XInternAtom(d, aState, False);
	XEvent ev;
	memset(&ev, 0, sizeof(ev));
	ev.xclient.type = ClientMessage;
	ev.xclient.window = aWin;
	ev.xclient.message_type = wm_state;
	ev.xclient.format = 32;
	ev.xclient.data.l[0] = aAdd ? 1 : 0; // _NET_WM_STATE_ADD / _NET_WM_STATE_REMOVE.
	ev.xclient.data.l[1] = (long)state;
	ev.xclient.data.l[2] = 0;
	XSendEvent(d, DefaultRootWindow(d), False, SubstructureRedirectMask | SubstructureNotifyMask, &ev);
}

static void LinuxWinSetOpacity(Display *d, Window aWin, int aTransparent)
{
	Atom opacity = XInternAtom(d, "_NET_WM_WINDOW_OPACITY", False);
	unsigned long value = (unsigned long)((unsigned long long)aTransparent * 0xFFFFFFFFull / 255ull);
	XChangeProperty(d, aWin, opacity, XA_CARDINAL, 32, PropModeReplace
		, (unsigned char *)&value, 1);
}

static void LinuxWinSetTitleProp(Display *d, Window aWin, const std::wstring &aTitle)
{
	std::string utf8;
	for (wchar_t c : aTitle)
	{
		unsigned int cp = (unsigned int)c;
		if (cp < 0x80) utf8 += (char)cp;
		else if (cp < 0x800) { utf8 += (char)(0xC0 | (cp >> 6)); utf8 += (char)(0x80 | (cp & 0x3F)); }
		else if (cp < 0x10000) { utf8 += (char)(0xE0 | (cp >> 12)); utf8 += (char)(0x80 | ((cp >> 6) & 0x3F)); utf8 += (char)(0x80 | (cp & 0x3F)); }
		else { utf8 += (char)(0xF0 | (cp >> 18)); utf8 += (char)(0x80 | ((cp >> 12) & 0x3F)); utf8 += (char)(0x80 | ((cp >> 6) & 0x3F)); utf8 += (char)(0x80 | (cp & 0x3F)); }
	}
	Atom utf8_atom = XInternAtom(d, "UTF8_STRING", False);
	Atom net_wm_name = XInternAtom(d, "_NET_WM_NAME", False);
	XChangeProperty(d, aWin, net_wm_name, utf8_atom, 8, PropModeReplace
		, (unsigned char *)utf8.c_str(), (int)utf8.size());
	XStoreName(d, aWin, utf8.c_str());
}

// ---------------------------------------------------------------------------
// BIF helpers
// ---------------------------------------------------------------------------

static void LinuxWinSetPersistent(ResultToken &aResultToken, const std::wstring &aStr)
{
	if (aStr.empty())
	{
		aResultToken.SetValue(_T(""));
		return;
	}
	LPTSTR persistent = (LPTSTR)SimpleHeap::Alloc((aStr.size() + 1) * sizeof(TCHAR));
	tmemcpy(persistent, aStr.c_str(), aStr.size() + 1);
	aResultToken.SetValue(persistent, aStr.size());
}

static void LinuxWinSetInt(ResultToken &aResultToken, __int64 aValue)
{
	aResultToken.SetValue(aValue);
}

// Get the target window for WinGet*/WinSet*/etc.: the found window or NULL.
// Throws TargetError when no window matches (docs).
static Window LinuxWinTarget(ResultToken &aResultToken, ExprTokenType *aParam[], int aParamCount
	, ScriptThreadSettings &aSettings, bool aAllowZero = false)
{
	Display *d = LinuxWinDisplay();
	if (!d)
	{
		aResultToken.Error(_T("No X display is available."), _T(""), ErrorPrototype::Target);
		return 0;
	}
	LinuxWinCriteria c;
	bool has_text = false;
	LinuxResolveCriteria(aParam, aParamCount, aSettings, c, has_text);
	if (has_text)
	{
		aResultToken.Error(_T("The specified window could not be found."), _T(""), ErrorPrototype::Target);
		return 0;
	}
	Window found = 0;
	if (LinuxWinFind(d, c, false, found) || (aAllowZero && !c.has_title && !c.has_class && !c.has_exe && !c.has_pid && !c.has_id && !c.has_group))
		return found;
	aResultToken.Error(_T("The specified window could not be found."), _T(""), ErrorPrototype::Target);
	return 0;
}

// ---------------------------------------------------------------------------
// WinExist / WinActive (registered in g_BIF as BIF_WinExistActive)
// ---------------------------------------------------------------------------

BIF_DECL(BIF_WinExistActive)
{
	(void)aParam;
	(void)aParamCount;
	Display *d = LinuxWinDisplay();
	__int64 result = 0;
	bool is_active = (_f_callee_id == FID_WinActive);

	if (!d)
	{
		aResultToken.SetValue(_T(""));
		return;
	}

	TCHAR title_buf[4096], text_buf[4096], exclude_buf[4096];
	title_buf[0] = L'\0';
	text_buf[0] = L'\0';
	exclude_buf[0] = L'\0';
	LPTSTR title = _T("");
	LPTSTR text = _T("");
	LPTSTR exclude = _T("");
	if (aParamCount > 0 && !ParamIndexIsOmitted(0))
		title = TokenToString(*aParam[0], title_buf, nullptr);
	if (aParamCount > 1 && !ParamIndexIsOmitted(1))
		text = TokenToString(*aParam[1], text_buf, nullptr);
	if (aParamCount > 2 && !ParamIndexIsOmitted(2))
		exclude = TokenToString(*aParam[2], exclude_buf, nullptr);

	if (is_active)
	{
		// Docs: WinActive checks whether the active window matches.
		Window active = LinuxActiveWindow(d);
		if (active)
		{
			LinuxWinCriteria c;
			LinuxParseCriteria(*g, title, exclude, c);
			if (*text)
				; // WinText can never match on X11.
			else if (LinuxWindowDetectable(d, active, c.detect_hidden) && LinuxWinMatches(d, active, c))
				result = (__int64)active;
		}
	}
	else
	{
		// WinExist: no criteria -> the last found window (g->hWndLastUsed).
		bool all_blank = !*title && !*text && !*exclude;
		if (all_blank)
		{
			if (g && g->hWndLastUsed && LinuxWindowDetectable(d, (Window)(ULONG_PTR)g->hWndLastUsed, g->DetectHiddenWindows))
				result = (__int64)(ULONG_PTR)g->hWndLastUsed;
		}
		else
		{
			LinuxWinCriteria c;
			LinuxParseCriteria(*g, title, exclude, c);
			if (!*text)
			{
				Window found = 0;
				if (LinuxWinFind(d, c, false, found))
				{
					result = (__int64)found;
					if (g)
						g->hWndLastUsed = (HWND)(ULONG_PTR)found;
				}
			}
		}
	}
	if (result)
		aResultToken.SetValue(result);
	else
		aResultToken.SetValue(_T(""));
}

// ---------------------------------------------------------------------------
// Criteria resolution at arbitrary parameter indices (WinGet* put an output
// first, WinClose puts WaitTime between WinText and ExcludeTitle, etc.).
// ---------------------------------------------------------------------------

static void LinuxResolveCriteriaIdx(ExprTokenType *aParam[], int aParamCount
	, ScriptThreadSettings &aSettings, int aTitleIdx, int aTextIdx, int aExcludeIdx
	, LinuxWinCriteria &aOut, bool &aHasText)
{
	aHasText = false;
	TCHAR title_buf[4096], exclude_buf[4096], text_buf[4096];
	title_buf[0] = L'\0';
	exclude_buf[0] = L'\0';
	text_buf[0] = L'\0';
	LPTSTR title = _T("");
	LPTSTR exclude = _T("");
	if (aTitleIdx >= 0 && aTitleIdx < aParamCount && !ParamIndexIsOmitted(aTitleIdx))
		title = TokenToString(*aParam[aTitleIdx], title_buf, nullptr);
	// WinText can never match on X11; a blank WinText is ignored (docs:
	// "If this is blank or omitted, ...").
	if (aTextIdx >= 0 && aTextIdx < aParamCount && !ParamIndexIsOmitted(aTextIdx))
		if (*TokenToString(*aParam[aTextIdx], text_buf, nullptr))
			aHasText = true;
	if (aExcludeIdx >= 0 && aExcludeIdx < aParamCount && !ParamIndexIsOmitted(aExcludeIdx))
		exclude = TokenToString(*aParam[aExcludeIdx], exclude_buf, nullptr);
	LinuxParseCriteria(aSettings, title, exclude, aOut);
}

// Find the target window for an action (throws TargetError if none; aAllowEmpty
// returns 0 when no criteria are given at all, used by WinExist-like paths).
static Window LinuxWinFindTarget(ResultToken &aResultToken, ExprTokenType *aParam[], int aParamCount
	, ScriptThreadSettings &aSettings, int aTitleIdx, int aTextIdx, int aExcludeIdx)
{
	Display *d = LinuxWinDisplay();
	if (!d)
	{
		aResultToken.Error(_T("No X display is available."), _T(""), ErrorPrototype::Target);
		return 0;
	}
	LinuxWinCriteria c;
	bool has_text = false;
	LinuxResolveCriteriaIdx(aParam, aParamCount, aSettings, aTitleIdx, aTextIdx, aExcludeIdx, c, has_text);
	if (has_text)
	{
		aResultToken.Error(_T("The specified window could not be found."), _T(""), ErrorPrototype::Target);
		return 0;
	}
	Window found = 0;
	if (LinuxWinFind(d, c, false, found))
		return found;
	aResultToken.Error(_T("The specified window could not be found."), _T(""), ErrorPrototype::Target);
	return 0;
}

// WinGet* single-value helpers: find the window (TargetError per docs) and
// call aSink with it.  Criteria are at (aTitleIdx, aTextIdx, aExcludeIdx).
template <typename Fn>
static void LinuxWinGetOne(ResultToken &aResultToken, ExprTokenType *aParam[], int aParamCount
	, ScriptThreadSettings &aSettings, int aTitleIdx, int aTextIdx, int aExcludeIdx, Fn aSink)
{
	Window w = LinuxWinFindTarget(aResultToken, aParam, aParamCount, aSettings, aTitleIdx, aTextIdx, aExcludeIdx);
	if (!w)
		return;
	Display *d = LinuxWinDisplay();
	aSink(d, w);
}

// ---------------------------------------------------------------------------
// WinGetTitle / WinGetClass / WinGetPID / WinGetProcessName / WinGetProcessPath
// ---------------------------------------------------------------------------

BIF_DECL(BIF_Linux_WinGetTitle)
{
	LinuxWinGetOne(aResultToken, aParam, aParamCount, *g, 0, 1, 2,
		[&](Display *d, Window w) {
			std::wstring title;
			LinuxWinTitle(d, w, title);
			LinuxWinSetPersistent(aResultToken, title);
		});
}

BIF_DECL(BIF_Linux_WinGetClass)
{
	LinuxWinGetOne(aResultToken, aParam, aParamCount, *g, 0, 1, 2,
		[&](Display *d, Window w) {
			std::wstring cls, inst;
			LinuxWinClass(d, w, cls, inst);
			LinuxWinSetPersistent(aResultToken, cls);
		});
}

BIF_DECL(BIF_Linux_WinGetPID)
{
	LinuxWinGetOne(aResultToken, aParam, aParamCount, *g, 0, 1, 2,
		[&](Display *d, Window w) {
			pid_t pid = 0;
			LinuxWinPid(d, w, pid);
			aResultToken.SetValue((__int64)pid);
		});
}

BIF_DECL(BIF_Linux_WinGetProcessName)
{
	LinuxWinGetOne(aResultToken, aParam, aParamCount, *g, 0, 1, 2,
		[&](Display *, Window w) {
			pid_t pid = 0;
			if (Display *d2 = LinuxWinDisplay())
				LinuxWinPid(d2, w, pid);
			std::string name, path;
			LinuxWinProcess(pid, name, path);
			LinuxWinSetPersistent(aResultToken, LinuxNarrowToWide(name.c_str()));
		});
}

BIF_DECL(BIF_Linux_WinGetProcessPath)
{
	LinuxWinGetOne(aResultToken, aParam, aParamCount, *g, 0, 1, 2,
		[&](Display *, Window w) {
			pid_t pid = 0;
			if (Display *d2 = LinuxWinDisplay())
				LinuxWinPid(d2, w, pid);
			std::string name, path;
			LinuxWinProcess(pid, name, path);
			LinuxWinSetPersistent(aResultToken, LinuxNarrowToWide(path.c_str()));
		});
}

// ---------------------------------------------------------------------------
// WinGetID / WinGetIDLast / WinGetCount / WinGetList
// ---------------------------------------------------------------------------

static void LinuxWinGetIDImpl(ResultToken &aResultToken, ExprTokenType *aParam[], int aParamCount, bool aLast)
{
	Display *d = LinuxWinDisplay();
	if (!d)
	{
		aResultToken.SetValue(_T(""));
		return;
	}
	LinuxWinCriteria c;
	bool has_text = false;
	LinuxResolveCriteriaIdx(aParam, aParamCount, *g, 0, 1, 2, c, has_text);
	if (has_text)
	{
		aResultToken.SetValue(_T(""));
		return;
	}
	Window found = 0;
	LinuxWinFind(d, c, aLast, found);
	if (!found)
	{
		aResultToken.SetValue(_T(""));
		return;
	}
	if (g)
		g->hWndLastUsed = (HWND)(ULONG_PTR)found;
	aResultToken.SetValue((__int64)found);
}

BIF_DECL(BIF_Linux_WinGetID)
{
	LinuxWinGetIDImpl(aResultToken, aParam, aParamCount, false);
}

BIF_DECL(BIF_Linux_WinGetIDLast)
{
	LinuxWinGetIDImpl(aResultToken, aParam, aParamCount, true);
}

BIF_DECL(BIF_Linux_WinGetCount)
{
	Display *d = LinuxWinDisplay();
	if (!d)
	{
		aResultToken.SetValue((__int64)0);
		return;
	}
	LinuxWinCriteria c;
	bool has_text = false;
	LinuxResolveCriteriaIdx(aParam, aParamCount, *g, 0, 1, 2, c, has_text);
	std::vector<Window> matches;
	if (!has_text)
	{
		// aFindLast=true keeps searching after each match so that ALL matches
		// are collected into aAll.
		Window dummy = 0;
		LinuxWinFind(d, c, true, dummy, &matches);
	}
	aResultToken.SetValue((__int64)matches.size());
}

BIF_DECL(BIF_Linux_WinGetList)
{
	Display *d = LinuxWinDisplay();
	Array *arr = Array::Create();
	if (!d)
	{
		if (arr)
			aResultToken.SetValue(arr);
		else
			aResultToken.SetValue(_T(""));
		return;
	}
	LinuxWinCriteria c;
	bool has_text = false;
	LinuxResolveCriteriaIdx(aParam, aParamCount, *g, 0, 1, 2, c, has_text);
	std::vector<Window> matches;
	if (!has_text)
	{
		Window dummy = 0;
		LinuxWinFind(d, c, true, dummy, &matches);
	}
	for (auto w : matches)
		arr->Append((__int64)w);
	aResultToken.SetValue(arr);
}

// ---------------------------------------------------------------------------
// WinGetPos / WinGetClientPos (output params X,Y,W,H first)
// ---------------------------------------------------------------------------

BIF_DECL(BIF_Linux_WinGetPos)
{
	Window w = LinuxWinFindTarget(aResultToken, aParam, aParamCount, *g, 4, 5, 6);
	if (!w)
		return;
	Display *d = LinuxWinDisplay();
	int x = 0, y = 0;
	unsigned wd = 0, ht = 0;
	if (LinuxWinGeom(d, w, x, y, wd, ht))
	{
		// NOTE: aParam[] only has aParamCount valid entries — never read beyond.
		Var *out;
		if (aParamCount > 0 && (out = TokenToOutputVar(*aParam[0]))) out->Assign((__int64)x);
		if (aParamCount > 1 && (out = TokenToOutputVar(*aParam[1]))) out->Assign((__int64)y);
		if (aParamCount > 2 && (out = TokenToOutputVar(*aParam[2]))) out->Assign((__int64)wd);
		if (aParamCount > 3 && (out = TokenToOutputVar(*aParam[3]))) out->Assign((__int64)ht);
	}
}

BIF_DECL(BIF_Linux_WinGetClientPos)
{
	// Without a window manager there are no decorations, so the client area
	// equals the window area (X11 has no separate client geometry).
	Window w = LinuxWinFindTarget(aResultToken, aParam, aParamCount, *g, 4, 5, 6);
	if (!w)
		return;
	Display *d = LinuxWinDisplay();
	int x = 0, y = 0;
	unsigned wd = 0, ht = 0;
	if (LinuxWinGeom(d, w, x, y, wd, ht))
	{
		Var *out;
		if (aParamCount > 0 && (out = TokenToOutputVar(*aParam[0]))) out->Assign((__int64)x);
		if (aParamCount > 1 && (out = TokenToOutputVar(*aParam[1]))) out->Assign((__int64)y);
		if (aParamCount > 2 && (out = TokenToOutputVar(*aParam[2]))) out->Assign((__int64)wd);
		if (aParamCount > 3 && (out = TokenToOutputVar(*aParam[3]))) out->Assign((__int64)ht);
	}
}

// ---------------------------------------------------------------------------
// WinGetMinMax / WinGetStyle / WinGetExStyle / WinGetText / WinGetTransparent
// / WinGetTransColor / WinGetControls / WinGetControlsHwnd
// ---------------------------------------------------------------------------

BIF_DECL(BIF_Linux_WinGetMinMax)
{
	LinuxWinGetOne(aResultToken, aParam, aParamCount, *g, 0, 1, 2,
		[&](Display *, Window w) {
			aResultToken.SetValue((__int64)LinuxWinStateOf(w).minmax);
		});
}

BIF_DECL(BIF_Linux_WinGetStyle)
{
	LinuxWinGetOne(aResultToken, aParam, aParamCount, *g, 0, 1, 2,
		[&](Display *, Window w) {
			aResultToken.SetValue((__int64)LinuxWinStateOf(w).style);
		});
}

BIF_DECL(BIF_Linux_WinGetExStyle)
{
	LinuxWinGetOne(aResultToken, aParam, aParamCount, *g, 0, 1, 2,
		[&](Display *, Window w) {
			LinuxWinState &s = LinuxWinStateOf(w);
			aResultToken.SetValue((__int64)(s.exstyle | (s.topmost ? 0x8 : 0))); // WS_EX_TOPMOST.
		});
}

BIF_DECL(BIF_Linux_WinGetText)
{
	// X11 has no control text; the docs' behaviour for a window without
	// matching controls is an empty string.
	LinuxWinGetOne(aResultToken, aParam, aParamCount, *g, 0, 1, 2,
		[&](Display *, Window) {
			aResultToken.SetValue(_T(""));
		});
}

BIF_DECL(BIF_Linux_WinGetTransparent)
{
	LinuxWinGetOne(aResultToken, aParam, aParamCount, *g, 0, 1, 2,
		[&](Display *, Window w) {
			int t = LinuxWinStateOf(w).transparent;
			if (t < 0)
				aResultToken.SetValue(_T(""));
			else
				aResultToken.SetValue((__int64)t);
		});
}

BIF_DECL(BIF_Linux_WinGetTransColor)
{
	LinuxWinGetOne(aResultToken, aParam, aParamCount, *g, 0, 1, 2,
		[&](Display *, Window w) {
			LinuxWinSetPersistent(aResultToken, LinuxWinStateOf(w).trans_color);
		});
}

// ---------------------------------------------------------------------------
// WinActivate / WinActivateBottom / WinClose / WinKill / WinMove / WinRedraw
// / WinHide / WinShow / WinMinimize / WinMaximize / WinRestore / WinMoveTop /
// WinMoveBottom
// ---------------------------------------------------------------------------

BIF_DECL(BIF_Linux_WinActivate)
{
	Window w = LinuxWinFindTarget(aResultToken, aParam, aParamCount, *g, 0, 1, 2);
	if (!w)
		return;
	Display *d = LinuxWinDisplay();
	LinuxWinActivateWin(d, w);
}

BIF_DECL(BIF_Linux_WinActivateBottom)
{
	// Docs: same as WinActivate but activates the bottommost matching window.
	Window w = LinuxWinFindTarget(aResultToken, aParam, aParamCount, *g, 0, 1, 2);
	if (!w)
		return;
	Display *d = LinuxWinDisplay();
	XLowerWindow(d, w);
	LinuxWinActivateWin(d, w);
}

// WM_DELETE_WINDOW if supported, otherwise destroy directly.
static void LinuxWinSendClose(Display *d, Window w)
{
	Atom wm_protocols = XInternAtom(d, "WM_PROTOCOLS", False);
	Atom wm_delete = XInternAtom(d, "WM_DELETE_WINDOW", False);
	Atom type = None;
	int fmt = 0;
	unsigned long nitems = 0, after = 0;
	unsigned char *prop = nullptr;
	bool has_delete = false;
	if (XGetWindowProperty(d, w, wm_protocols, 0, 32, False, XA_ATOM
		, &type, &fmt, &nitems, &after, &prop) == Success && prop)
	{
		Atom *atoms = (Atom *)prop;
		for (unsigned long i = 0; i < nitems; ++i)
			if (atoms[i] == wm_delete)
				has_delete = true;
		XFree(prop);
	}
	if (has_delete)
	{
		XEvent ev;
		memset(&ev, 0, sizeof(ev));
		ev.xclient.type = ClientMessage;
		ev.xclient.window = w;
		ev.xclient.message_type = wm_protocols;
		ev.xclient.format = 32;
		ev.xclient.data.l[0] = (long)wm_delete;
		ev.xclient.data.l[1] = CurrentTime;
		XSendEvent(d, w, False, NoEventMask, &ev);
		XFlush(d);
	}
	else
		XDestroyWindow(d, w);
}

BIF_DECL(BIF_Linux_WinClose)
{
	Window w = LinuxWinFindTarget(aResultToken, aParam, aParamCount, *g, 0, 1, 3);
	if (!w)
		return;
	Display *d = LinuxWinDisplay();
	LinuxWinSendClose(d, w);
	// Docs: WaitTime (param 2, seconds) waits for the window to close.
	double wait_time = aParamCount > 2 && !ParamIndexIsOmitted(2) ? TokenToDouble(*aParam[2]) : 0;
	if (wait_time > 0)
	{
		double waited = 0;
		while (waited < wait_time)
		{
			XSync(d, False);
			if (!LinuxWinExists(d, w))
				break;
			ScriptSleep(50);
			waited += 0.05;
		}
	}
	else
		XSync(d, False);
}

BIF_DECL(BIF_Linux_WinKill)
{
	Window w = LinuxWinFindTarget(aResultToken, aParam, aParamCount, *g, 0, 1, 3);
	if (!w)
		return;
	Display *d = LinuxWinDisplay();
	XKillClient(d, w);
	XSync(d, False);
}

BIF_DECL(BIF_Linux_WinMove)
{
	Window w = LinuxWinFindTarget(aResultToken, aParam, aParamCount, *g, 4, 5, 6);
	if (!w)
		return;
	Display *d = LinuxWinDisplay();
	int x = 0, y = 0;
	unsigned wd = 0, ht = 0;
	LinuxWinGeom(d, w, x, y, wd, ht);
	if (aParamCount > 0 && !ParamIndexIsOmitted(0))
		x = (int)TokenToInt64(*aParam[0]);
	if (aParamCount > 1 && !ParamIndexIsOmitted(1))
		y = (int)TokenToInt64(*aParam[1]);
	if (aParamCount > 2 && !ParamIndexIsOmitted(2))
		wd = (unsigned)TokenToInt64(*aParam[2]);
	if (aParamCount > 3 && !ParamIndexIsOmitted(3))
		ht = (unsigned)TokenToInt64(*aParam[3]);
	XMoveResizeWindow(d, w, x, y, wd, ht);
	XSync(d, False);
}

BIF_DECL(BIF_Linux_WinRedraw)
{
	Window w = LinuxWinFindTarget(aResultToken, aParam, aParamCount, *g, 0, 1, 2);
	if (!w)
		return;
	Display *d = LinuxWinDisplay();
	XClearArea(d, w, 0, 0, 0, 0, True);
	XFlush(d);
}

BIF_DECL(BIF_Linux_WinHide)
{
	Window w = LinuxWinFindTarget(aResultToken, aParam, aParamCount, *g, 0, 1, 2);
	if (!w)
		return;
	Display *d = LinuxWinDisplay();
	XUnmapWindow(d, w);
	XSync(d, False);
}

BIF_DECL(BIF_Linux_WinShow)
{
	Window w = LinuxWinFindTarget(aResultToken, aParam, aParamCount, *g, 0, 1, 2);
	if (!w)
		return;
	Display *d = LinuxWinDisplay();
	XMapWindow(d, w);
	XSync(d, False);
}

BIF_DECL(BIF_Linux_WinMinimize)
{
	Window w = LinuxWinFindTarget(aResultToken, aParam, aParamCount, *g, 0, 1, 2);
	if (!w)
		return;
	Display *d = LinuxWinDisplay();
	XIconifyWindow(d, w, DefaultScreen(d));
	XSync(d, False);
	LinuxWinStateOf(w).minmax = -1;
}

BIF_DECL(BIF_Linux_WinMaximize)
{
	Window w = LinuxWinFindTarget(aResultToken, aParam, aParamCount, *g, 0, 1, 2);
	if (!w)
		return;
	Display *d = LinuxWinDisplay();
	LinuxWinState &s = LinuxWinStateOf(w);
	// Save the current geometry so WinRestore can undo the maximize.
	if (!s.has_saved_geom)
	{
		int sx = 0, sy = 0;
		unsigned sw = 0, sh = 0;
		LinuxWinGeom(d, w, sx, sy, sw, sh);
		s.saved_x = sx;
		s.saved_y = sy;
		s.saved_w = sw;
		s.saved_h = sh;
		s.has_saved_geom = true;
	}
	int sx = 0, sy = 0;
	unsigned sw = 0, sh = 0;
	LinuxWinGeom(d, DefaultRootWindow(d), sx, sy, sw, sh);
	XMoveResizeWindow(d, w, 0, 0, sw, sh);
	XSync(d, False);
	s.minmax = 1;
}

BIF_DECL(BIF_Linux_WinRestore)
{
	Window w = LinuxWinFindTarget(aResultToken, aParam, aParamCount, *g, 0, 1, 2);
	if (!w)
		return;
	Display *d = LinuxWinDisplay();
	LinuxWinState &s = LinuxWinStateOf(w);
	if (s.minmax != 0)
	{
		if (s.has_saved_geom)
		{
			XMoveResizeWindow(d, w, s.saved_x, s.saved_y, s.saved_w, s.saved_h);
			XSync(d, False);
		}
		s.minmax = 0;
	}
	// Also un-iconify if a WM is present.
	XMapWindow(d, w);
	XSync(d, False);
}

BIF_DECL(BIF_Linux_WinMoveTop)
{
	Window w = LinuxWinFindTarget(aResultToken, aParam, aParamCount, *g, 0, 1, 2);
	if (!w)
		return;
	Display *d = LinuxWinDisplay();
	XRaiseWindow(d, w);
	XSync(d, False);
}

BIF_DECL(BIF_Linux_WinMoveBottom)
{
	Window w = LinuxWinFindTarget(aResultToken, aParam, aParamCount, *g, 0, 1, 2);
	if (!w)
		return;
	Display *d = LinuxWinDisplay();
	XLowerWindow(d, w);
	XSync(d, False);
}

BIF_DECL(BIF_Linux_WinMinimizeAll)
{
	Display *d = LinuxWinDisplay();
	if (!d)
		return;
	std::vector<Window> wins;
	LinuxEnumTopWindows(d, wins);
	for (auto w : wins)
	{
		if (LinuxWinVisible(d, w))
		{
			XIconifyWindow(d, w, DefaultScreen(d));
			LinuxWinStateOf(w).minmax = -1;
		}
	}
	XSync(d, False);
}

BIF_DECL(BIF_Linux_WinMinimizeAllUndo)
{
	Display *d = LinuxWinDisplay();
	if (!d)
		return;
	std::vector<Window> wins;
	LinuxEnumTopWindows(d, wins);
	for (auto w : wins)
	{
		LinuxWinState &s = LinuxWinStateOf(w);
		if (s.minmax == -1)
		{
			XMapWindow(d, w);
			s.minmax = 0;
		}
	}
	XSync(d, False);
}

// ---------------------------------------------------------------------------
// WinSetTitle / WinSetAlwaysOnTop / WinSetTransparent / WinSetTransColor /
// WinSetEnabled / WinSetStyle / WinSetExStyle
// ---------------------------------------------------------------------------

BIF_DECL(BIF_Linux_WinSetTitle)
{
	Window w = LinuxWinFindTarget(aResultToken, aParam, aParamCount, *g, 1, 2, 3);
	if (!w)
		return;
	TCHAR new_buf[4096];
	LPTSTR new_title = aParamCount > 0 ? TokenToString(*aParam[0], new_buf, nullptr) : nullptr;
	Display *d = LinuxWinDisplay();
	LinuxWinSetTitleProp(d, w, new_title ? new_title : L"");
}

// Value: 0=Off, 1=On, 2=Toggle (docs; omitted defaults to Toggle).
static void LinuxWinSetTopmost(ResultToken &aResultToken, ExprTokenType *aParam[], int aParamCount, int aValueIdx, Window w)
{
	LinuxWinState &s = LinuxWinStateOf(w);
	int value = 2;
	if (aValueIdx < aParamCount && !ParamIndexIsOmitted(aValueIdx))
		value = (int)TokenToInt64(*aParam[aValueIdx]);
	bool on;
	switch (value)
	{
	case 0: on = false; break;
	case 1: on = true; break;
	case 2: on = !s.topmost; break;
	default:
		aResultToken.Error(_T("Invalid value."), _T(""), ErrorPrototype::Value);
		return;
	}
	s.topmost = on;
	Display *d = LinuxWinDisplay();
	LinuxWinSetStateEwmh(d, w, "_NET_WM_STATE_ABOVE", on);
	XSync(d, False);
}

BIF_DECL(BIF_Linux_WinSetAlwaysOnTop)
{
	Window w = LinuxWinFindTarget(aResultToken, aParam, aParamCount, *g, 1, 2, 3);
	if (!w)
		return;
	LinuxWinSetTopmost(aResultToken, aParam, aParamCount, 0, w);
}

BIF_DECL(BIF_Linux_WinSetTransparent)
{
	Window w = LinuxWinFindTarget(aResultToken, aParam, aParamCount, *g, 1, 2, 3);
	if (!w)
		return;
	TCHAR val_buf[64];
	LPTSTR val = aParamCount > 0 ? TokenToString(*aParam[0], val_buf, nullptr) : nullptr;
	LinuxWinState &s = LinuxWinStateOf(w);
	if (!val || !_tcsicmp(val, _T("Off")))
	{
		s.transparent = -1;
		Display *d = LinuxWinDisplay();
		XDeleteProperty(d, w, XInternAtom(d, "_NET_WM_WINDOW_OPACITY", False));
		XSync(d, False);
		return;
	}
	int t = (int)_ttoi(val);
	if (t < 0 || t > 255)
	{
		aResultToken.Error(_T("Invalid value."), _T(""), ErrorPrototype::Value);
		return;
	}
	s.transparent = t;
	Display *d = LinuxWinDisplay();
	LinuxWinSetOpacity(d, w, t);
	XSync(d, False);
}

// Value: "Off" or a color like 0x112233 (stored only; X11 has no
// transparent-colour concept).  Invalid colors throw ValueError.
BIF_DECL(BIF_Linux_WinSetTransColor)
{
	Window w = LinuxWinFindTarget(aResultToken, aParam, aParamCount, *g, 1, 2, 3);
	if (!w)
		return;
	TCHAR val_buf[64];
	LPTSTR val = aParamCount > 0 ? TokenToString(*aParam[0], val_buf, nullptr) : nullptr;
	LinuxWinState &s = LinuxWinStateOf(w);
	if (!val || !_tcsicmp(val, _T("Off")))
	{
		s.trans_color.clear();
		return;
	}
	// Accept 6 hex digits with optional 0x prefix.
	std::wstring v(val);
	size_t b = 0;
	if (v.size() >= 2 && v[0] == L'0' && (v[1] == L'x' || v[1] == L'X'))
		b = 2;
	if (v.size() - b != 6)
	{
		aResultToken.Error(_T("Invalid value."), _T(""), ErrorPrototype::Value);
		return;
	}
	for (size_t i = b; i < v.size(); ++i)
		if (!iswxdigit(v[i]))
		{
			aResultToken.Error(_T("Invalid value."), _T(""), ErrorPrototype::Value);
			return;
		}
	s.trans_color = v;
}

BIF_DECL(BIF_Linux_WinSetEnabled)
{
	Window w = LinuxWinFindTarget(aResultToken, aParam, aParamCount, *g, 1, 2, 3);
	if (!w)
		return;
	LinuxWinStateOf(w).enabled = aParamCount > 0 && !ParamIndexIsOmitted(0) && TokenToBOOL(*aParam[0]);
}

// Style string: "+0x800000" / "-0x400000" / "0x123" (add/remove/replace bits).
static void LinuxWinSetStyleBits(ResultToken &aResultToken, ExprTokenType *aParam[], int aParamCount, int aValueIdx, Window w, bool aExStyle)
{
	TCHAR val_buf[64];
	LPTSTR val = aValueIdx < aParamCount ? TokenToString(*aParam[aValueIdx], val_buf, nullptr) : nullptr;
	LinuxWinState &s = LinuxWinStateOf(w);
	DWORD &bits = aExStyle ? s.exstyle : s.style;
	DWORD mask = (DWORD)wcstoul((val ? val : L"0") + (val && (*val == L'+' || *val == L'-') ? 1 : 0), nullptr, 0);
	if (val && *val == L'+')
		bits |= mask;
	else if (val && *val == L'-')
		bits &= ~mask;
	else
		bits = mask;
}

BIF_DECL(BIF_Linux_WinSetStyle)
{
	Window w = LinuxWinFindTarget(aResultToken, aParam, aParamCount, *g, 1, 2, 3);
	if (!w)
		return;
	LinuxWinSetStyleBits(aResultToken, aParam, aParamCount, 0, w, false);
}

BIF_DECL(BIF_Linux_WinSetExStyle)
{
	Window w = LinuxWinFindTarget(aResultToken, aParam, aParamCount, *g, 1, 2, 3);
	if (!w)
		return;
	LinuxWinSetStyleBits(aResultToken, aParam, aParamCount, 0, w, true);
}

// ---------------------------------------------------------------------------
// WinWait / WinWaitActive / WinWaitNotActive / WinWaitClose
// (docs: return HWND or 0 / 1 or 0; Timeout omitted = wait indefinitely)
// ---------------------------------------------------------------------------

static bool LinuxWinWaitLoop(ResultToken &aResultToken, ExprTokenType *aParam[], int aParamCount
	, int aCondition) // 0=Wait, 1=WaitActive, 2=WaitNotActive, 3=WaitClose
{
	Display *d = LinuxWinDisplay();
	if (!d)
	{
		aResultToken.Error(_T("No X display is available."), _T(""), ErrorPrototype::Target);
		return false;
	}
	LinuxWinCriteria c;
	bool has_text = false;
	LinuxResolveCriteriaIdx(aParam, aParamCount, *g, 0, 1, 3, c, has_text);
	double timeout = -1;
	if (aParamCount > 2 && !ParamIndexIsOmitted(2))
		timeout = TokenToDouble(*aParam[2]);
	double waited = 0;
	for (;;)
	{
		bool active_matches = false;
		if (aCondition == 1 || aCondition == 2)
		{
			Window active = LinuxActiveWindow(d);
			active_matches = active && LinuxWindowDetectable(d, active, c.detect_hidden) && LinuxWinMatches(d, active, c);
		}
		bool found = false;
		Window w = 0;
		switch (aCondition)
		{
		case 0: found = LinuxWinFind(d, c, false, w); break;
		case 1: found = active_matches; w = LinuxActiveWindow(d); break;
		case 2: found = !active_matches; break;
		case 3: found = !LinuxWinFind(d, c, false, w); break;
		}
		if (found)
		{
			if (aCondition == 0 || aCondition == 1)
				aResultToken.SetValue((__int64)w);
			else
				aResultToken.SetValue((__int64)1);
			return true;
		}
		if (timeout >= 0 && waited >= timeout)
		{
			aResultToken.SetValue((__int64)0);
			return false;
		}
		ScriptSleep(50);
		waited += 0.05;
	}
}

BIF_DECL(BIF_Linux_WinWait)          { LinuxWinWaitLoop(aResultToken, aParam, aParamCount, 0); }
BIF_DECL(BIF_Linux_WinWaitActive)    { LinuxWinWaitLoop(aResultToken, aParam, aParamCount, 1); }
BIF_DECL(BIF_Linux_WinWaitNotActive) { LinuxWinWaitLoop(aResultToken, aParam, aParamCount, 2); }
BIF_DECL(BIF_Linux_WinWaitClose)     { LinuxWinWaitLoop(aResultToken, aParam, aParamCount, 3); }

// ---------------------------------------------------------------------------
// GroupAdd / GroupActivate / GroupClose / GroupDeactivate
// ---------------------------------------------------------------------------

BIF_DECL(BIF_Linux_GroupAdd)
{
	TCHAR name_buf[256];
	LPTSTR name = TokenToString(*aParam[0], name_buf, nullptr);
	LinuxWinGroup &group = LinuxGroups()[LinuxGroupKey(name)];
	group.name = name;
	// Docs: a group whose latest entry is empty never matches (removes all
	// previous criteria).
	bool all_blank = true;
	for (int i = 1; i < aParamCount; ++i)
		if (!ParamIndexIsOmitted(i))
		{
			TCHAR b[4096];
			LPTSTR s = TokenToString(*aParam[i], b, nullptr);
			if (s && *s)
			{
				all_blank = false;
				break;
			}
		}
	if (all_blank)
		group.criteria.clear();
	else
	{
		LinuxWinCriteria c;
		bool has_text = false;
		LinuxResolveCriteriaIdx(aParam, aParamCount, *g, 1, 2, 3, c, has_text);
		group.criteria.push_back(c);
	}
}

// Collect the live windows of a group, in top-to-bottom stacking order.
static std::vector<Window> LinuxGroupWindows(Display *d, LinuxWinGroup &aGroup, bool aDetectHidden)
{
	std::vector<Window> wins;
	LinuxEnumTopWindows(d, wins);
	std::vector<Window> members;
	for (auto w : wins)
	{
		if (!LinuxWindowDetectable(d, w, aDetectHidden))
			continue;
		for (auto &c : aGroup.criteria)
			if (LinuxWinMatches(d, w, c))
			{
				members.push_back(w);
				break;
			}
	}
	return members;
}

BIF_DECL(BIF_Linux_GroupActivate)
{
	Display *d = LinuxWinDisplay();
	if (!d)
	{
		aResultToken.SetValue(_T(""));
		return;
	}
	TCHAR name_buf[256];
	LPTSTR name = TokenToString(*aParam[0], name_buf, nullptr);
	LinuxWinGroup *group = LinuxFindGroup(name);
	if (!group)
	{
		aResultToken.SetValue(_T(""));
		return;
	}
	bool reset = false;
	if (aParamCount > 1 && !ParamIndexIsOmitted(1))
	{
		TCHAR m_buf[16];
		LPTSTR m = TokenToString(*aParam[1], m_buf, nullptr);
		reset = m && !_tcsicmp(m, _T("R"));
	}
	std::vector<Window> members = LinuxGroupWindows(d, *group, g->DetectHiddenWindows);
	if (members.empty())
	{
		aResultToken.SetValue(_T(""));
		return;
	}
	Window active = LinuxActiveWindow(d);
	bool group_active = active && std::find(members.begin(), members.end(), active) != members.end();
	// Docs: the "R" (reset) mode starts again from the topmost window.
	if (!group_active || reset)
		group->visited.clear();
	if (group_active && !LinuxInVisited(group->visited, active))
		group->visited.push_back(active); // The active window has been reviewed.
	// Find the first (bottommost, i.e. earliest in the stacking order) member
	// that has not been visited yet (mirrors WinGroup::Activate).
	Window to_activate = 0;
	for (auto w : members)
	{
		if (!LinuxInVisited(group->visited, w))
		{
			to_activate = w;
			break;
		}
	}
	if (!to_activate)
	{
		// All members visited: wrap around and start over (upstream resets the
		// visited list and retries).
		group->visited.clear();
		if (!members.empty())
			to_activate = members.front();
	}
	if (!to_activate)
	{
		aResultToken.SetValue(_T(""));
		return;
	}
	group->last_activated = to_activate;
	group->visited.push_back(to_activate);
	LinuxWinActivateWin(d, to_activate);
	aResultToken.SetValue((__int64)to_activate);
}

BIF_DECL(BIF_Linux_GroupClose)
{
	Display *d = LinuxWinDisplay();
	if (!d)
		return;
	TCHAR name_buf[256];
	LPTSTR name = TokenToString(*aParam[0], name_buf, nullptr);
	LinuxWinGroup *group = LinuxFindGroup(name);
	if (!group)
		return;
	bool all = false;
	if (aParamCount > 1 && !ParamIndexIsOmitted(1))
	{
		TCHAR m_buf[16];
		LPTSTR m = TokenToString(*aParam[1], m_buf, nullptr);
		all = m && !_tcsicmp(m, _T("A"));
	}
	std::vector<Window> members = LinuxGroupWindows(d, *group, g->DetectHiddenWindows);
	if (all)
	{
		for (auto w : members)
			LinuxWinSendClose(d, w);
		XSync(d, False);
		return;
	}
	Window active = LinuxActiveWindow(d);
	if (active && std::find(members.begin(), members.end(), active) != members.end())
	{
		LinuxWinSendClose(d, active);
		XSync(d, False);
	}
}

BIF_DECL(BIF_Linux_GroupDeactivate)
{
	Display *d = LinuxWinDisplay();
	if (!d)
		return;
	TCHAR name_buf[256];
	LPTSTR name = TokenToString(*aParam[0], name_buf, nullptr);
	LinuxWinGroup *group = LinuxFindGroup(name);
	if (!group)
		return;
	bool reset = false;
	if (aParamCount > 1 && !ParamIndexIsOmitted(1))
	{
		TCHAR m_buf[16];
		LPTSTR m = TokenToString(*aParam[1], m_buf, nullptr);
		reset = m && !_tcsicmp(m, _T("R"));
	}
	(void)reset;
	std::vector<Window> members = LinuxGroupWindows(d, *group, g->DetectHiddenWindows);
	Window active = LinuxActiveWindow(d);
	for (auto w : members)
	{
		if (w == active)
			continue; // Docs: minimize all members except the active window.
		XIconifyWindow(d, w, DefaultScreen(d));
		LinuxWinStateOf(w).minmax = -1;
	}
	XSync(d, False);
}


// ---------------------------------------------------------------------------
// Accessors for the control module (core_ctrl_linux.cpp).  These must come
// after the static helpers they wrap.
// ---------------------------------------------------------------------------

Display *LinuxX11Display()
{
	return LinuxWinDisplay();
}

Window LinuxWinFindTargetEx(ResultToken &aResultToken, ExprTokenType *aParam[], int aParamCount
	, ScriptThreadSettings &aSettings, int aTitleIdx, int aTextIdx, int aExcludeIdx)
{
	return LinuxWinFindTarget(aResultToken, aParam, aParamCount, aSettings, aTitleIdx, aTextIdx, aExcludeIdx);
}

bool LinuxWinTitleEx(Display *d, Window aWin, std::wstring &aTitle)
{
	return LinuxWinTitle(d, aWin, aTitle);
}

void LinuxWinSetPersistentEx(ResultToken &aResultToken, const std::wstring &aStr)
{
	LinuxWinSetPersistent(aResultToken, aStr);
}

// ---------------------------------------------------------------------------
// WinSetRegion (X11 SHAPE extension)
// ---------------------------------------------------------------------------
//
// Docs: "Changes the shape of the specified window to be the specified
// rectangle, ellipse, or polygon."  The X11 equivalent of a Win32 region is
// the SHAPE extension's bounding shape; the upstream option grammar
// (lib/win.cpp WinSetRegion) is mirrored exactly:
//   - blank/omitted Options restore the window's normal shape;
//   - "X-Y" coordinate pairs (separated by '-') are points; the first pair
//     is the origin for the rectangle/ellipse/rounded-rectangle forms;
//   - 'Wn' width, 'Hn' height, 'E' ellipse, 'R'/'Rw-h' rounded rectangle
//     (default 30x30), 'Wind' winding polygon fill (accepted; self-
//     intersecting polygons are filled with the even-odd rule, documented);
//   - unknown letters, missing '-' delimiters or a missing origin yield a
//     ValueError (upstream FR_E_ARG(0));
//   - TargetError when the window cannot be found; OSError when the SHAPE
//     extension is unavailable.
// The region is converted to a list of scan-line rectangles and applied
// with XShapeCombineRectangles (ShapeBounding, ShapeSet).

#define REGION_DELIMITER L'-'

static void LinuxRegionAddRect(std::vector<XRectangle> &aRects, int aX, int aY, int aW, int aH)
{
	if (aW <= 0 || aH <= 0)
		return;
	XRectangle r;
	r.x = aX;
	r.y = aY;
	r.width = (unsigned)aW;
	r.height = (unsigned)aH;
	aRects.push_back(r);
}

// Scan-lines for an ellipse spanning [aX, aX+aW) x [aY, aY+aH).
static void LinuxRegionEllipse(std::vector<XRectangle> &aRects, int aX, int aY, int aW, int aH)
{
	double cx = aX + aW / 2.0;
	double cy = aY + aH / 2.0;
	double rx = aW / 2.0;
	double ry = aH / 2.0;
	for (int yy = aY; yy < aY + aH; ++yy)
	{
		double t = (yy + 0.5 - cy) / ry;
		if (t < -1.0 || t > 1.0)
			continue;
		double half = rx * sqrt(1.0 - t * t);
		int x0 = (int)floor(cx - half + 0.5);
		int x1 = (int)floor(cx + half + 0.5);
		LinuxRegionAddRect(aRects, x0, yy, x1 - x0, 1);
	}
}

// Scan-lines for a rounded rectangle spanning [aX, aX+aW) x [aY, aY+aH)
// with corner radii rr_w x rr_h.
static void LinuxRegionRoundRect(std::vector<XRectangle> &aRects, int aX, int aY, int aW, int aH
	, int aRrW, int aRrH)
{
	if (aRrW > aW / 2)
		aRrW = aW / 2;
	if (aRrH > aH / 2)
		aRrH = aH / 2;
	for (int yy = aY; yy < aY + aH; ++yy)
	{
		int local = yy - aY;
		int from_top = local;
		int from_bottom = aH - 1 - local;
		int d = from_top < from_bottom ? from_top : from_bottom;
		int inset = 0;
		if (d < aRrH && aRrH > 0)
		{
			double ratio = 1.0 - (double)d / aRrH;
			inset = (int)floor(aRrW * (1.0 - sqrt(1.0 - ratio * ratio)) + 0.5);
		}
		LinuxRegionAddRect(aRects, aX + inset, yy, aW - 2 * inset, 1);
	}
}

// Even-odd scan-line fill of a polygon.
static void LinuxRegionPolygon(std::vector<XRectangle> &aRects, const std::vector<POINT> &aPts)
{
	int min_y = aPts[0].y, max_y = aPts[0].y;
	for (size_t i = 1; i < aPts.size(); ++i)
	{
		if (aPts[i].y < min_y)
			min_y = aPts[i].y;
		if (aPts[i].y > max_y)
			max_y = aPts[i].y;
	}
	for (int yy = min_y; yy <= max_y; ++yy)
	{
		std::vector<double> xs;
		size_t n = aPts.size();
		for (size_t i = 0; i < n; ++i)
		{
			const POINT &p1 = aPts[i];
			const POINT &p2 = aPts[(i + 1) % n];
			if ((p1.y <= yy && yy < p2.y) || (p2.y <= yy && yy < p1.y))
			{
				double t = (double)(yy - p1.y) / (p2.y - p1.y);
				xs.push_back(p1.x + t * (p2.x - p1.x));
			}
		}
		std::sort(xs.begin(), xs.end());
		for (size_t i = 0; i + 1 < xs.size(); i += 2)
		{
			int x0 = (int)floor(xs[i] + 0.5);
			int x1 = (int)floor(xs[i + 1] + 0.5);
			LinuxRegionAddRect(aRects, x0, yy, x1 - x0, 1);
		}
	}
}

BIF_DECL(BIF_Linux_WinSetRegion)
{
	Window w = LinuxWinFindTargetEx(aResultToken, aParam, aParamCount, *g, 1, 2, 3);
	if (!w)
		return; // TargetError already raised.
	Display *d = LinuxWinDisplay();
	// Blank/omitted Options: restore the window's normal shape (docs).
	if (aParamCount == 0 || ParamIndexIsOmitted(0))
	{
		XShapeCombineMask(d, w, ShapeBounding, 0, 0, None, ShapeSet);
		XFlush(d);
		return;
	}
	int shape_event = 0, shape_error = 0;
	if (!XShapeQueryExtension(d, &shape_event, &shape_error))
	{
		aResultToken.Error(_T("The X SHAPE extension is not available."), _T(""), ErrorPrototype::OS);
		return;
	}
	TCHAR opt_buf[4096];
	LPTSTR opts = TokenToString(*aParam[0], opt_buf, nullptr);
	// Option grammar (upstream WinSetRegion): space-separated tokens.
	std::vector<POINT> pts;
	int width = COORD_UNSPECIFIED, height = COORD_UNSPECIFIED;
	int rr_width = COORD_UNSPECIFIED, rr_height = COORD_UNSPECIFIED;
	bool use_ellipse = false;
	const wchar_t *cp = opts ? opts : L"";
	for (;;)
	{
		cp = omit_leading_whitespace(cp);
		if (!*cp)
			break;
		if (cisdigit(*cp) || *cp == L'-' || *cp == L'+')
		{
			POINT pt;
			pt.x = ATOI(cp);
			const wchar_t *delim = _tcschr(cp + 1, REGION_DELIMITER);
			if (!delim)
			{
				FResultToError(aResultToken, aParam, aParamCount, FR_E_ARG(0), 0);
				return;
			}
			pt.y = ATOI(delim + 1);
			pts.push_back(pt);
			cp = delim + 1;
		}
		else
		{
			wchar_t c = towupper(*cp);
			switch (c)
			{
			case L'E':
				use_ellipse = true;
				++cp;
				break;
			case L'R':
				if (!cp[1] || cp[1] == L' ')
				{
					rr_width = 30;
					rr_height = 30;
					++cp;
				}
				else
				{
					rr_width = ATOI(cp + 1);
					const wchar_t *delim = _tcschr(cp + 1, REGION_DELIMITER);
					if (!delim)
					{
						FResultToError(aResultToken, aParam, aParamCount, FR_E_ARG(0), 0);
						return;
					}
					rr_height = ATOI(delim + 1);
					cp = delim + 1;
				}
				break;
			case L'W':
				if (!_tcsnicmp(cp, L"Wind", 4))
					cp += 4; // Winding fill accepted; even-odd applied (documented).
				else
				{
					width = ATOI(cp + 1);
					++cp;
					while (cisdigit(*cp))
						++cp;
				}
				break;
			case L'H':
				height = ATOI(cp + 1);
				++cp;
				while (cisdigit(*cp))
					++cp;
				break;
			default:
				FResultToError(aResultToken, aParam, aParamCount, FR_E_ARG(0), 0);
				return;
			}
		}
		if (!(cp = _tcschr(cp, L' ')))
			break;
	}
	if (pts.empty())
	{
		FResultToError(aResultToken, aParam, aParamCount, FR_E_ARG(0), 0);
		return;
	}
	bool both = !(width == COORD_UNSPECIFIED || height == COORD_UNSPECIFIED);
	if (both)
	{
		width += pts[0].x;
		height += pts[0].y;
	}
	std::vector<XRectangle> rects;
	if (use_ellipse)
	{
		if (!both)
		{
			// Upstream: CreateEllipticRgn with no width/height -> NULL -> FR_E_WIN32.
			aResultToken.Error(_T("An ellipse region requires W and H options."), _T(""), ErrorPrototype::OS);
			return;
		}
		LinuxRegionEllipse(rects, pts[0].x, pts[0].y, width - pts[0].x, height - pts[0].y);
	}
	else if (rr_width != COORD_UNSPECIFIED)
	{
		if (!both)
		{
			// Upstream: CreateRoundRectRgn with no width/height -> NULL -> FR_E_WIN32.
			aResultToken.Error(_T("A rounded rectangle region requires W and H options."), _T(""), ErrorPrototype::OS);
			return;
		}
		LinuxRegionRoundRect(rects, pts[0].x, pts[0].y, width - pts[0].x, height - pts[0].y
			, rr_width, rr_height);
	}
	else if (both)
		LinuxRegionAddRect(rects, pts[0].x, pts[0].y, width - pts[0].x, height - pts[0].y);
	else
		LinuxRegionPolygon(rects, pts);
	XShapeCombineRectangles(d, w, ShapeBounding, 0, 0, rects.data(), (int)rects.size()
		, ShapeSet, Unsorted);
	XFlush(d);
}

#undef REGION_DELIMITER
