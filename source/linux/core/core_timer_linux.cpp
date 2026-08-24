// Linux timer infrastructure (round 11): a port of the upstream
// CheckScriptTimers() (application.cpp) plus the Linux main wait loop that
// fires due timers, and the SetTimer/ToolTip BIFs.
//
//   - LinuxCheckScriptTimers: faithful port of upstream (same semantics:
//     timers run sequentially, skipping disabled/busy/low-priority ones,
//     mTimeLastRun is reset before launch, run-only-once timers disable
//     themselves, finished one-shot timers are deleted).
//   - LinuxRunMainLoop: called from main_linux.cpp after the auto-execute
//     section when the script has timers or is persistent; sleeps until the
//     next timer is due (bounded at 50 ms) and fires due timers until
//     ExitApp is requested.
//   - ToolTip: an override-redirect X11 window per tooltip index; the text
//     is drawn INTO the window body with the core font (the title is also
//     set, e.g. for accessibility).  Drawing happens pre-map so a window
//     manager cannot clear it (an override-redirect window is not repainted
//     by the WM); blank/omitted Text hides the tooltip and returns 0;
//     otherwise the HWND is returned (docs).  X/Y honour CoordMode ToolTip.
#include "../../stdafx.h"
#include "../../application.h"
#include "../../script.h"
#include "../../globaldata.h"
#include "../../script_func_impl.h"
#include "../../hotkey.h"
#include "core_timer_linux.h"
#include "core_debugger_linux.h"
#include "core_win_linux.h"
#include "core_hotkey_linux.h"
#include "core_capture_linux.h"
#include "core_gshortcut_linux.h"
#include "input_backend.h"
#include "core_clipboard_linux.h"
#include "core_wayland_linux.h"
#include "../gui/script_gui_linux.h"
#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <X11/Xutil.h>
#include <cstdlib>
#include <cstring>
#include <cstdio>
#include <string>

// Reload support: true while a SIGTERM (from a /restart sibling) is pending.
extern "C" bool LinuxRestartRequested();
#include <poll.h>

void ScriptSleep(int aDelay);

// ---------------------------------------------------------------------------
// CheckScriptTimers (port of application.cpp)
// ---------------------------------------------------------------------------

bool LinuxCheckScriptTimers()
{
	// Mirrors the upstream guard: no new timer threads while paused or when
	// the current thread forbids them or the thread limit is reached.
	if (g_nPausedThreads > 0 || (!g->AllowTimers && g_nThreads) || g_nThreads >= g_MaxThreadsTotal || !IsInterruptible())
		return false;

	ScriptTimer *ptimer, *next_timer;
	BOOL at_least_one_timer_launched = FALSE;
	DWORD tick_start;

	for (ptimer = g_script.mFirstTimer; ptimer != NULL; ptimer = next_timer)
	{
		ScriptTimer &timer = *ptimer;
		if (!timer.mEnabled || timer.mExistingThreads > 0 || timer.mPriority < g->Priority)
		{
			next_timer = timer.mNextTimer;
			continue;
		}
		tick_start = GetTickCount();
		if (tick_start - timer.mTimeLastRun < (DWORD)timer.mPeriod)
		{
			next_timer = timer.mNextTimer;
			continue;
		}
		if (!at_least_one_timer_launched)
		{
			at_least_one_timer_launched = TRUE;
			++g_nThreads;
			++g;
		}
		timer.mTimeLastRun = tick_start;
		if (timer.mRunOnlyOnce)
			timer.Disable();
		g_script.mLastPeekTime = tick_start;
		InitNewThread(timer.mPriority, false, false);
		g->CurrentTimer = &timer;
		++timer.mExistingThreads;
		timer.mCallback->ExecuteInNewThread(_T("Timer"));
		--timer.mExistingThreads;
		for (auto *this_timer = &timer; this_timer; this_timer = next_timer)
		{
			if (this_timer->mEnabled || this_timer->mExistingThreads || this_timer->mDeleteLocked)
			{
				if (this_timer == &timer)
					next_timer = this_timer->mNextTimer;
				break;
			}
			next_timer = this_timer->mNextTimer;
			if (next_timer)
				next_timer->mDeleteLocked++;
			g_script.DeleteTimer(this_timer->mCallback->ToObject());
			if (next_timer)
				next_timer->mDeleteLocked--;
		}
	}
	if (at_least_one_timer_launched)
	{
		ResumeUnderlyingThread();
		return true;
	}
	return false;
}

