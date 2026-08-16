// Linux X11 misc module (round 10): StatusBarGetText/StatusBarWait,
// ListVars/ListHotkeys/KeyHistory, FileCreateShortcut/FileGetShortcut.
//
// Semantics follow docs-v2:
//   - StatusBarGetText/StatusBarWait operate on the first descendant of the
//     target window whose class contains "statusbar" (docs: the standard
//     msctls_statusbar32 common control).  The bar's text is its X11 name;
//     parts are not representable on X11, so Part 1 returns the whole text
//     and higher parts return "".  StatusBarWait matches per TitleMatchMode
//     (case-sensitive), polling at Interval (default 50 ms), returning 1/0.
//   - ListVars/ListHotkeys/KeyHistory are debug displays: upstream opens a
//     window (ShowMainWindow); on Linux the port shows the same information
//     through MsgBox (headless: printed to stdout).  KeyHistory(MaxEvents)
//     validates 0..500 like upstream and stores the requested maximum.
//   - FileCreateShortcut creates the documented URL shortcut (.url -> INI
//     with [InternetShortcut]/URL=) or, for any other extension, a
//     freedesktop .desktop launcher (the Linux equivalent of .lnk: Exec/
//     Path/Icon/Comment/Name).  FileGetShortcut parses both formats back.
#include "../../stdafx.h"
#include "../../script.h"
#include "../../globaldata.h"
#include "../../script_func_impl.h"
#include "core_display_linux.h"
#include "core_win_linux.h"
#include "core_ctrl_linux.h"
#include <X11/Xlib.h>
#include <cstdlib>
#include <cstring>
#include <cstdio>
#include <cwctype>
#include <string>
#include <vector>
#include <fstream>
#include <sstream>
#include <algorithm>

void ScriptSleep(int aDelay);

// ---------------------------------------------------------------------------
// Narrow/wide conversion helpers (UTF-8), local to this module.
// ---------------------------------------------------------------------------

static std::wstring LinuxNarrowToWideEx(const char *aUtf8)
{
	std::wstring w;
	if (!aUtf8)
		return w;
	size_t len = strlen(aUtf8);
	size_t i = 0;
	while (i < len)
	{
		unsigned char c = (unsigned char)aUtf8[i];
		if (c < 0x80) { w += (wchar_t)c; ++i; }
		else if ((c & 0xE0) == 0xC0 && i + 1 < len) { w += (wchar_t)(((c & 0x1F) << 6) | ((unsigned char)aUtf8[i + 1] & 0x3F)); i += 2; }
		else if ((c & 0xF0) == 0xE0 && i + 2 < len) { w += (wchar_t)(((c & 0x0F) << 12) | (((unsigned char)aUtf8[i + 1] & 0x3F) << 6) | ((unsigned char)aUtf8[i + 2] & 0x3F)); i += 3; }
		else if ((c & 0xF8) == 0xF0 && i + 3 < len) { w += (wchar_t)(((c & 0x07) << 18) | (((unsigned char)aUtf8[i + 1] & 0x3F) << 12) | (((unsigned char)aUtf8[i + 2] & 0x3F) << 6) | ((unsigned char)aUtf8[i + 3] & 0x3F)); i += 4; }
		else { w += L'?'; ++i; }
	}
	return w;
}

static std::string LinuxWideToNarrowEx(const wchar_t *aWide)
{
	std::string s;
	if (!aWide)
		return s;
	for (const wchar_t *p = aWide; *p; ++p)
	{
		unsigned int c = (unsigned int)*p;
		if (c < 0x80) s += (char)c;
		else if (c < 0x800) { s += (char)(0xC0 | (c >> 6)); s += (char)(0x80 | (c & 0x3F)); }
		else if (c < 0x10000) { s += (char)(0xE0 | (c >> 12)); s += (char)(0x80 | ((c >> 6) & 0x3F)); s += (char)(0x80 | (c & 0x3F)); }
		else { s += (char)(0xF0 | (c >> 18)); s += (char)(0x80 | ((c >> 12) & 0x3F)); s += (char)(0x80 | ((c >> 6) & 0x3F)); s += (char)(0x80 | (c & 0x3F)); }
	}
	return s;
}

