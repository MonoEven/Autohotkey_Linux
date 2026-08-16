// Linux hotkey activation (round 12): XGrabKey-based.
//
// The upstream Hotkey() function (script2.cpp BIF_Hotkey -> Hotkey::Dynamic)
// parses key names/options and keeps the hotkey list; on Windows a keyboard
// hook dispatches events.  On Linux each hotkey is registered with
// XGrabKey on the root window and key events are matched in the main loop
// and during MsgSleep waits:
//   - modifier bits (MOD_CONTROL/SHIFT/ALT/WIN) map to the X11 masks
//     Control/Shift/Mod1/Mod4; the combo is grabbed with and without the
//     CapsLock/NumLock modifiers so those don't block firing.
//   - key-up hotkeys (mKeyUp) fire on KeyRelease, others on KeyPress;
//     the event's modifier state must match the hotkey exactly (extraneous
//     modifiers block firing, matching mAllowExtraModifiers=false).
//   - mouse/scan-code/prefix ("A & B") hotkeys have no X11 equivalent and
//     are not grabbed (documented limitation).
//   - the matched variant fires through the upstream
//     PerformInNewThreadMadeByCaller (throttling + ExecuteInNewThread).
#include "../../stdafx.h"
#include "../../application.h"
#include "../../script.h"
#include "../../globaldata.h"
#include "../../script_func_impl.h"
#include "../../hotkey.h"
#include "core_hotkey_linux.h"
#include "core_win_linux.h"
#include "core_wayland_linux.h"
#include "core_input_linux.h"
#include <X11/Xlib.h>
#include <X11/keysym.h>
#include <set>
#include <poll.h>

// X11 modifier mask for the Windows MOD_* bits.
static unsigned int LinuxModsToX(mod_type aModifiers)
{
	unsigned int m = 0;
	if (aModifiers & MOD_CONTROL)
		m |= ControlMask;
	if (aModifiers & MOD_SHIFT)
		m |= ShiftMask;
	if (aModifiers & MOD_ALT)
		m |= Mod1Mask;
	if (aModifiers & MOD_WIN)
		m |= Mod4Mask;
	return m;
}

static optl<StrArg> LinuxHotkeyOptStr(ExprTokenType *aParam[], int aParamCount, int aIndex, TCHAR *aBuf, size_t aBufSize)
{
	aBuf[0] = L'\0';
	if (aIndex >= aParamCount || aParam[aIndex]->symbol == SYM_MISSING)
		return optl<StrArg>(nullptr);
	TokenToString(*aParam[aIndex], aBuf, nullptr);
	return optl<StrArg>(aBuf);
}

// Keycode used to grab/fire this hotkey (0 = unsupported on X11).
static KeyCode LinuxHotkeyKeycode(Display *d, Hotkey *aHotkey)
{
	if (aHotkey->mVK)
		return LinuxKeycodeForVkEx(d, aHotkey->mVK);
	return 0; // Scan-code or other hotkeys are not supported.
}

// (Re)establish X grabs for every registered hotkey.
void LinuxUpdateHotkeyGrabs(Display *d)
{
	static std::set<Hotkey *> sGrabbed;
	Window root = DefaultRootWindow(d);
	for (int i = 0; i < Hotkey::sHotkeyCount; ++i)
	{
		Hotkey *hk = Hotkey::shk[i];
		if (!hk || hk->mModifierVK || sGrabbed.count(hk))
			continue; // Prefix hotkeys unsupported; others already grabbed.
		KeyCode kc = LinuxHotkeyKeycode(d, hk);
		if (!kc)
			continue;
		unsigned int mods = LinuxModsToX(hk->mModifiers);
		// Grab with/without the lock modifiers so CapsLock/NumLock states
		// don't prevent the hotkey from firing.
		XGrabKey(d, kc, mods, root, False, GrabModeAsync, GrabModeAsync);
		XGrabKey(d, kc, mods | LockMask, root, False, GrabModeAsync, GrabModeAsync);
		XGrabKey(d, kc, mods | Mod2Mask, root, False, GrabModeAsync, GrabModeAsync);
		XGrabKey(d, kc, mods | LockMask | Mod2Mask, root, False, GrabModeAsync, GrabModeAsync);
		sGrabbed.insert(hk);
	}
	XSync(d, False);
}

// Fire hotkeys for pending X key events.  Called from the main loop and
// from MsgSleep so hotkeys work both while idle and while the script waits.
void LinuxDispatchHotkeys(Display *d)
{
	LinuxUpdateHotkeyGrabs(d);
	while (XPending(d) > 0)
	{
		XEvent ev;
		XNextEvent(d, &ev);
		if (ev.type != KeyPress && ev.type != KeyRelease)
			continue;
		bool is_up = ev.type == KeyRelease;
		unsigned int evmods = ev.xkey.state & (ControlMask | ShiftMask | Mod1Mask | Mod4Mask);
		for (int i = 0; i < Hotkey::sHotkeyCount; ++i)
		{
			Hotkey *hk = Hotkey::shk[i];
			if (!hk || hk->mKeyUp != is_up)
				continue;
			KeyCode kc = LinuxHotkeyKeycode(d, hk);
			if (!kc || kc != ev.xkey.keycode)
				continue;
			if (LinuxModsToX(hk->mModifiers) != evmods)
				continue;
			HotkeyVariant *vp = hk->FindVariant();
			if (!vp || !vp->mEnabled || !hk->PerformIsAllowed(*vp))
				continue;
			// Fire in a new quasi-thread (same pattern as the timer loop).
			++g_nThreads;
			++g;
			InitNewThread(vp->mPriority, false, false);
			hk->PerformInNewThreadMadeByCaller(*vp);
			ResumeUnderlyingThread();
		}
	}
}

// ---------------------------------------------------------------------------
// Hotkey function (upstream BIF_Hotkey in script2.cpp)
// ---------------------------------------------------------------------------

FResult BIF_Hotkey(StrArg aName, ExprTokenType *aAction, optl<StrArg> aOptions);

BIF_DECL(BIF_Linux_Hotkey)
{
	// (In String KeyName), (In_Opt Variant Action), (In_Opt String Options).
	// Hotkeys need global key grabbing (XGrabKey); Wayland has no global
	// hotkey protocol, so they are unavailable there (documented).
	if (!LinuxX11Display() && LinuxWaylandActive())
	{
		aResultToken.Error(_T("Hotkeys are not available on Wayland (no global hotkey protocol); use XWayland."), _T(""), ErrorPrototype::OS);
		return;
	}
	TCHAR name_buf[1024];
	LPTSTR name = TokenToString(*aParam[0], name_buf, nullptr);
	TCHAR opt_buf[256];
	opt_buf[0] = L'\0';
	FResult fr = BIF_Hotkey(name
		, aParamCount > 1 && !ParamIndexIsOmitted(1) ? aParam[1] : nullptr
		, LinuxHotkeyOptStr(aParam, aParamCount, 2, opt_buf, sizeof(opt_buf)));
	if (FAILED(fr))
		FResultToError(aResultToken, aParam, aParamCount, fr, 0);
}