// ---------------------------------------------------------------------------
// Main wait loop (fires timers; exits when the script requests exit)
// ---------------------------------------------------------------------------

void LinuxRunMainLoop()
{
	Display *d = nullptr;
	while (!g_script.mPendingExitCode && !LinuxRestartRequested())
	{
		// Dispatch pending GTK/GUI events (button clicks, window close,
		// combo changes) so a visible GUI stays live and responsive.
		ahk_gtk::GtkPump();
		// Sleep until the next due timer (bounded at 50 ms so new timers and
		// exit requests are noticed quickly); when hotkeys exist, wait on the
		// X connection instead so key events are dispatched promptly.
		int sleep_ms = 50;
		if (g_script.mTimerEnabledCount)
		{
			DWORD now = GetTickCount();
			DWORD earliest = 0;
			for (ScriptTimer *timer = g_script.mFirstTimer; timer; timer = timer->mNextTimer)
			{
				if (!timer->mEnabled || timer->mExistingThreads > 0)
					continue;
				DWORD due = timer->mTimeLastRun + timer->mPeriod;
				if (!earliest || due - now < earliest - now)
					earliest = due;
			}
			if (earliest)
			{
				DWORD delta = earliest - now;
				if (delta < (DWORD)sleep_ms)
					sleep_ms = (int)delta;
				if (sleep_ms < 1)
					sleep_ms = 1;
			}
		}
		if ((d = LinuxX11Display()))
		{
			// Wait on the window/clipboard connection AND the dedicated
			// hotkey connection separately, so neither module can consume
			// the other's events (check0818 P0-1).
			struct pollfd pfds[3];
			int n = 0;
			pfds[n] = {ConnectionNumber(d), POLLIN, 0};
			++n;
			if (Display *hd = LinuxHotkeyDisplay())
			{
				pfds[n] = {ConnectionNumber(hd), POLLIN, 0};
				++n;
			}
			int pr = poll(pfds, n, sleep_ms);
			if (pr > 0)
			{
				if (pfds[0].revents & (POLLIN | POLLHUP))
					LinuxClipboardDispatchX11(d);
				if (n > 1 && (pfds[1].revents & (POLLIN | POLLHUP)) && (Hotkey::sHotkeyCount || LinuxCaptureActive()))
					LinuxDispatchHotkeys();
			}
			else if (Hotkey::sHotkeyCount || LinuxCaptureActive())
				LinuxDispatchHotkeys(); // No event: still reconcile grabs.
		}
		else if (LinuxWaylandActive())
		{
			// Wayland mode: wait on the Wayland connection so xdg configure
			// events are acknowledged promptly.
			struct pollfd pfd;
			pfd.fd = LinuxWaylandPollFd();
			pfd.events = POLLIN;
			pfd.revents = 0;
			if (pfd.fd >= 0 && poll(&pfd, 1, sleep_ms) > 0)
				LinuxWaylandDispatch();
		}
		else
			ScriptSleep(sleep_ms);
		// Global-hotkey backends (portal / GNOME Shell) need pumping
		// regardless of which backend the loop waited on.
		LinuxInputBackendDispatch();
#ifdef CONFIG_DEBUGGER
		// PreExecLine handles running code; this covers persistent-idle commands,
		// peer crash cleanup and an explicit SIGUSR2 reconnect request.
		LinuxDebuggerPump();
#endif
		LinuxCheckScriptTimers();
		// A non-persistent GUI script ends when its last window closes (and
		// there are no timers to keep it alive) - the same way the Windows
		// message pump would run out of messages and quit the script.
		if (!g_script.IsPersistent() && !g_script.mTimerEnabledCount && !ahk_gtk::GuiWindowsVisible())
			break;
	}
}

// ---------------------------------------------------------------------------
// SetTimer (upstream bif_impl in script2.cpp)
// ---------------------------------------------------------------------------

FResult SetTimer(optl<IObject *> aFunction, optl<__int64> aPeriod, optl<int> aPriority);