// Case-insensitive suffix check (replaces _stricmp on the extension).
static bool LinuxEndsWithI(const std::string &aStr, const char *aSuffix)
{
	size_t sl = strlen(aSuffix);
	if (aStr.size() < sl)
		return false;
	for (size_t i = 0; i < sl; ++i)
		if (tolower((unsigned char)aStr[aStr.size() - sl + i]) != tolower((unsigned char)aSuffix[i]))
			return false;
	return true;
}

// ---------------------------------------------------------------------------
// StatusBarGetText / StatusBarWait
// ---------------------------------------------------------------------------

// Text of the first standard status bar in the target window, or false.
static bool LinuxStatusBarText(ResultToken &aResultToken, ExprTokenType *aParam[], int aParamCount
	, int aTitleIdx, Window &aBar, std::wstring &aText)
{
	Display *d = LinuxX11Display();
	if (!d)
	{
		aResultToken.Error(_T("No X display is available."), _T(""), ErrorPrototype::Target);
		return false;
	}
	Window target = LinuxWinFindTargetEx(aResultToken, aParam, aParamCount, *g
		, aTitleIdx, aTitleIdx + 1, aTitleIdx + 2);
	if (!target)
		return false;
	aBar = LinuxFindDescendantByClass(d, target, L"statusbar");
	if (!aBar)
	{
		aResultToken.Error(_T("The target window does not contain a standard status bar."), _T(""), ErrorPrototype::Target);
		return false;
	}
	LinuxWinTitleEx(d, aBar, aText);
	return true;
}

BIF_DECL(BIF_Linux_StatusBarGetText)
{
	// Part#, WinTitle, WinText, ExcludeTitle, ExcludeText.
	Window bar = 0;
	std::wstring text;
	if (!LinuxStatusBarText(aResultToken, aParam, aParamCount, 1, bar, text))
		return;
	int part = aParamCount > 0 && !ParamIndexIsOmitted(0) ? (int)TokenToInt64(*aParam[0]) : 1;
	// X11 status bars have a single part: Part 1 = the whole text.
	LinuxWinSetPersistentEx(aResultToken, part <= 1 ? text : std::wstring());
}

BIF_DECL(BIF_Linux_StatusBarWait)
{
	// Text?, Timeout?, Part#, WinTitle, WinText, Interval?, ExcludeTitle,
	// ExcludeText.
	Display *d = LinuxX11Display();
	if (!d)
	{
		aResultToken.Error(_T("No X display is available."), _T(""), ErrorPrototype::Target);
		return;
	}
	Window target = LinuxWinFindTargetEx(aResultToken, aParam, aParamCount, *g, 3, 4, 6);
	if (!target)
		return;
	Window bar = LinuxFindDescendantByClass(d, target, L"statusbar");
	if (!bar)
	{
		aResultToken.Error(_T("The target window does not contain a standard status bar."), _T(""), ErrorPrototype::Target);
		return;
	}
	TCHAR want_buf[4096];
	want_buf[0] = L'\0';
	LPTSTR want = aParamCount > 0 && !ParamIndexIsOmitted(0) ? TokenToString(*aParam[0], want_buf, nullptr) : nullptr;
	if (!want)
		want = want_buf;
	double timeout = -1;
	if (aParamCount > 1 && !ParamIndexIsOmitted(1))
		timeout = TokenToDouble(*aParam[1]);
	int interval = 50;
	if (aParamCount > 5 && !ParamIndexIsOmitted(5))
		interval = (int)TokenToInt64(*aParam[5]);
	if (interval < 10)
		interval = 10;
	double waited = 0;
	for (;;)
	{
		std::wstring text;
		LinuxWinTitleEx(d, bar, text);
		bool match;
		if (!*want)
			match = text.empty(); // Docs: wait for the bar to become blank.
		else
			match = LinuxWinTitleMatches(*g, text.c_str(), want);
		if (match)
		{
			aResultToken.SetValue((__int64)1);
			return;
		}
		if (timeout >= 0 && waited >= timeout)
		{
			aResultToken.SetValue((__int64)0);
			return;
		}
		ScriptSleep(interval);
		waited += interval / 1000.0;
	}
}

