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
// The upstream Hotkey() parse/variant machinery is unchanged.  Features the
// X11 backend cannot express (scan codes, "A & B" prefixes) are NOT grabbed
// and are documented as unsupported rather than silently misbehaving.
//
// Left/right modifiers and wildcard modifiers (round 31): X11 passive grabs
// match modifier masks only, so the grab uses the combined mask of the
// neutral and LR-specific bits (both sides of Ctrl share ControlMask); the
// event handler then discriminates the physical side with XQueryKeymap
// (left/right ctrl/shift/alt/win keycodes) against the hotkey\'s consolidated
// LR set, and a wrong-side press no variant matches is passed through with
// the standard XTEST re-injection (Windows parity).  Wildcard (*) hotkeys
// expand the grab to the full primary-modifier power set so the grab
// activates under any extra modifiers, and the handler accepts them.  Event
// matching uses a per-(key/button, modifiers) hash index rebuilt when the
// grab set changes; resolution is unique: exact hotkeys beat wildcard ones,
// and among equals the one allowing fewer side bits wins.
// Mouse hotkeys
// (LButton/RButton/MButton/XButton1/XButton2/Wheel*) are grabbed with
// XGrabButton on the same connection, with the same reconcile, BadAccess
// trap, lock-mask power set and XTEST passthrough as keyboard hotkeys.
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
#include <X11/extensions/XInput2.h>
#include <set>
#include <vector>
#include <unordered_map>
#include <cstring>
#include <cstdio>

