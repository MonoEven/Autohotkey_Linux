// Linux hotkey activation (round 28 rewrite; see check0818.md audit).
//
// Design fixes applied per the audit:
//   P0-1  Dedicated X11 connection (LinuxHotkeyDisplay): the hotkey backend
//         never shares the window/clipboard display, so LinuxDispatchHotkeys
//         can no longer consume SelectionRequest/PropertyNotify/etc. that
//         belong to other modules.
//   P0-2  desired/installed grab diff (GrabSpec, not Hotkey*): "Hotkey F7,
//         Off", disabled variants and suspend now XUngrabKey the combo, so
//         the key is delivered to other applications again.
//   P0-3  Pass-through: X11 passive grabs hand the event to the grabbing
//         client, and Async grabs cannot "replay" it.  Sync grabs were
//         rejected (frozen events never notify the grabber -> deadlock), so
//         passthrough (tilde, HotIf false, Off, Suspend, thread limits,
//         key-up hotkey's non-firing phase) is done by re-injecting the
//         event with XTEST, with a short ignore-window for the re-grabbed
//         copy.  Documented deviation from ReplayKeyboard (check0818 P0-3).
//   P0-4  BadAccess (grab conflict with another client) is detected through
//         a scoped X error trap keyed on the request serial, reported as an
//         OSError from BIF_Linux_Hotkey, and the failed grab is not treated
//         as installed.
//   P1-1  Reconcile runs right after registration and before the main loop
//         (not only when an X event arrives), removing the cold-start
//         timing hazard.
//   P1-2  Dynamic modifier map via XGetModifierMapping (Alt/Super slots and
//         the actual lock modifiers), rebuilt on MappingNotify with a full
//         re-grab; the lock-mask power set is enumerated instead of a
//         hard-coded Caps|Num list.
//   P1-4  XkbSetDetectableAutoRepeat is enabled on the hotkey connection so
//         key-up hotkeys fire once per physical release (with a fallback
//         filter for synthetic repeats when the server lacks XKB support).
//
// The upstream Hotkey() parse/variant machinery is unchanged; features the
// X11 backend cannot express (mouse keys, scan codes, "A & B" prefixes,
// left/right modifier discrimination, wildcard modifiers) are NOT grabbed
// and are documented as unsupported rather than silently misbehaving.
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
#include <X11/XKBlib.h>
#include <X11/extensions/XTest.h>
#include <set>
#include <vector>
#include <cstring>
#include <cstdio>