// ---------------------------------------------------------------------------
// ListVars / ListHotkeys / KeyHistory (debug displays via MsgBox)
// ---------------------------------------------------------------------------

// Title matching per SetTitleMatchMode (shared with the window module).
bool LinuxWinTitleMatches(ScriptThreadSettings &aSettings, const wchar_t *aTitle, const wchar_t *aNeedle)
{
	if (!aTitle || !aNeedle)
		return false;
	std::wstring text(aTitle), pat(aNeedle);
	switch (aSettings.TitleMatchMode)
	{
	case 1:
		return text.size() >= pat.size() && wcsncmp(text.c_str(), pat.c_str(), pat.size()) == 0;
	case 2:
		return text.find(pat) != std::wstring::npos;
	case 3:
		return text == pat;
	default: // RegEx.
		return RegExMatch(aTitle, aNeedle) != nullptr;
	}
}

// Friend of Script (see script.h) so ListVars can enumerate global vars.
class LinuxVarDump
{
public:
	static void Dump(std::wstring &aOut)
	{
		VarList &vars = g_script.mVars;
		for (int i = 0; i < vars.mCount; ++i)
		{
			Var *var = vars.mItem[i];
			if (!var)
				continue;
			ResultToken token;
			// Normal vars use ToToken; virtual vars (e.g. A_*) use Get --
			// mirror Debugger.cpp's GetPropertyValue.
			if (var->IsUninitializedNormalVar())
				token.symbol = SYM_MISSING;
			else if (var->Type() == VAR_VIRTUAL)
			{
				token.InitResult(token.buf);
				token.symbol = SYM_INTEGER;
				var->Get(token);
			}
			else
				var->ToToken(token);
			aOut += var->mName;
			aOut += L": ";
			if (token.symbol == SYM_STRING)
				aOut += token.marker_length > 0 ? std::wstring(token.marker, token.marker_length) : std::wstring();
			else if (token.symbol == SYM_INTEGER || token.symbol == SYM_FLOAT)
			{
				TCHAR num_buf[64];
				aOut += TokenToString(token, num_buf, nullptr);
			}
			else
				aOut += L"(object)";
			aOut += L"\r\n";
		}
	}
};

BIF_DECL(BIF_Linux_ListVars)
{
	// Dump the script's global variables (upstream opens a debugger window;
	// on Linux the list is shown via MsgBox, i.e. printed headless).
	std::wstring out;
	LinuxVarDump::Dump(out);
	if (out.empty())
		out = L"(no variables)\r\n";
	LinuxMessageBox(nullptr, out.c_str(), L"", 0);
}

BIF_DECL(BIF_Linux_ListHotkeys)
{
	// No hotkey system on Linux: the list is empty (docs: shows the script's
	// hotkeys).
	LinuxMessageBox(nullptr, _T("(no hotkeys)\r\n"), L"", 0);
}

static int sKeyHistoryMax = 50; // Upstream default.

BIF_DECL(BIF_Linux_KeyHistory)
{
	// KeyHistory(MaxEvents): with a value, validate 0..500 and store it
	// (upstream).  Without one, display the (empty) key history.
	if (aParamCount > 0 && !ParamIndexIsOmitted(0))
	{
		int value = (int)TokenToInt64(*aParam[0]);
		if (value < 0 || value > 500)
		{
			aResultToken.Error(_T("Invalid parameter."), _T(""), ErrorPrototype::Value);
			return;
		}
		sKeyHistoryMax = value;
		return;
	}
	wchar_t buf[128];
	swprintf(buf, 128, _T("Max events: %d\r\n(no key history on Linux)\r\n"), sKeyHistoryMax);
	LinuxMessageBox(nullptr, buf, L"", 0);
}

