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
//     is the window title (no toolkit text rendering on X11); blank/omitted
//     Text hides the tooltip and returns 0; otherwise the HWND is returned
//     (docs).  X/Y honour CoordMode ToolTip.
#include "../../stdafx.h"
#include "../../application.h"
#include "../../script.h"
#include "../../globaldata.h"
#include "../../script_func_impl.h"
#include "core_timer_linux.h"
#include "core_win_linux.h"
#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <X11/Xutil.h>
#include <cstdlib>
#include <cstring>
#include <cstdio>
#include <string>

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
	while (!g_script.mPendingExitCode)
	{
		// Sleep until the next due timer (bounded at 50 ms so new timers and
		// exit requests are noticed quickly).
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
		ScriptSleep(sleep_ms);
		LinuxCheckScriptTimers();
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
	if (!d)
	{
		aResultToken.Error(_T("No X display is available."), _T(""), ErrorPrototype::OS);
		return;
	}
	int index = aParamCount > 3 && !ParamIndexIsOmitted(3) ? (int)TokenToInt64(*aParam[3]) : 1;
	if (index < 1 || index > 20)
		index = 1;
	static Window sTips[21] = {0};
	// Blank/omitted Text hides the tooltip (docs) and returns 0.
	if (aParamCount == 0 || ParamIndexIsOmitted(0))
	{
		if (sTips[index])
		{
			XDestroyWindow(d, sTips[index]);
			sTips[index] = 0;
			XSync(d, False);
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
	// CoordMode ToolTip: CLIENT = relative to the active window's client area.
	if (((g->CoordMode >> COORD_MODE_TOOLTIP) & COORD_MODE_MASK) == COORD_MODE_CLIENT && x >= 0 && y >= 0)
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
	Window tip = sTips[index];
	if (!tip)
	{
		// Estimate a size from the text length (no text metrics without a
		// toolkit): ~8 px per character, ~20 px per line.
		size_t len = _tcslen(text);
		unsigned w = (unsigned)(len * 8 + 16);
		unsigned h = 40;
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
		sTips[index] = tip;
	}
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