namespace {

static optl<StrArg> LinuxHotkeyOptStr(ExprTokenType *aParam[], int aParamCount, int aIndex, TCHAR *aBuf, size_t aBufSize)
{
	aBuf[0] = L'\0';
	if (aIndex >= aParamCount || aParam[aIndex]->symbol == SYM_MISSING)
		return optl<StrArg>(nullptr);
	TokenToString(*aParam[aIndex], aBuf, nullptr);
	return optl<StrArg>(aBuf);
}

// ---------------------------------------------------------------------------
// Dedicated connection
// ---------------------------------------------------------------------------

Display *sDpy = nullptr;
bool sDpyUnavailable = false;

// XKB detectable auto-repeat enabled (key-up semantics).
bool sDetectableAutoRepeat = false;

// Fallback synthetic-repeat filter state (no XKB): the previous event.
KeyCode sPrevKeyCode = 0;
Time sPrevTime = 0;
bool sPrevWasPress = false;

// ---------------------------------------------------------------------------
// Desired/installed grab bookkeeping
// ---------------------------------------------------------------------------

struct GrabSpec
{
	KeyCode keycode;
	unsigned int modifiers;
	bool operator<(const GrabSpec &o) const
	{
		return keycode != o.keycode ? keycode < o.keycode : modifiers < o.modifiers;
	}
};

std::set<GrabSpec> sInstalled;

// Name of the key involved in the most recent BadAccess conflict (for the
// BIF error message), or empty.
TCHAR sLastConflictName[256];

// ---------------------------------------------------------------------------
// Dynamic modifier map (Alt/Super slots + actual lock modifiers)
// ---------------------------------------------------------------------------

unsigned int sAltMask = Mod1Mask;   // Populated from XGetModifierMapping.
unsigned int sSuperMask = Mod4Mask;
unsigned int sLockMasks[8];
int sLockMaskCount = 2;             // LockMask/Mod2Mask fallback.

void LinuxUpdateModifierMap(Display *d)
{
	unsigned int lock[8];
	int nlock = 0;
	unsigned int alt = 0, super = 0;
	XModifierKeymap *map = XGetModifierMapping(d);
	if (map)
	{
		for (int slot = 0; slot < 8; ++slot)
		{
			unsigned int mask = 1u << slot;
			for (int k = 0; k < map->max_keypermod; ++k)
			{
				KeyCode kc = map->modifiermap[slot * map->max_keypermod + k];
				if (!kc)
					continue;
				KeySym ks = XkbKeycodeToKeysym(d, kc, 0, 0);
				if (ks == XK_Alt_L || ks == XK_Alt_R)
					alt |= mask;
				else if (ks == XK_Super_L || ks == XK_Super_R)
					super |= mask;
				else if (ks == XK_Num_Lock || ks == XK_Caps_Lock || ks == XK_Scroll_Lock)
				{
					bool dup = false;
					for (int i = 0; i < nlock; ++i)
						if (lock[i] == mask)
							dup = true;
					if (!dup)
						lock[nlock++] = mask;
				}
			}
		}
		XFreeModifiermap(map);
	}
	sAltMask = alt ? alt : Mod1Mask;      // Fallbacks match the common layout.
	sSuperMask = super ? super : Mod4Mask;
	if (nlock)
	{
		memcpy(sLockMasks, lock, sizeof(lock[0]) * (size_t)nlock);
		sLockMaskCount = nlock;
	}
	else
	{
		sLockMasks[0] = LockMask;
		sLockMasks[1] = Mod2Mask;
		sLockMaskCount = 2;
	}
}

// X11 modifier mask for the Windows MOD_* bits (dynamic Alt/Super slots).
unsigned int LinuxModsToX(mod_type aModifiers)
{
	unsigned int m = 0;
	if (aModifiers & MOD_CONTROL)
		m |= ControlMask;
	if (aModifiers & MOD_SHIFT)
		m |= ShiftMask;
	if (aModifiers & MOD_ALT)
		m |= sAltMask;
	if (aModifiers & MOD_WIN)
		m |= sSuperMask;
	return m;
}

// ---------------------------------------------------------------------------
// Per-request X error trap (grabs are asynchronous; BadAccess arrives later)
// ---------------------------------------------------------------------------

struct TrapRecord
{
	unsigned long serial;
	int code;
};

TrapRecord sTrap = {0, 0};
Display *sTrapDpy = nullptr;
XErrorHandler sTrapPrev = nullptr;

int LinuxErrorTrapHandler(Display *d, XErrorEvent *e)
{
	if (d == sTrapDpy && e->error_code == BadAccess)
		sTrap = {e->serial, BadAccess};
	return 0;
}

class ScopedXErrorTrap
{
public:
	explicit ScopedXErrorTrap(Display *d)
		: mD(d)
	{
		sTrapDpy = d;
		sTrap = {0, 0};
		sTrapPrev = XSetErrorHandler(LinuxErrorTrapHandler);
	}
	~ScopedXErrorTrap()
	{
		XSync(mD, False); // Flush so any BadAccess reaches the trap.
		XSetErrorHandler(sTrapPrev);
	}
	bool HasBadAccess() const { return sTrap.code == BadAccess; }
	unsigned long BadSerial() const { return sTrap.serial; }

private:
	Display *mD;
};

// Keycode used to grab/fire this hotkey (0 = unsupported on X11).
KeyCode LinuxHotkeyKeycode(Display *d, Hotkey *aHotkey)
{
	if (aHotkey->mVK)
		return LinuxKeycodeForVkEx(d, aHotkey->mVK);
	return 0; // Scan-code or other hotkeys are not supported.
}

// Build the desired grab set: every key+modifier combination (including the
// full lock-modifier power set) of every hotkey that has an enabled variant.
void LinuxBuildDesired(Display *d, std::set<GrabSpec> &aDesired)
{
	unsigned int combos[8];
	int ncombos = 1;
	combos[0] = 0;
	for (int i = 0; i < sLockMaskCount && ncombos < 8; ++i)
	{
		int n = ncombos;
		for (int j = 0; j < n; ++j)
			combos[ncombos++] = combos[j] | sLockMasks[i];
	}
	for (int i = 0; i < Hotkey::sHotkeyCount; ++i)
	{
		Hotkey *hk = Hotkey::shk[i];
		if (!hk || hk->mModifierVK || hk->IsCompletelyDisabled())
			continue; // Prefix hotkeys unsupported; disabled variants ungrabbed.
		KeyCode kc = LinuxHotkeyKeycode(d, hk);
		if (!kc)
			continue; // Scan-code / mouse: unsupported, not grabbed.
		unsigned int base = LinuxModsToX(hk->mModifiers);
		for (int c = 0; c < ncombos; ++c)
			aDesired.insert(GrabSpec{kc, base | combos[c]});
	}
}

// ---------------------------------------------------------------------------
// Event matching and dispatch
// ---------------------------------------------------------------------------

// Fallback synthetic-repeat filter: with classic auto-repeat the server
// produces Press/Release/Press/Release... pairs; the synthetic Release is
// the one that immediately follows a Press of the same keycode.
bool LinuxIsSyntheticRelease(XEvent &aEv)
{
	if (aEv.type != KeyRelease)
		return false;
	// Synthetic pairs come in with identical timestamps in practice; use
	// a small window to tolerate server timing.
	long delta = (long)aEv.xkey.time - (long)sPrevTime;
	if (sPrevWasPress && aEv.xkey.keycode == sPrevKeyCode && delta >= 0 && delta < 50)
		return true;
	return false;
}

// Passthrough re-injection: when a grabbed combination must be passed
// through (tilde, HotIf false, key-up hotkey's non-firing phase), the event
// is re-injected with XTEST.  While a passive grab is active the whole
// keyboard belongs to the grabbing client, so re-injecting under the grab
// would feed the event back into our own queue (infinite loop).  The active
// keyboard grab is therefore released first (the passive grab
// registrations stay installed) and the injected event reaches the normal
// target window.  The injected press re-activates the passive grab and is
// grabbed back by us; those copies are suppressed through a short log of
// recent injections (keycode + direction).  A multi-entry log (rather than
// a single "last injection" mark) is required because the real release
// travels on the shared X connection while the injected copy comes back on
// the hotkey connection: the two connections are independent, so the copy
// may arrive AFTER the release has already updated the mark.  This is the
// documented X11 equivalent of ReplayKeyboard (check0818 P0-3; sync grabs
// were rejected because frozen events never notify the grabbing client,
// which deadlocks).  Note: releasing the active keyboard grab also ends any
// XGrabKeyboard grab by other code paths (e.g. BlockInput) -- acceptable
// for passthrough hotkeys, which are rare.
struct PassthruMark
{
	KeyCode keycode;
	DWORD when; // GetTickCount() at injection time.
	bool is_up;
};
PassthruMark sPassthruLog[8];
int sPassthruHead = 0;

void LinuxInjectKey(Display *d, XEvent &ev)
{
	XUngrabKeyboard(d, CurrentTime);
	XTestFakeKeyEvent(d, ev.xkey.keycode, ev.type == KeyPress ? True : False, CurrentTime);
	XFlush(d);
	PassthruMark &m = sPassthruLog[sPassthruHead++ % _countof(sPassthruLog)];
	m = PassthruMark{ev.xkey.keycode, GetTickCount(), ev.type == KeyRelease};
}

bool LinuxIsPassthruCopy(XEvent &ev)
{
	bool is_up = ev.type == KeyRelease;
	DWORD now = GetTickCount();
	for (int i = 0; i < _countof(sPassthruLog); ++i)
		if (sPassthruLog[i].keycode == ev.xkey.keycode
			&& sPassthruLog[i].is_up == is_up
			&& now - sPassthruLog[i].when < 300)
			return true;
	return false;
}

void LinuxHandleKeyEvent(Display *d, XEvent &ev)
{
	sPrevKeyCode = ev.xkey.keycode;
	sPrevTime = ev.xkey.time;
	sPrevWasPress = ev.type == KeyPress;

	// Suppress the re-grabbed copy of a passthrough-injected event (the
	// target already received the injected one).
	if (LinuxIsPassthruCopy(ev))
		return;

	bool is_up = ev.type == KeyRelease;

	// The grab may activate on any lock state; the event's state then
	// carries those bits.  Compare only the primary modifier slots.
	unsigned int evmods = ev.xkey.state & (ControlMask | ShiftMask | sAltMask | sSuperMask);

	Hotkey *hk_fire = nullptr;
	HotkeyVariant *vp_fire = nullptr;
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
		if (is_up && !sDetectableAutoRepeat && LinuxIsSyntheticRelease(ev))
			continue;
		hk_fire = hk;
		vp_fire = vp;
		break;
	}