// ---------------------------------------------------------------------------
// FileCreateShortcut / FileGetShortcut (.desktop / .url)
// ---------------------------------------------------------------------------

// Quote a .desktop Exec argument if it contains spaces.
static std::string LinuxDesktopQuote(const std::string &aArg)
{
	if (aArg.find(' ') == std::string::npos && !aArg.empty())
		return aArg;
	return "\"" + aArg + "\"";
}

BIF_DECL(BIF_Linux_FileCreateShortcut)
{
	// Target, LinkFile, WorkingDir?, Args?, Description?, IconFile?,
	// ShortcutKey?, IconNumber?, RunState?.
	TCHAR tbuf[8192], lbuf[8192], wbuf[1024], abuf[4096], dbuf[1024], ibuf[1024];
	tbuf[0] = lbuf[0] = wbuf[0] = abuf[0] = dbuf[0] = ibuf[0] = L'\0';
	LPTSTR target = TokenToString(*aParam[0], tbuf, nullptr);
	LPTSTR link = TokenToString(*aParam[1], lbuf, nullptr);
	LPTSTR wd = aParamCount > 2 && !ParamIndexIsOmitted(2) ? TokenToString(*aParam[2], wbuf, nullptr) : nullptr;
	LPTSTR args = aParamCount > 3 && !ParamIndexIsOmitted(3) ? TokenToString(*aParam[3], abuf, nullptr) : nullptr;
	LPTSTR desc = aParamCount > 4 && !ParamIndexIsOmitted(4) ? TokenToString(*aParam[4], dbuf, nullptr) : nullptr;
	LPTSTR icon = aParamCount > 5 && !ParamIndexIsOmitted(5) ? TokenToString(*aParam[5], ibuf, nullptr) : nullptr;

	if (!target || !link || !*target || !*link)
	{
		aResultToken.Error(_T("Invalid parameter."), _T(""), ErrorPrototype::Value);
		return;
	}
	std::string lpath = LinuxWideToNarrowEx(link);
	FILE *f = fopen(lpath.c_str(), "w");
	if (!f)
	{
		aResultToken.Error(_T("The shortcut file could not be created."), _T(""), ErrorPrototype::OS);
		return;
	}
	if (LinuxEndsWithI(lpath, ".url"))
	{
		// Docs: a .url file is an Internet shortcut (INI format).
		fprintf(f, "[InternetShortcut]\r\nURL=%ls\r\n", target);
		if (icon && *icon)
			fprintf(f, "IconFile=%ls\r\n", icon);
	}
	else
	{
		// .desktop launcher (the Linux equivalent of the .lnk shortcut).
		std::string exec = LinuxDesktopQuote(LinuxWideToNarrowEx(target));
		if (args && *args)
			exec += " " + LinuxWideToNarrowEx(args);
		fprintf(f, "[Desktop Entry]\r\nType=Application\r\n");
		// Name: base name of the target without extension.
		std::string t = LinuxWideToNarrowEx(target);
		size_t slash = t.find_last_of('/');
		std::string base = slash == std::string::npos ? t : t.substr(slash + 1);
		size_t dot = base.find_last_of('.');
		if (dot != std::string::npos)
			base = base.substr(0, dot);
		fprintf(f, "Name=%s\r\n", base.c_str());
		fprintf(f, "Exec=%s\r\n", exec.c_str());
		if (wd && *wd)
			fprintf(f, "Path=%ls\r\n", wd);
		if (desc && *desc)
			fprintf(f, "Comment=%ls\r\n", desc);
		if (icon && *icon)
			fprintf(f, "Icon=%ls\r\n", icon);
	}
	fclose(f);
}