static optl<int> LinuxOptInt(int &aSlot, ExprTokenType *aParam[], int aParamCount, int aIndex)
{
	if (aIndex >= aParamCount || aParam[aIndex]->symbol == SYM_MISSING)
		return optl<int>(nullptr);
	aSlot = (int)TokenToInt64(*aParam[aIndex]);
	return optl<int>(aSlot);
}

static optl<__int64> LinuxOptInt64(__int64 &aSlot, ExprTokenType *aParam[], int aParamCount, int aIndex)
{
	if (aIndex >= aParamCount || aParam[aIndex]->symbol == SYM_MISSING)
		return optl<__int64>(nullptr);
	aSlot = TokenToInt64(*aParam[aIndex]);
	return optl<__int64>(aSlot);
}

BIF_DECL(BIF_Linux_SetTimer)
{
	// (In_Opt Object Function), (In_Opt Int64 Period), (In_Opt Int32 Priority).
	IObject *fn = aParamCount > 0 && !ParamIndexIsOmitted(0) ? TokenToObject(*aParam[0]) : nullptr;
	__int64 slot1 = 0;
	int slot2 = 0;
	FResult fr = SetTimer(fn
		, LinuxOptInt64(slot1, aParam, aParamCount, 1)
		, LinuxOptInt(slot2, aParam, aParamCount, 2));
	if (FAILED(fr))
		FResultToError(aResultToken, aParam, aParamCount, fr, 0);
}

// ---------------------------------------------------------------------------
// ToolTip (X11 override-redirect window; text = window title)
// ---------------------------------------------------------------------------