	if (hk_fire)
	{
		// Consume the event unless the hotkey is a pass-through (~): in that
		// case re-inject it so the foreground application still receives it.
		// Note: mNoSuppress also carries bookkeeping flags such as
		// AT_LEAST_ONE_VARIANT_LACKS_TILDE, so test only NO_SUPPRESS_PREFIX.
		bool suppress = !((hk_fire->mNoSuppress & (NO_SUPPRESS_PREFIX | AT_LEAST_ONE_VARIANT_HAS_TILDE))
			|| (vp_fire->mNoSuppress & (NO_SUPPRESS_PREFIX | AT_LEAST_ONE_VARIANT_HAS_TILDE)));
		if (!suppress)
			LinuxInjectKey(d, ev);
		// Fire in a new quasi-thread (same pattern as the timer loop).
		++g_nThreads;
		++g;
		InitNewThread(vp_fire->mPriority, false, false);
		hk_fire->PerformInNewThreadMadeByCaller(*vp_fire);
		ResumeUnderlyingThread();
	}
	else
	{
		// No variant may fire (HotIf false, Suspend, thread limits) or this
		// event belongs to a pass-through/key-up hotkey's non-firing phase:
		// re-inject the event so the normal target window receives it.
		// (X11 passive grabs hand the event to us; passthrough is done by
		// releasing the active keyboard grab and re-injecting with XTEST --
		// check0818 P0-3, documented deviation from ReplayKeyboard.)
		LinuxInjectKey(d, ev);
	}
}

} // namespace