BIF_DECL(BIF_Linux_FileGetShortcut)
{
	// LinkFile, &Target, &WorkingDir, &Args, &Description, &IconFile,
	// &IconNum, &RunState.
	TCHAR lbuf[8192];
	LPTSTR link = TokenToString(*aParam[0], lbuf, nullptr);
	if (!link || !*link)
	{
		aResultToken.Error(_T("Invalid parameter."), _T(""), ErrorPrototype::Value);
		return;
	}
	std::string lpath = LinuxWideToNarrowEx(link);
	std::ifstream f(lpath.c_str());
	if (!f)
	{
		aResultToken.Error(_T("The shortcut file could not be opened."), _T(""), ErrorPrototype::OS);
		return;
	}
	std::string content((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
	std::wstring target, wd, args, desc, icon;

	if (LinuxEndsWithI(lpath, ".url"))
	{
		// Parse [InternetShortcut] URL=...
		size_t p = content.find("URL=");
		if (p == std::string::npos)
		{
			aResultToken.Error(_T("The shortcut file is invalid."), _T(""), ErrorPrototype::OS);
			return;
		}
		p += 4;
		size_t e = content.find_first_of("\r\n", p);
		target = LinuxNarrowToWideEx(content.substr(p, e == std::string::npos ? std::string::npos : e - p).c_str());
	}
	else
	{
		// Parse [Desktop Entry]: Exec/Path/Comment/Icon.
		auto field = [&content](const char *aName) -> std::string {
			std::string pat = std::string(aName) + "=";
			size_t p = content.find(pat);
			if (p == std::string::npos)
				return "";
			p += pat.size();
			size_t e = content.find_first_of("\r\n", p);
			return content.substr(p, e == std::string::npos ? std::string::npos : e - p);
		};
		std::string exec = field("Exec");
		// Split the first quoted/unquoted token as the target, the rest as args.
		size_t i = 0;
		while (i < exec.size() && (exec[i] == ' ' || exec[i] == '\t'))
			++i;
		std::string targ, a;
		if (i < exec.size() && exec[i] == '"')
		{
			size_t e = exec.find('"', i + 1);
			if (e == std::string::npos)
				e = exec.size();
			targ = exec.substr(i + 1, e - i - 1);
			i = e + 1;
		}
		else
		{
			size_t e = exec.find_first_of(" \t", i);
			if (e == std::string::npos)
				e = exec.size();
			targ = exec.substr(i, e - i);
			i = e;
		}
		while (i < exec.size() && (exec[i] == ' ' || exec[i] == '\t'))
			++i;
		a = exec.substr(i);
		target = LinuxNarrowToWideEx(targ.c_str());
		args = LinuxNarrowToWideEx(a.c_str());
		wd = LinuxNarrowToWideEx(field("Path").c_str());
		desc = LinuxNarrowToWideEx(field("Comment").c_str());
		icon = LinuxNarrowToWideEx(field("Icon").c_str());
	}

	Var *out;
	if (aParamCount > 1 && (out = TokenToOutputVar(*aParam[1]))) out->Assign(target.c_str());
	if (aParamCount > 2 && (out = TokenToOutputVar(*aParam[2]))) out->Assign(wd.c_str());
	if (aParamCount > 3 && (out = TokenToOutputVar(*aParam[3]))) out->Assign(args.c_str());
	if (aParamCount > 4 && (out = TokenToOutputVar(*aParam[4]))) out->Assign(desc.c_str());
	if (aParamCount > 5 && (out = TokenToOutputVar(*aParam[5]))) out->Assign(icon.c_str());
	if (aParamCount > 6 && (out = TokenToOutputVar(*aParam[6]))) out->Assign((__int64)0); // No icon index on Linux.
	if (aParamCount > 7 && (out = TokenToOutputVar(*aParam[7]))) out->Assign((__int64)1); // Normal run state.
}