// Reload support (core_platform_stubs.cpp): bail out of the dispatch loop
// when a restart is pending so a stuck dispatch cannot block the exit.
extern "C" bool LinuxRestartRequested();

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
	KeyCode keycode;      // Key grab (button == 0).
	unsigned int button;  // Mouse grab (keycode == 0): X11 button 1..9.
	unsigned int modifiers;
	bool operator<(const GrabSpec &o) const
	{
		if (keycode != o.keycode)
			return keycode < o.keycode;
		if (button != o.button)
			return button < o.button;
		return modifiers < o.modifiers;
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

// X11 modifier mask contributed by left/right-specific modifiers (X11 cannot
// distinguish sides, so both sides map to the same mask).
unsigned int LinuxModsToXLR(modLR_type aModifiersLR)
{
	unsigned int m = 0;
	if (aModifiersLR & (MOD_LCONTROL | MOD_RCONTROL))
		m |= ControlMask;
	if (aModifiersLR & (MOD_LSHIFT | MOD_RSHIFT))
		m |= ShiftMask;
	if (aModifiersLR & (MOD_LALT | MOD_RALT))
		m |= sAltMask;
	if (aModifiersLR & (MOD_LWIN | MOD_RWIN))
		m |= sSuperMask;
	return m;
}

// Left/right state of the modifiers held now, read from the physical keymap.
// Only a side whose keycode is verified down is reported; a mask bit set
// without any side keycode (unusual layouts) contributes no side bit, so
// neutral hotkeys still match while side-specific ones do not.
// ---------------------------------------------------------------------------
// XI2 raw-event modifier-side observer (check0818 batch 3: XI2 observer)
// ---------------------------------------------------------------------------
// X11 passive grabs match modifier masks only (no side information), and
// XQueryKeymap reflects the state at query time rather than at event
// generation time -- wrong for batched XTEST input, where the modifier may
// already be released when the queued grabbed event is dispatched.  The
// XI2 raw-event stream reports every physical key press/release with its
// keycode (hence its side) as it happens, on the same connection, before
// the corresponding grabbed event; a small state machine tracks which
// sides of Ctrl/Shift/Alt/Win are held.  Servers without XInput2 fall back
// to XQueryKeymap (accurate for human-paced input; documented deviation
// for batched XTEST input).

bool sXI2Observer = false;
int sXI2Opcode = 0;
KeyCode sModKc[8];          // L/R ctrl, shift, alt, win keycodes.
modLR_type sHeldLR = 0;

void LinuxTrackModifierKeycode(KeyCode aKc, bool aDown)
{
	static const modLR_type sBit[8] = { MOD_LCONTROL, MOD_RCONTROL, MOD_LSHIFT, MOD_RSHIFT
		, MOD_LALT, MOD_RALT, MOD_LWIN, MOD_RWIN };
	for (int i = 0; i < 8; ++i)
		if (sModKc[i] && sModKc[i] == aKc)
		{
			if (aDown)
				sHeldLR |= sBit[i];
			else
				sHeldLR = (modLR_type)(sHeldLR & ~sBit[i]);
			break;
		}
}

void LinuxUpdateModifierKeycodes(Display *d)
{
	static const vk_type sVks[8] = { 0xA2, 0xA3, 0xA0, 0xA1, 0xA4, 0xA5, 0x5B, 0x5C };
	for (int i = 0; i < 8; ++i)
		sModKc[i] = d ? LinuxKeycodeForVkEx(d, sVks[i]) : 0;
	// Seed the held-sides state from the current keymap (modifiers pressed
	// before the observer started).
	sHeldLR = 0;
	if (d)
	{
		char keys[32];
		memset(keys, 0, sizeof(keys));
		XQueryKeymap(d, keys);
		for (int i = 0; i < 8; ++i)
			if (sModKc[i] && (keys[sModKc[i] >> 3] & (1 << (sModKc[i] & 7))))
				LinuxTrackModifierKeycode(sModKc[i], true);
	}
}

void LinuxXI2Init(Display *d)
{
	int major = 2, minor = 0;
	int ev = 0, err = 0;
	if (d && XQueryExtension(d, "XInputExtension", &sXI2Opcode, &ev, &err)
		&& XIQueryVersion(d, &major, &minor) == Success)
	{
		unsigned char mask_data[XIMaskLen(XI_RawKeyRelease)];
		memset(mask_data, 0, sizeof(mask_data));
		XISetMask(mask_data, XI_RawKeyPress);
		XISetMask(mask_data, XI_RawKeyRelease);
		XIEventMask mask;
		mask.deviceid = XIAllMasterDevices;
		mask.mask_len = sizeof(mask_data);
		mask.mask = mask_data;
		XISelectEvents(d, DefaultRootWindow(d), &mask, 1);
		sXI2Observer = true;
	}
}

// Left/right state of the modifiers held when the event was generated.
// With the XI2 observer this is the tracked physical state (raw events
// precede the grabbed event for the same physical input, so the tracker is
// consistent even for batched XTEST input); without it, the current
// physical keymap is queried.
modLR_type LinuxEventLR(Display *d, unsigned int aEvMods)
{
	if (sXI2Observer)
		return sHeldLR;
	modLR_type r = 0;
	char keys[32];
	memset(keys, 0, sizeof(keys));
	if (d)
		XQueryKeymap(d, keys);
	auto side = [&](vk_type aLeftVk, vk_type aRightVk, modLR_type aLM, modLR_type aRM, unsigned int aMask)
	{
		if (!(aEvMods & aMask))
			return;
		KeyCode lk = LinuxKeycodeForVkEx(d, aLeftVk);
		KeyCode rk = LinuxKeycodeForVkEx(d, aRightVk);
		bool ldown = lk && (keys[lk >> 3] & (1 << (lk & 7)));
		bool rdown = rk && (keys[rk >> 3] & (1 << (rk & 7)));
		if (ldown) r |= aLM;
		if (rdown) r |= aRM;
	};
	side(0xA0, 0xA1, MOD_LSHIFT, MOD_RSHIFT, ShiftMask);
	side(0xA2, 0xA3, MOD_LCONTROL, MOD_RCONTROL, ControlMask);
	side(0xA4, 0xA5, MOD_LALT, MOD_RALT, sAltMask);
	side(0x5B, 0x5C, MOD_LWIN, MOD_RWIN, sSuperMask);
	return r;
}

// Modifier matching (Windows semantics): the required neutral masks must be
// held, the held sides must lie inside the hotkey's consolidated LR set, and
// (unless wildcard) no extra primary modifier may be held.
bool LinuxHotkeyModsMatch(Hotkey *aHotkey, unsigned int aEvMods, modLR_type aEvLR)
{
	unsigned int req = LinuxModsToX(aHotkey->mModifiers);
	if ((aEvMods & req) != req)
		return false;
	if (!aHotkey->mAllowExtraModifiers)
	{
		if (aEvLR & ~aHotkey->mModifiersConsolidatedLR)
			return false;
		if (aEvMods & ~(req | LinuxModsToXLR(aHotkey->mModifiersLR)))
			return false;
	}
	return true;
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

// X11 button number used to grab/fire this mouse hotkey (0 = not a mouse
// hotkey).  Mouse hotkeys keep their Windows vks in mVK (VK_LBUTTON..,
// VK_WHEEL_*) because LinuxVkToKeysym maps them to NoSymbol.  X11 numbering
// is 1=left, 2=middle, 3=right, 4/5=wheel up/down, 6/7=wheel left/right,
// 8/9=x1/x2 (same as LinuxMouseButtonForVk in core_input_linux.cpp).
unsigned int LinuxHotkeyButton(Hotkey *aHotkey)
{
	switch (aHotkey->mVK)
	{
	case VK_LBUTTON:     return 1;
	case VK_MBUTTON:     return 2;
	case VK_RBUTTON:     return 3;
	case VK_WHEEL_UP:    return 4;
	case VK_WHEEL_DOWN:  return 5;
	case VK_WHEEL_LEFT:  return 6;
	case VK_WHEEL_RIGHT: return 7;
	case VK_XBUTTON1:    return 8;
	case VK_XBUTTON2:    return 9;
	}
	return 0;
}

// Build the desired grab set: every key+modifier combination (including the
// full lock-modifier power set) of every hotkey that has an enabled variant.
void LinuxBuildDesired(Display *d, std::set<GrabSpec> &aDesired)
{
	unsigned int lock_combos[8];
	int nlock = 1;
	lock_combos[0] = 0;
	for (int i = 0; i < sLockMaskCount && nlock < 8; ++i)
	{
		int n = nlock;
		for (int j = 0; j < n; ++j)
			lock_combos[nlock++] = lock_combos[j] | sLockMasks[i];
	}
	unsigned int prim[4] = { ControlMask, ShiftMask, sAltMask, sSuperMask };
	for (int i = 0; i < Hotkey::sHotkeyCount; ++i)
	{
		Hotkey *hk = Hotkey::shk[i];
		if (!hk || hk->mModifierVK || hk->IsCompletelyDisabled())
			continue; // Prefix hotkeys unsupported; disabled variants ungrabbed.
		KeyCode kc = LinuxHotkeyKeycode(d, hk);
		unsigned int btn = kc ? 0 : LinuxHotkeyButton(hk);
		if (!kc && !btn)
			continue; // Scan-code / prefix / unsupported: not grabbed.
		unsigned int base = LinuxModsToX(hk->mModifiers) | LinuxModsToXLR(hk->mModifiersLR);
		if (hk->mAllowExtraModifiers)
		{
			// Wildcard: the grab must activate under any additional primary
			// modifier, so grab every superset of the required combination
			// (up to 16 x lock combos per hotkey).
			for (int t = 0; t < 16; ++t)
			{
				unsigned int extra = 0;
				for (int p = 0; p < 4; ++p)
					if (t & (1 << p))
						extra |= prim[p];
				unsigned int combo = base | extra;
				for (int c = 0; c < nlock; ++c)
					aDesired.insert(GrabSpec{kc, btn, combo | lock_combos[c]});
			}
		}
		else
		{
			for (int c = 0; c < nlock; ++c)
				aDesired.insert(GrabSpec{kc, btn, base | lock_combos[c]});
		}
	}
}

// ---------------------------------------------------------------------------
// Hotkey hash index (unique-resolution lookup; rebuilt when grabs change)
// ---------------------------------------------------------------------------

// Map (id<<8)|mods -> hotkey indices, where id is the keycode or the X11
// button number and mods is the primary-only modifier mask (lock state is
// excluded, matching the event handlers' mask).
struct HotkeyIndex
{
	std::unordered_map<unsigned int, std::vector<int>> buckets;
};

HotkeyIndex sIndex;
bool sIndexDirty = true;

void LinuxIndexAdd(unsigned int aId, unsigned int aBase, bool aWildcard, const unsigned int aPrim[4], int aHotkeyIndex)
{
	if (aWildcard)
	{
		for (int t = 0; t < 16; ++t)
		{
			unsigned int extra = 0;
			for (int p = 0; p < 4; ++p)
				if (t & (1 << p))
					extra |= aPrim[p];
			sIndex.buckets[(aId << 8) | (aBase | extra)].push_back(aHotkeyIndex);
		}
	}
	else
		sIndex.buckets[(aId << 8) | aBase].push_back(aHotkeyIndex);
}

void LinuxBuildHotkeyIndex(Display *d)
{
	sIndex.buckets.clear();
	unsigned int prim[4] = { ControlMask, ShiftMask, sAltMask, sSuperMask };
	for (int i = 0; i < Hotkey::sHotkeyCount; ++i)
	{
		Hotkey *hk = Hotkey::shk[i];
		if (!hk || hk->mModifierVK || hk->IsCompletelyDisabled())
			continue;
		KeyCode kc = LinuxHotkeyKeycode(d, hk);
		unsigned int btn = kc ? 0 : LinuxHotkeyButton(hk);
		unsigned int id = btn ? btn : (unsigned int)kc;
		if (!id)
			continue;
		unsigned int base = LinuxModsToX(hk->mModifiers) | LinuxModsToXLR(hk->mModifiersLR);
		LinuxIndexAdd(id, base, hk->mAllowExtraModifiers != 0, prim, i);
	}
}

// Find the best matching hotkey+enabled variant for (id, mods, side, phase).
// Unique resolution: exact (non-wildcard) hotkeys beat wildcard ones; among
// equals the one allowing fewer side bits wins; ties keep registration order
// (the bucket is built in registration order).
void LinuxFindHotkey(Display *d, unsigned int aId, unsigned int aEvMods, modLR_type aEvLR, bool aIsUp
	, Hotkey *&aHk, HotkeyVariant *&aVp)
{
	aHk = nullptr;
	aVp = nullptr;
	std::unordered_map<unsigned int, std::vector<int>>::const_iterator it = sIndex.buckets.find((aId << 8) | aEvMods);
	if (it == sIndex.buckets.end())
		return;
	int best = 0x7FFFFFFF;
	for (size_t k = 0; k < it->second.size(); ++k)
	{
		int i = it->second[k];
		Hotkey *hk = Hotkey::shk[i];
		if (!hk || hk->mKeyUp != aIsUp || hk->mModifierVK)
			continue;
		if (!LinuxHotkeyModsMatch(hk, aEvMods, aEvLR))
			continue;
		HotkeyVariant *vp = hk->FindVariant();
		if (!vp || !vp->mEnabled || !hk->PerformIsAllowed(*vp))
			continue;
		int side_bits = 0;
		for (unsigned t = (unsigned)hk->mModifiersConsolidatedLR; t; t >>= 1)
			side_bits += (int)(t & 1);
		int score = (hk->mAllowExtraModifiers ? 8 : 0) + side_bits;
		if (score < best)
		{
			best = score;
			aHk = hk;
			aVp = vp;
		}
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
	bool is_button;    // Mouse button event (otherwise a key event).
	unsigned int id;   // KeyCode for keys, X11 button number for buttons.
	DWORD when;        // GetTickCount() at injection time.
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
	m = PassthruMark{false, (unsigned int)ev.xkey.keycode, GetTickCount(), ev.type == KeyRelease};
}

void LinuxButtonPassthrough(Display *d, unsigned int aButton, XEvent &ev)
{
	// X11 passive button grabs hand the whole press/release pair to the
	// grabbing client, and re-injecting a press with XTEST while the button
	// is still held is swallowed by the server (unlike keyboard repeat
	// delivery, which the key passthrough relies on).  Forwarding a press
	// therefore removes the passive grabs for the button (restored by the
	// next reconcile, which runs on every dispatch), releases the active
	// pointer grab, and re-injects an un-press/press pair; the window under
	// the pointer receives the click (with a harmless leading release it
	// never saw a press for).  The release phase is forwarded with a single
	// XTEST release.  No injection marks are needed: with the passive grabs
	// removed and the active grab released, nothing can be delivered back
	// to this connection.
	Window root = DefaultRootWindow(d);
	if (ev.type == ButtonPress)
	{
		for (std::set<GrabSpec>::iterator it = sInstalled.begin(); it != sInstalled.end();)
		{
			if (it->button == aButton)
			{
				XUngrabButton(d, it->button, it->modifiers, root);
				sInstalled.erase(it++);
			}
			else
				++it;
		}
		XUngrabPointer(d, CurrentTime);
		XTestFakeButtonEvent(d, aButton, False, CurrentTime);
		XTestFakeButtonEvent(d, aButton, True, CurrentTime);
		XFlush(d);
	}
	else
	{
		XUngrabPointer(d, CurrentTime);
		XTestFakeButtonEvent(d, aButton, False, CurrentTime);
		XFlush(d);
	}
}

bool LinuxIsPassthruCopy(XEvent &ev)
{
	bool is_button = ev.type == ButtonPress || ev.type == ButtonRelease;
	bool is_up = is_button ? ev.type == ButtonRelease : ev.type == KeyRelease;
	unsigned int id = is_button ? (unsigned int)ev.xbutton.button : (unsigned int)ev.xkey.keycode;
	DWORD now = GetTickCount();
	// A generous window (1 s) tolerates slow servers/parallel-connection
	// reordering; repeated real input of the same key/button/phase within it
	// is rare for pass-through hotkeys and the cost of a missed copy is a
	// single swallowed event, while a missed match here would re-inject and
	// loop.
	for (int i = 0; i < _countof(sPassthruLog); ++i)
		if (sPassthruLog[i].is_button == is_button
			&& sPassthruLog[i].id == id
			&& sPassthruLog[i].is_up == is_up
			&& now - sPassthruLog[i].when < 1000)
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
	modLR_type evlr = LinuxEventLR(d, evmods);

	Hotkey *hk_fire = nullptr;
	HotkeyVariant *vp_fire = nullptr;
	LinuxFindHotkey(d, (unsigned int)ev.xkey.keycode, evmods, evlr, is_up, hk_fire, vp_fire);
	// Key-up hotkeys without XKB repeat suppression: a synthetic repeat
	// release is a repeat artifact, not a physical release.
	if (hk_fire && is_up && !sDetectableAutoRepeat && LinuxIsSyntheticRelease(ev))
		hk_fire = nullptr, vp_fire = nullptr;

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

// True if some enabled, fireable button-up variant exists for this combo.
// Used by the press-phase decision: when an up variant exists, the press
// must keep the grab alive (i.e. be consumed) so the release is delivered
// to this connection and the up variant can fire; on Windows the press
// would be passed through to the target application, but X11 passive grabs
// cannot split the press/release pair (documented deviation).
bool LinuxButtonUpVariantExists(Display *d, unsigned int aButton, unsigned int aEvMods)
{
	modLR_type evlr = LinuxEventLR(d, aEvMods);
	std::unordered_map<unsigned int, std::vector<int>>::const_iterator it = sIndex.buckets.find((aButton << 8) | aEvMods);
	if (it == sIndex.buckets.end())
		return false;
	for (size_t k = 0; k < it->second.size(); ++k)
	{
		Hotkey *hk = Hotkey::shk[it->second[k]];
		if (!hk || !hk->mKeyUp || hk->mModifierVK)
			continue;
		if (!LinuxHotkeyModsMatch(hk, aEvMods, evlr))
			continue;
		HotkeyVariant *vp = hk->FindVariant();
		if (vp && vp->mEnabled && hk->PerformIsAllowed(*vp))
			return true;
	}
	return false;
}

void LinuxHandleButtonEvent(Display *d, XEvent &ev)
{
	bool is_up = ev.type == ButtonRelease;
	unsigned int button = (unsigned int)ev.xbutton.button;

	// The grab may activate on any lock state; the event's state then
	// carries those bits.  Compare only the primary modifier slots (the
	// button state bits of the pressed button itself are excluded, like
	// Windows, where other held buttons do not affect matching).
	unsigned int evmods = ev.xbutton.state & (ControlMask | ShiftMask | sAltMask | sSuperMask);
	modLR_type evlr = LinuxEventLR(d, evmods);

	Hotkey *hk_fire = nullptr;
	HotkeyVariant *vp_fire = nullptr;
	LinuxFindHotkey(d, button, evmods, evlr, is_up, hk_fire, vp_fire);

	bool passthrough;
	if (hk_fire)
	{
		// Consume the event unless the hotkey is a pass-through (~).
		bool suppress = !((hk_fire->mNoSuppress & (NO_SUPPRESS_PREFIX | AT_LEAST_ONE_VARIANT_HAS_TILDE))
			|| (vp_fire->mNoSuppress & (NO_SUPPRESS_PREFIX | AT_LEAST_ONE_VARIANT_HAS_TILDE)));
		passthrough = !suppress;
	}
	else
	{
		// No variant fires.  Releases (delivered while the grab is still
		// active, e.g. after a suppressed down-only hotkey) are forwarded so
		// the target application never sees a stuck button.  For the press
		// phase, keep the grab alive only when an enabled button-up variant
		// exists for this combo (so the release reaches us and the up
		// variant can fire); otherwise forward the click (HotIf false,
		// Suspend, thread limits).
		passthrough = is_up || !LinuxButtonUpVariantExists(d, button, evmods);
	}

	if (passthrough)
		LinuxButtonPassthrough(d, button, ev);

	if (hk_fire)
	{
		++g_nThreads;
		++g;
		InitNewThread(vp_fire->mPriority, false, false);
		hk_fire->PerformInNewThreadMadeByCaller(*vp_fire);
		ResumeUnderlyingThread();
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
	LinuxUpdateModifierKeycodes(d);
	LinuxXI2Init(d);
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
	bool changed = false;

	// Ungrab combinations that are no longer desired.
	for (std::set<GrabSpec>::iterator it = sInstalled.begin(); it != sInstalled.end();)
	{
		if (!desired.count(*it))
		{
			if (it->button)
				XUngrabButton(d, it->button, it->modifiers, root);
			else
				XUngrabKey(d, it->keycode, it->modifiers, root);
			sInstalled.erase(it++);
			changed = true;
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
	sLastConflictName[0] = _T(' ');
	{
		ScopedXErrorTrap trap(d);
		for (std::set<GrabSpec>::iterator it = desired.begin(); it != desired.end(); ++it)
		{
						if (sInstalled.count(*it))
				continue;
			unsigned long serial = (unsigned long)XNextRequest(d) - 1;
			if (it->button)
				XGrabButton(d, it->button, it->modifiers, root, False
					, ButtonPressMask | ButtonReleaseMask, GrabModeAsync, GrabModeAsync, None, None);
			else
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
					if (pending[i].spec.button)
						_sntprintf(sLastConflictName, _countof(sLastConflictName), _T("mouse button %u (modifiers %X)")
							, pending[i].spec.button, pending[i].spec.modifiers);
					else
					{
						KeySym ks = XkbKeycodeToKeysym(d, pending[i].spec.keycode, 0, 0);
						_sntprintf(sLastConflictName, _countof(sLastConflictName), _T("%s (modifiers %X)")
							, ks ? XKeysymToString(ks) : _T("key"), pending[i].spec.modifiers);
					}
					break;
				}
			}
		}
	}

	// XSync inside the trap destructor flushed the requests above; the
	// conflict-free grabs are now installed.
	for (size_t i = 0; i < pending.size(); ++i)
		sInstalled.insert(pending[i].spec);
	if (changed || !pending.empty())
		sIndexDirty = true;
}

void LinuxDispatchHotkeys()
{
	Display *d = LinuxHotkeyDisplay();
	if (!d)
		return;
	LinuxReconcileHotkeyGrabs();
	if (sIndexDirty)
	{
		LinuxBuildHotkeyIndex(d);
		sIndexDirty = false;
	}
	// Bound the number of events processed per dispatch: a passthrough
	// re-injection loop (should be prevented by the injection log, but a
	// hostile/key-repeat or slow-server timing could still produce one)
	// must not starve the wait loops that check the restart flag.
	int processed = 0;
	while (XPending(d) > 0 && processed < 256)
	{
		if (LinuxRestartRequested())
			return;
		XEvent ev;
		XNextEvent(d, &ev);
		++processed;
		switch (ev.type)
		{
		case KeyPress:
		case KeyRelease:
			LinuxHandleKeyEvent(d, ev);
			break;
		case ButtonPress:
		case ButtonRelease:
			LinuxHandleButtonEvent(d, ev);
			break;
		case GenericEvent:
		{
			XGenericEventCookie *cookie = &ev.xcookie;
			if (XGetEventData(d, cookie))
			{
				if (cookie->extension == sXI2Opcode && cookie->evtype == XI_RawKeyPress
					|| cookie->extension == sXI2Opcode && cookie->evtype == XI_RawKeyRelease)
				{
					XIRawEvent *re = (XIRawEvent *)cookie->data;
					LinuxTrackModifierKeycode((KeyCode)re->detail, cookie->evtype == XI_RawKeyPress);
				}
				XFreeEventData(d, cookie);
			}
			break;
		}
		case MappingNotify:
			// Keyboard map/layout changed: refresh the modifier slots and
			// rebuild every grab from scratch.
			XRefreshKeyboardMapping(&ev.xmapping);
			LinuxUpdateModifierMap(d);
			LinuxUpdateModifierKeycodes(d);
			sIndexDirty = true;
			{
				Window root = DefaultRootWindow(d);
				for (std::set<GrabSpec>::iterator it = sInstalled.begin(); it != sInstalled.end(); ++it)
				{
					if (it->button)
						XUngrabButton(d, it->button, it->modifiers, root);
					else
						XUngrabKey(d, it->keycode, it->modifiers, root);
				}
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