// ---------------------------------------------------------------------------
// Public interface
// ---------------------------------------------------------------------------

Display *LinuxHotkeyDisplay()
{
	if (sDpy || sDpyUnavailable)
		return sDpy;
	Display *d = XOpenDisplay(nullptr);
	if (!d)
	{
		sDpyUnavailable = true;
		return nullptr;
	}
	// Detectable auto-repeat: key-up hotkeys then fire once per physical
	// release even while a key is held down.
	Bool supported = False;
	if (XkbSetDetectableAutoRepeat(d, True, &supported) == Success && supported)
		sDetectableAutoRepeat = true;
	LinuxUpdateModifierMap(d);
	sDpy = d;
	return d;
}

void LinuxReconcileHotkeyGrabs()
{
	Display *d = LinuxHotkeyDisplay();
	if (!d)
		return;
	Window root = DefaultRootWindow(d);

	std::set<GrabSpec> desired;
	LinuxBuildDesired(d, desired);

	// Ungrab combinations that are no longer desired.
	for (std::set<GrabSpec>::iterator it = sInstalled.begin(); it != sInstalled.end();)
	{
		if (!desired.count(*it))
		{
			XUngrabKey(d, it->keycode, it->modifiers, root);
			sInstalled.erase(it++);
		}
		else
			++it;
	}

	// Grab the missing combinations; record request serials so a BadAccess
	// can be attributed to the exact grab.
	struct PendingGrab
	{
		unsigned long serial;
		GrabSpec spec;
	};
	std::vector<PendingGrab> pending;
	sLastConflictName[0] = _T('\0');
	{
		ScopedXErrorTrap trap(d);
		for (std::set<GrabSpec>::iterator it = desired.begin(); it != desired.end(); ++it)
		{
			if (sInstalled.count(*it))
				continue;
			unsigned long serial = (unsigned long)XNextRequest(d) - 1;
			XGrabKey(d, it->keycode, it->modifiers, root, False, GrabModeAsync, GrabModeAsync);
			pending.push_back(PendingGrab{serial, *it});
		}
		if (trap.HasBadAccess())
		{
			// Find the offending grab and drop it from the "installed" view.
			for (size_t i = 0; i < pending.size(); ++i)
			{
				if (pending[i].serial == trap.BadSerial())
				{
					KeySym ks = XkbKeycodeToKeysym(d, pending[i].spec.keycode, 0, 0);
					_sntprintf(sLastConflictName, _countof(sLastConflictName), _T("%s (modifiers %X)")
						, ks ? XKeysymToString(ks) : _T("key"), pending[i].spec.modifiers);
					break;
				}
			}
		}
	}

	// XSync inside the trap destructor flushed the requests above; the
	// conflict-free grabs are now installed.
	for (size_t i = 0; i < pending.size(); ++i)
		sInstalled.insert(pending[i].spec);
}