BIF_DECL(BIF_Linux_ToolTip)
{
	Display *d = LinuxX11Display();
	if (!d && !LinuxWaylandActive())
	{
		aResultToken.Error(_T("No X display or Wayland display is available."), _T(""), ErrorPrototype::OS);
		return;
	}
	int index = aParamCount > 3 && !ParamIndexIsOmitted(3) ? (int)TokenToInt64(*aParam[3]) : 1;
	if (index < 1 || index > 20)
		index = 1;
	static void *sTips[21] = {nullptr};
	// Blank/omitted Text hides the tooltip (docs) and returns 0.
	if (aParamCount == 0 || ParamIndexIsOmitted(0))
	{
		if (sTips[index])
		{
			if (d)
			{
				XDestroyWindow(d, (Window)(intptr_t)sTips[index]);
				XSync(d, False);
			}
			else
				LinuxWaylandDestroyWindow((LinuxWaylandWindow *)sTips[index]);
			sTips[index] = nullptr;
		}
		aResultToken.SetValue((__int64)0);
		return;
	}
	TCHAR text_buf[65536];
	LPTSTR text = TokenToString(*aParam[0], text_buf, nullptr);
	int x = -1, y = -1;
	if (aParamCount > 1 && !ParamIndexIsOmitted(1))
		x = (int)TokenToInt64(*aParam[1]);
	if (aParamCount > 2 && !ParamIndexIsOmitted(2))
		y = (int)TokenToInt64(*aParam[2]);
	// CoordMode ToolTip: CLIENT = relative to the active window's client
	// area (X11 only; Wayland has no active-window concept).
	if (d && ((g->CoordMode >> COORD_MODE_TOOLTIP) & COORD_MODE_MASK) == COORD_MODE_CLIENT && x >= 0 && y >= 0)
	{
		Window active = LinuxX11ActiveWindow();
		if (active)
		{
			int rx = 0, ry = 0;
			Window child = 0;
			if (XTranslateCoordinates(d, active, DefaultRootWindow(d), 0, 0, &rx, &ry, &child))
			{
				x += rx;
				y += ry;
			}
		}
	}
	if (!d)
	{
		// Wayland: an xdg toplevel window whose title is the text (the
		// compositor decides placement; documented).
		size_t len = _tcslen(text);
		unsigned w = (unsigned)(len * 8 + 16);
		unsigned h = 40;
		if (w < 120)
			w = 120;
		if (w > 600)
			w = 600;
		LinuxWaylandWindow *win = LinuxWaylandCreateWindow(text, (int)w, (int)h);
		if (!win)
		{
			aResultToken.Error(_T("The tooltip window could not be created."), _T(""), ErrorPrototype::OS);
			return;
		}
		if (sTips[index])
			LinuxWaylandDestroyWindow((LinuxWaylandWindow *)sTips[index]);
		sTips[index] = win;
		aResultToken.SetValue((__int64)(intptr_t)win);
		return;
	}
	Window tip = sTips[index] ? (Window)(intptr_t)sTips[index] : 0;
	if (!tip)
	{
		// Size the window to fit the text (~8 px/char, core-font line height).
		XFontStruct *szfont = XLoadQueryFont(d, "fixed");
		if (!szfont)
			szfont = XLoadQueryFont(d, "9x15");
		int line_h = szfont ? szfont->ascent + szfont->descent : 16;
		size_t len = _tcslen(text);
		int lines = 1;
		for (const TCHAR *p = text; *p; ++p)
			if (*p == L'\n')
				++lines;
		unsigned w = (unsigned)(len * 8 + 16);
		unsigned h = (unsigned)(10 + lines * line_h + 10);
		if (w < 120)
			w = 120;
		if (w > 600)
			w = 600;
		tip = XCreateSimpleWindow(d, DefaultRootWindow(d), x < 0 ? 0 : x, y < 0 ? 0 : y
			, w, h, 1, BlackPixel(d, DefaultScreen(d)), WhitePixel(d, DefaultScreen(d)));
		XSetWindowAttributes attrs;
		attrs.override_redirect = True;
		attrs.save_under = True;
		XChangeWindowAttributes(d, tip, CWOverrideRedirect | CWSaveUnder, &attrs);
		// No background repaint: otherwise the server white-washes the window
		// on map/expose, erasing our pre-drawn text (the same class of bug as
		// the MsgBox expose loss, just server-side for an override-redirect
		// tooltip).  With background None the drawn content is retained.
		XSetWindowBackgroundPixmap(d, tip, None);
		if (szfont)
			XFreeFont(d, szfont);
		sTips[index] = (void *)(intptr_t)tip;
	}
	// Draw the text INTO the window body (otherwise the tooltip appears as a
	// blank box on real desktops, since only the title was set).  Pre-map draw
	// persists: an override-redirect window is not repainted by the WM.
	// Residual limitation: an expose of an already-mapped tooltip is not
	// repainted (no event pump for it).
	GC gc = XCreateGC(d, tip, 0, nullptr);
	XFontStruct *font = XLoadQueryFont(d, "fixed");
	if (!font)
		font = XLoadQueryFont(d, "9x15");
	if (font)
	{
		XSetFont(d, gc, font->fid);
		XClearWindow(d, tip);
		char utf8line[131072];
		int y0 = 10 + font->ascent;
		const TCHAR *p = text;
		while (*p)
		{
			const TCHAR *eol = _tcschr(p, L'\n');
			size_t l = eol ? (size_t)(eol - p) : _tcslen(p);
			TCHAR wline[2048];
			if (l >= sizeof(wline) / sizeof(wline[0]))
				l = sizeof(wline) / sizeof(wline[0]) - 1;
			for (size_t i = 0; i < l; ++i)
				wline[i] = p[i];
			wline[l] = L'\0';
			int ul = WideCharToUTF8(wline, utf8line, (int)sizeof(utf8line));
			if (ul > 0)
				XDrawString(d, tip, gc, 8, y0, utf8line, ul - 1);
			y0 += font->ascent + font->descent;
			if (!eol)
				break;
			p = eol + 1;
		}
		XFreeFont(d, font);
	}
	XFreeGC(d, gc);
	Atom utf8 = XInternAtom(d, "UTF8_STRING", False);
	char utf8buf[131072];
	int ulen = WideCharToUTF8(text, utf8buf, (int)sizeof(utf8buf));
	if (ulen > 0)
	{
		XChangeProperty(d, tip, XInternAtom(d, "_NET_WM_NAME", False), utf8, 8
			, PropModeReplace, (const unsigned char *)utf8buf, (unsigned long)(ulen - 1));
		XStoreName(d, tip, utf8buf);
	}
	if (x >= 0 && y >= 0)
		XMoveWindow(d, tip, x, y);
	XMapRaised(d, tip);
	XSync(d, False);
	aResultToken.SetValue((__int64)tip); // Docs: the tooltip's HWND.
}