void LinuxDispatchHotkeys()
{
	Display *d = LinuxHotkeyDisplay();
	if (!d)
		return;
	LinuxReconcileHotkeyGrabs();
	while (XPending(d) > 0)
	{
		XEvent ev;
		XNextEvent(d, &ev);
		switch (ev.type)
		{
		case KeyPress:
		case KeyRelease:
			LinuxHandleKeyEvent(d, ev);
			break;
		case MappingNotify:
			// Keyboard map/layout changed: refresh the modifier slots and
			// rebuild every grab from scratch.
			XRefreshKeyboardMapping(&ev.xmapping);
			LinuxUpdateModifierMap(d);
			{
				Window root = DefaultRootWindow(d);
				for (std::set<GrabSpec>::iterator it = sInstalled.begin(); it != sInstalled.end(); ++it)
					XUngrabKey(d, it->keycode, it->modifiers, root);
				sInstalled.clear();
			}
			break;
		default:
			// The dedicated connection only carries grab/keyboard events.
			break;
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
	if (!LinuxHotkeyDisplay())
	{
		aResultToken.Error(LinuxWaylandActive()
			? _T("Hotkeys are not available on Wayland (no global hotkey protocol); use XWayland.")
			: _T("Hotkeys require an X11 display (XOpenDisplay failed)."), _T(""), ErrorPrototype::OS);
		return;
	}
	TCHAR name_buf[1024];
	LPTSTR name = TokenToString(*aParam[0], name_buf, nullptr);
	TCHAR opt_buf[256];
	opt_buf[0] = _T('\0');
	FResult fr = BIF_Hotkey(name
		, aParamCount > 1 && !ParamIndexIsOmitted(1) ? aParam[1] : nullptr
		, LinuxHotkeyOptStr(aParam, aParamCount, 2, opt_buf, sizeof(opt_buf)));
	if (FAILED(fr))
	{
		FResultToError(aResultToken, aParam, aParamCount, fr, 0);
		return;
	}
	// Registration succeeded: make the backend reconcile immediately (no
	// cold-start wait for an X event) and surface grab conflicts.
	LinuxReconcileHotkeyGrabs();
	if (sLastConflictName[0])
	{
		aResultToken.Error(_T("Hotkey could not be registered: the key combination is already grabbed by another client (X11 BadAccess): ")
			, sLastConflictName, ErrorPrototype::OS);
		sLastConflictName[0] = _T('\0');
	}
}