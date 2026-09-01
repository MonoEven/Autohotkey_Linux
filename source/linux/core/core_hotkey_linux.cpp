// Linux hotkey activation (round 28 rewrite; see audits/check0818.md).
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
// The upstream Hotkey() parse/variant machinery is unchanged. Explicit set-1
// scan codes are normalized to evdev and X keycodes by core_keymodel_linux;
// custom "A & B" prefix combos still require their dedicated state machine
// and are rejected rather than silently misbehaving.
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
#include "core_keymodel_linux.h"
#include "core_inputd_client_linux.h"
#include "core_capture_linux.h"
#include "core_gshortcut_linux.h"
#include "input_backend.h"
#include "input_event.h"
#include "input_pipeline.h"
#include "input_semantics.h" // unified synthetic-level policy (check0901 P0-2).
#include <X11/Xlib.h>
#include <X11/Xatom.h>
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

// Copy-suppression check for re-injected events (defined at file scope
// in the public section below; the anonymous-namespace handlers need it).
bool LinuxIsPassthruCopy(XEvent &ev);

// Self-injection lookup (defined below; the event handler needs it before).
static bool LinuxSelfLookup(XEvent &ev, int &aLevel, bool &aIsSendInput, bool &aIsSendPlay);

namespace {

uint64_t sX11PipelineSeq = 0;

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

std::set<GrabSpec> sInstalled; // GrabSpec lives in core_hotkey_linux.h.
// Desired grabs which received BadAccess.  They are deliberately distinct
// from sInstalled and retried while the script stays alive.
std::set<GrabSpec> sConflicted;
DWORD sNextConflictRetry = 0;
constexpr DWORD CONFLICT_RETRY_MS = 1000;

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
unsigned int sRawModMaskByKeycode[256] = {0};
unsigned int sRawDepressedMods = 0;
unsigned int sRawLockedMods = 0;
unsigned int sRawGroup = 0;

void LinuxUpdateModifierMap(Display *d)
{
	unsigned int lock[8];
	int nlock = 0;
	unsigned int alt = 0, super = 0;
	memset(sRawModMaskByKeycode, 0, sizeof(sRawModMaskByKeycode));
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
				sRawModMaskByKeycode[(unsigned int)kc] |= mask;
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
	XkbStateRec state;
	if (d && XkbGetState(d, XkbUseCoreKbd, &state) == Success)
	{
		sRawDepressedMods = state.base_mods;
		sRawLockedMods = state.locked_mods;
		sRawGroup = state.group;
	}
	else
		sRawDepressedMods = sRawLockedMods = sRawGroup = 0;
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
// True when XI 2.1 was negotiated (XIRawEvent.sourceid is only valid from
// 2.1; see check_detail0821 §2.2-A).
bool sXI2HasSourceId = false;
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
	// Request XI 2.1: XIRawEvent.sourceid is a historical bug and reads 0 on
	// XI 2.0 servers (check_detail0821 §2.2-A).  The server echoes back the
	// highest version it supports; sourceid-based detection needs >= 2.1 and
	// falls back to the time-window heuristics otherwise.
	int major = 2, minor = 1;
	int ev = 0, err = 0;
	if (d && XQueryExtension(d, "XInputExtension", &sXI2Opcode, &ev, &err)
		&& XIQueryVersion(d, &major, &minor) == Success)
	{
		sXI2HasSourceId = (major > 2 || (major == 2 && minor >= 1));
		unsigned char mask_data[XIMaskLen(XI_RawKeyRelease)];
		memset(mask_data, 0, sizeof(mask_data));
		XISetMask(mask_data, XI_RawKeyPress);
		XISetMask(mask_data, XI_RawKeyRelease);
		XIEventMask mask;
		mask.deviceid = XIAllMasterDevices;
		mask.mask_len = sizeof(mask_data);
		mask.mask = mask_data;
		XISelectEvents(d, DefaultRootWindow(d), &mask, 1);
		// XI_HierarchyChanged must be selected on XIAllDevices: selecting it
		// on the master set raises BadValue (minor 46) on Xorg/Xvfb.
		unsigned char hier_mask[XIMaskLen(XI_HierarchyChanged)];
		memset(hier_mask, 0, sizeof(hier_mask));
		XISetMask(hier_mask, XI_HierarchyChanged);
		XIEventMask hier;
		hier.deviceid = XIAllDevices;
		hier.mask_len = sizeof(hier_mask);
		hier.mask = hier_mask;
		XISelectEvents(d, DefaultRootWindow(d), &hier, 1);
		sXI2Observer = true;
		LinuxXI2EnumXTest(d);
	}
}

// (The XTEST device detection + raw-event source tap live at file scope,
// after the anonymous namespace below, so core_platform_stubs.cpp's --diag
// can query them through core_hotkey_linux.h.)

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

std::vector<TrapRecord> sTrapRecords;
Display *sTrapDpy = nullptr;
XErrorHandler sTrapPrev = nullptr;

int LinuxErrorTrapHandler(Display *d, XErrorEvent *e)
{
	if (d == sTrapDpy && e->error_code == BadAccess)
		sTrapRecords.push_back(TrapRecord{e->serial, BadAccess});
	return 0;
}

class ScopedXErrorTrap
{
public:
	explicit ScopedXErrorTrap(Display *d)
		: mD(d)
	{
		sTrapDpy = d;
		sTrapRecords.clear();
		sTrapPrev = XSetErrorHandler(LinuxErrorTrapHandler);
	}
	~ScopedXErrorTrap()
	{
		XSync(mD, False); // Flush so any BadAccess reaches the trap.
		XSetErrorHandler(sTrapPrev);
	}
	bool HasBadAccessFor(unsigned long aSerial) const
	{
		for (const TrapRecord &record : sTrapRecords)
			if (record.code == BadAccess && record.serial == aSerial)
				return true;
		return false;
	}

private:
	Display *mD;
};

// Keycode used to grab/fire this hotkey (0 = unsupported on X11).
KeyCode LinuxHotkeyKeycode(Display *d, Hotkey *aHotkey)
{
	if (aHotkey->mVK)
		return LinuxKeycodeForVkEx(d, aHotkey->mVK);
	if (aHotkey->mSC)
		return LinuxX11KeycodeForScanCode(aHotkey->mSC);
	return 0;
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
		if (!hk || hk->mModifierVK || hk->mModifierSC || hk->IsCompletelyDisabled()
			|| !LinuxInputBackendHotkeyAssigned(hk, AhkInputBackendKind::X11))
			continue; // Other mux lanes own their registrations.
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
		if (!hk || hk->mModifierVK || hk->mModifierSC || hk->IsCompletelyDisabled()
			|| !LinuxInputBackendHotkeyAssigned(hk, AhkInputBackendKind::X11))
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

// True if an enabled, currently fireable keyboard-up variant owns this press.
// A passive X grab must retain the press so the corresponding release is sent
// back to this connection; forwarding the press first loses the release.
bool LinuxKeyUpVariantExists(unsigned int aKeycode,
	unsigned int aEvMods, modLR_type aEvLR)
{
	std::unordered_map<unsigned int, std::vector<int>>::const_iterator it
		= sIndex.buckets.find((aKeycode << 8) | aEvMods);
	if (it == sIndex.buckets.end())
		return false;
	for (size_t k = 0; k < it->second.size(); ++k)
	{
		Hotkey *hk = Hotkey::shk[it->second[k]];
		if (!hk || !hk->mKeyUp || hk->mModifierVK || hk->mModifierSC)
			continue;
		if (!LinuxHotkeyModsMatch(hk, aEvMods, aEvLR))
			continue;
		HotkeyVariant *vp = hk->FindVariant();
		// Press-side ownership must not depend on the current thread being
		// interruptible or below MaxThreads. Re-evaluate PerformIsAllowed when
		// the actual release arrives; dropping the grab here loses it forever.
		if (vp && vp->mEnabled)
			return true;
	}
	return false;
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

// Passthrough re-injection and copy suppression are implemented at file
// scope below (so core_capture_linux.cpp can call LinuxInjectKey).


void LinuxButtonPassthrough(Display *d, unsigned int aButton, XEvent &ev)
{
	// X11 passive button grabs hand the whole press/release pair to the
	// grabbing client, and re-injecting a press with XTEST while the button
	// is still held is swallowed by the server (unlike keyboard repeat
	// delivery, which the key passthrough relies on).  Forwarding a press
	// therefore removes the passive grabs for the button (restored by the
	// next reconcile), releases the active pointer grab, and re-injects an
	// un-press/press pair; the window under the pointer receives the click
	// (with a harmless leading release it never saw a press for).  The
	// release phase is forwarded with a single XTEST release.  No injection
	// marks are needed: with the passive grabs removed and the active grab
	// released, nothing can be delivered back to this connection.
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
		// The removed grabs must be restored by the next (lazy) reconcile.
		LinuxSetReconcileDirty();
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

// Track A_ThisHotkey / A_PriorHotkey (+ the *_StartTime timestamps) before
// firing a hotkey callback, mirroring application.cpp's WM_AHK_HOTKEY handling
// (check_detail0821 §5).
static void LinuxTrackHotkey(Hotkey *aHotkey)
{
	g_script.mPriorHotkeyName = g_script.mThisHotkeyName;
	g_script.mPriorHotkeyStartTime = g_script.mThisHotkeyStartTime;
	g_script.mThisHotkeyName = aHotkey->mName;
	g_script.mThisHotkeyStartTime = GetTickCount();
}

void LinuxHandleKeyEvent(Display *d, XEvent &ev)
{
	sPrevKeyCode = ev.xkey.keycode;
	sPrevTime = ev.xkey.time;
	sPrevWasPress = ev.type == KeyPress;

	// Suppress the re-grabbed copy of a passthrough-injected event (the
	// target already received the injected one).
	// §3 (check_detail0821 §2.2-A / S5): the raw-event tap classifies a
	// grabbed event's source.  A PHYSICAL event is a real press and can never
	// be the stale copy of an injection, so its passthru/self marks must not
	// be consulted (a stale mark must not swallow a real repeat).  Under
	// Xvfb every event is XTEST, so this never fires there; a tap miss
	// (unknown) falls back to the time-window heuristics below.
	bool is_physical = LinuxXTestTapClassify((unsigned int)ev.xkey.keycode, ev.type == KeyPress) == 0;
	int self_level = -1;
	bool self_sendinput = false;
	bool self_sendplay = false;
	bool self_injected = false;
	if (is_physical)
	{
		// Physical real press: normal handling, no suppression checks.
	}
	else
	{
		if (LinuxIsPassthruCopy(ev))
			return;
		// Self-injection (Send/SendEvent/SendInput/SendPlay/SendText)?
		// SendInput and SendPlay copies must not fire own hotkeys/hotstrings
		// (Windows unloads its own hook during SendInput and SendPlay uses
		// the journal), but the injected event itself must still reach the
		// focused window: X11 passive grabs redirect the injected event to
		// this handler, so re-inject it once (the passthru mark drops the
		// re-grabbed copy on the next round).  The remaining self-sent
		// copies are level-gated by #InputLevel / InputHook MinSendLevel via
		// input_semantics.h (check0901 P0-2 / check_detail0901 §2).
		self_injected = LinuxSelfLookup(ev, self_level, self_sendinput, self_sendplay);
		if (self_injected && (self_sendinput || self_sendplay))
		{
			LinuxInjectKey(d, ev);
			return;
		}
	}

	bool is_up = ev.type == KeyRelease;
	unsigned int evmods = ev.xkey.state & (ControlMask | ShiftMask | sAltMask | sSuperMask);
	modLR_type evlr = LinuxEventLR(d, evmods);
	AhkLinuxKeyIdentity pipeline_identity;
	memset(&pipeline_identity, 0, sizeof(pipeline_identity));
	if (!LinuxKeyModelX11Decode(d, (KeyCode)ev.xkey.keycode,
			ev.xkey.state, pipeline_identity))
	{
		pipeline_identity.evdev_code = ev.xkey.keycode >= 8
			? (uint32_t)ev.xkey.keycode - 8 : 0;
		pipeline_identity.sc = LinuxScanCodeForEvdev(pipeline_identity.evdev_code);
		pipeline_identity.keysym = XLookupKeysym(&ev.xkey, 0);
		pipeline_identity.vk = LinuxKeysymToVk(pipeline_identity.keysym);
	}
	AhkInputSource pipeline_source = is_physical ? AhkInputSource::PHYSICAL
		: (self_injected ? AhkInputSource::SELF_INJECT
			: AhkInputSource::OTHER_INJECT);
	bool pipeline_repeat_release = is_up && !sDetectableAutoRepeat
		&& LinuxIsSyntheticRelease(ev);
	AhkInputEvent pipeline_event = {
		LinuxInputEventMonotonicUs(), pipeline_identity.evdev_code,
		pipeline_identity.vk, pipeline_identity.sc,
		(char32_t)pipeline_identity.text, is_up, pipeline_repeat_release,
		pipeline_source, (int16_t)(self_injected ? self_level : -1), 0,
		AhkInputOrigin::X11
	};
	unsigned int pipeline_mods = (ev.xkey.state & ShiftMask ? MOD_SHIFT : 0)
		| (ev.xkey.state & ControlMask ? MOD_CONTROL : 0)
		| (ev.xkey.state & sAltMask ? MOD_ALT : 0)
		| (ev.xkey.state & sSuperMask ? MOD_WIN : 0);
	AhkInputContext pipeline_context = {AhkInputBackendKind::X11,
		AhkInputSourceDomain::X11_GRAB,
		is_physical ? AhkProvenanceConfidence::DEVICE_DERIVED
			: (self_injected ? AhkProvenanceConfidence::TIME_CORRELATED
				: AhkProvenanceConfidence::UNKNOWN),
		1, ++sX11PipelineSeq, 0, 0, 0, 0,
		pipeline_mods, evlr, true, is_physical};
	AhkInputAcceptance pipeline_accepted = LinuxInputPipelineAccept(
		pipeline_event, pipeline_context);
	AhkInputMatch pipeline_match;
	AhkInputDecision pipeline_decision;
	bool pipeline_found = LinuxInputPipelineMatchSingleHotkey(
		pipeline_accepted, AhkInputBackendKind::X11, pipeline_match,
		pipeline_decision, true);

	// The grab may activate on any lock state; the event's state then
	// carries those bits. Generic and LR snapshots were normalized above.
	Hotkey *hk_fire = nullptr;
	HotkeyVariant *vp_fire = nullptr;
	LinuxFindHotkey(d, (unsigned int)ev.xkey.keycode, evmods, evlr, is_up, hk_fire, vp_fire);
	// Key-up hotkeys without XKB repeat suppression: a synthetic repeat
	// release is a repeat artifact, not a physical release.
	if (hk_fire && is_up && !sDetectableAutoRepeat && LinuxIsSyntheticRelease(ev))
		hk_fire = nullptr, vp_fire = nullptr;
	// #InputLevel gate (official HotInputLevelAllowsFiring): a synthetic
	// event triggers only when send_level > input_level; equal levels do
	// NOT fire, and physical/non-AHK input (level < 0) is never filtered.
	if (hk_fire && self_injected
		&& !AhkSyntheticMayTrigger(AhkConsumerKind::HOTKEY
			, AhkSendTransportClass::EVENT, self_level
			, (int)vp_fire->mInputLevel))
		hk_fire = nullptr, vp_fire = nullptr;

	LinuxInputPipelineTraceLegacyComparison(pipeline_accepted,
		pipeline_found ? &pipeline_match : nullptr, hk_fire, vp_fire);
	bool pipeline_handled = pipeline_found
		|| pipeline_decision.reason == AhkInputDecisionReason::KEYUP_OWNERSHIP
		|| pipeline_decision.action == AhkInputDecisionAction::DUPLICATE_IGNORED;
	if (LinuxInputPipelineActive() && pipeline_handled)
	{
		LinuxInputPipelineTraceDecision(pipeline_accepted, pipeline_decision);
		if (pipeline_decision.action == AhkInputDecisionAction::TRIGGER_PASS
			|| pipeline_decision.action == AhkInputDecisionAction::PASS_ORIGINAL)
			LinuxInjectKey(d, ev);
		if (pipeline_found)
			LinuxInputPipelineDispatch(pipeline_accepted, pipeline_match,
				pipeline_decision);
		const char *outcome = pipeline_decision.action
			== AhkInputDecisionAction::DUPLICATE_IGNORED ? "duplicate_ignored"
			: (pipeline_found
				? (pipeline_decision.action == AhkInputDecisionAction::TRIGGER_PASS
					? "triggered_pass" : "triggered_suppressed")
				: (pipeline_decision.action == AhkInputDecisionAction::SUPPRESS_ORIGINAL
					? "keyup_owned_suppressed" : "keyup_owned_pass"));
		LinuxInputPipelineTraceOutcome(pipeline_accepted, pipeline_decision,
			outcome);
		return;
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
		// Windows: every hotkey/hotstring thread starts with SendLevel equal
		// to the hotkey's InputLevel (official SendLevel docs).  Set it AFTER
		// InitNewThread (which resets thread settings to the auto-execute
		// defaults); capture the underlying thread's SendLevel BEFORE ++g
		// (the reused slot's stale settings must never leak back).
		SendLevelType saved_send_level = g->SendLevel;
		++g_nThreads;
		++g;
		InitNewThread(vp_fire->mPriority, false, false);
		g->SendLevel = vp_fire->mInputLevel;
		LinuxTrackHotkey(hk_fire);
		hk_fire->PerformInNewThreadMadeByCaller(*vp_fire);
		ResumeUnderlyingThread();
		g->SendLevel = saved_send_level;
	}
	else
	{
		// For a key-up hotkey, retain the press-side passive grab so X sends us
		// the release. This mirrors the established button-up path below.
		if (!is_up && LinuxKeyUpVariantExists(
			(unsigned int)ev.xkey.keycode, evmods, evlr))
			return;
		// No variant may fire (HotIf false, Suspend, thread limits, or this
		// event belongs to a pass-through/key-up hotkey's non-firing phase):
		// give the typed-text capture engine a chance to hold/consume it
		// (hotstrings / InputHook, level-gated by MinSendLevel), then re-inject
		// so the normal target window receives it.  Self-injected (Send/
		// SendEvent/etc.) events that did not fire are handled the same way --
		// the event passes through to the target; only the InputHook's
		// MinSendLevel filters it.  (X11 passive grabs hand the event to us;
		// passthrough is done by releasing the active keyboard grab and
		// re-injecting with XTEST -- check0818 P0-3, documented deviation
		// from ReplayKeyboard.)
		if (LinuxCaptureKeyEvent(d, ev, self_level))
			return;
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
		// Windows: the button hotkey thread starts at its InputLevel (set
		// after InitNewThread resets thread settings; capture the underlying
		// thread's SendLevel before ++g).
		SendLevelType saved_send_level = g->SendLevel;
		++g_nThreads;
		++g;
		InitNewThread(vp_fire->mPriority, false, false);
		g->SendLevel = vp_fire->mInputLevel;
		LinuxTrackHotkey(hk_fire);
		hk_fire->PerformInNewThreadMadeByCaller(*vp_fire);
		ResumeUnderlyingThread();
		g->SendLevel = saved_send_level;
	}
}

} // namespace

// ---------------------------------------------------------------------------
// XTEST device detection + raw-event source tap (check_detail0821 §2.2-A / §3)
// ---------------------------------------------------------------------------
// The X server gives the two XTEST devices ("Virtual core XTEST keyboard" /
// "pointer") the "XTEST Device" property (XI_PROP_XTEST_DEVICE = 1), and every
// XTestFakeKeyEvent-produced event carries them as its XIRawEvent.sourceid.
// The raw-event stream is therefore a definitive "injected vs physical"
// classifier: a grabbed event whose raw tap says PHYSICAL is a real press and
// can never be a stale copy of an injection (fixes S5's time-window swallow).
// Requires XI 2.1 for a valid sourceid; otherwise the tap is disabled and the
// time-window heuristics stay in charge.
#ifndef XI_PROP_XTEST_DEVICE
#define XI_PROP_XTEST_DEVICE "XTEST Device"
#endif

static int sXTestDevices[8];
static int sXTestDeviceCount = 0;

void LinuxXI2EnumXTest(Display *d)
{
	sXTestDeviceCount = 0;
	if (!d || !sXI2HasSourceId)
		return;
	Atom prop = XInternAtom(d, XI_PROP_XTEST_DEVICE, True);
	if (prop == None)
		return;
	int ndevs = 0;
	XIDeviceInfo *devs = XIQueryDevice(d, XIAllDevices, &ndevs);
	for (int i = 0; i < ndevs && sXTestDeviceCount < (int)_countof(sXTestDevices); ++i)
	{
		Atom type; int fmt; unsigned long nitems = 0, bytes_after = 0;
		unsigned char *data = nullptr;
		if (XIGetProperty(d, devs[i].deviceid, prop, 0, 1, False, XA_INTEGER
			, &type, &fmt, &nitems, &bytes_after, &data) == Success
			&& data && nitems >= 1
			&& ((fmt == 8 && data[0] == 1) || (fmt == 32 && *(long *)data == 1)))
			sXTestDevices[sXTestDeviceCount++] = devs[i].deviceid;
		if (data)
			XFree(data);
	}
	XIFreeDeviceInfo(devs);
}

// True when the raw event's source device is one of the XTEST devices.
bool LinuxIsXTestDevice(int aSourceId)
{
	for (int i = 0; i < sXTestDeviceCount; ++i)
		if (sXTestDevices[i] == aSourceId)
			return true;
	return false;
}

// Ring of the most recent raw key events: {keycode, phase, is_xtest}.  The
// raw event for a key arrives on the same connection BEFORE its processed
// (grabbed) counterpart, so a grabbed event can be classified by the most
// recent matching raw record.  A miss (ring overflow / no XI 2.1) is UNKNOWN
// and falls back to the time-window heuristics.
struct RawTap { unsigned int kc; bool up; bool is_xtest; };
static RawTap sRawTap[1024];
static int sRawTapHead = 0;

void LinuxXTestTapRecord(unsigned int aKeycode, bool aIsPress, bool aIsXTest)
{
	RawTap &t = sRawTap[sRawTapHead++ % _countof(sRawTap)];
	t.kc = aKeycode;
	t.up = !aIsPress;
	t.is_xtest = aIsXTest;
}

// Consume the most recent raw tap for {keycode, phase}.  Returns 1 = XTEST
// (injected), 0 = PHYSICAL (real), -1 = unknown (no record).
int LinuxXTestTapClassify(unsigned int aKeycode, bool aIsPress)
{
	bool up = !aIsPress;
	for (int i = 0; i < (int)_countof(sRawTap); ++i)
	{
		RawTap &t = sRawTap[(sRawTapHead - 1 - i + _countof(sRawTap)) % _countof(sRawTap)];
		if (t.kc == aKeycode && t.up == up)
		{
			t.kc = 0; // Consume: only the first matching grabbed event matches.
			return t.is_xtest ? 1 : 0;
		}
	}
	return -1;
}

// For --diag: the first XTEST device id (0 when none), and whether the
// sourceid tap is active.
int LinuxXTestPrimaryDeviceId()
{
	return sXTestDeviceCount > 0 ? sXTestDevices[0] : 0;
}
bool LinuxXI2SourceIdActive()
{
	return sXI2Observer && sXI2HasSourceId && sXTestDeviceCount > 0;
}

// One-shot probe for --diag: negotiate XI 2.1 and enumerate the XTEST
// devices WITHOUT subscribing to the raw-event stream (the diagnostic runs
// before the hotkey observer is initialized).  Returns true when at least
// one XTEST device was found under a >= 2.1 server.
bool LinuxXI2Probe(Display *d)
{
	if (!d)
		return false;
	int major = 2, minor = 1, ev = 0, err = 0;
	if (XQueryExtension(d, "XInputExtension", &sXI2Opcode, &ev, &err)
		&& XIQueryVersion(d, &major, &minor) == Success)
	{
		sXI2HasSourceId = (major > 2 || (major == 2 && minor >= 1));
		LinuxXI2EnumXTest(d);
		return sXTestDeviceCount > 0;
	}
	return false;
}

// ---------------------------------------------------------------------------
// Passthrough re-injection (file scope: used by the capture engine)
// ---------------------------------------------------------------------------
// When a grabbed combination must be passed through (tilde, HotIf false,
// key-up hotkey's non-firing phase, or any typed key while hotstrings are
// active), the event is re-injected with XTEST after releasing the active
// keyboard grab (the passive grabs stay installed).  On servers where the
// injected event re-activates the passive grab, the copy comes back to us;
// it is suppressed through a short log keyed on the SERVER time of the
// injected event -- the copy is the same server event (same time), while a
// real repeat has a later time, so typed text (which re-injects every key)
// is never swallowed.  This is the documented X11 equivalent of
// ReplayKeyboard (check0818 P0-3; sync grabs were rejected because frozen
// events never notify the grabbing client, which deadlocks).
struct PassthruMark
{
	unsigned int id;   // KeyCode.
	DWORD when;        // GetTickCount() just before injection (expiry base).
	bool is_up;
	bool consumed;     // Already matched the injected copy; see below.
};
PassthruMark sPassthruLog[8];
int sPassthruHead = 0;

// The copy is the FIRST delivered event for the same keycode/phase after
// the injection (the server processes requests in order), so each mark
// matches ONCE and is then consumed: a genuine repeat from the user --
// double letters "ll"/"oo", held-key auto-repeat, double-taps -- is never
// swallowed by a stale suppression record.  The window only bounds the
// case where the injected event never comes back (some servers do not
// re-activate the passive grab); a consumed mark can never swallow a
// SECOND real press, so the window is kept generous for slow servers
// (check0820 P0/P1).
#define PASSTHRU_COPY_MS_WINDOW 1000

void LinuxInjectKey(Display *d, XEvent &ev)
{
	XUngrabKeyboard(d, CurrentTime);
	XTestFakeKeyEvent(d, ev.xkey.keycode, ev.type == KeyPress ? True : False, CurrentTime);
	XFlush(d);
	PassthruMark &m = sPassthruLog[sPassthruHead++ % _countof(sPassthruLog)];
	m = PassthruMark{(unsigned int)ev.xkey.keycode, GetTickCount(), ev.type == KeyRelease, false};
}

// Inject a raw keycode with a copy-suppression mark (used by the typed-text
// capture engine for forwarded text and hotstring replacements, whose
// re-grabbed copies must not re-enter the engine).
void LinuxInjectMarked(Display *d, unsigned int aKeycode, bool aIsPress)
{
	XUngrabKeyboard(d, CurrentTime);
	XTestFakeKeyEvent(d, (KeyCode)aKeycode, aIsPress ? True : False, CurrentTime);
	XFlush(d);
	PassthruMark &m = sPassthruLog[sPassthruHead++ % _countof(sPassthruLog)];
	m = PassthruMark{aKeycode, GetTickCount(), !aIsPress, false};
}

bool LinuxIsPassthruCopy(XEvent &ev)
{
	bool is_up = ev.type == KeyRelease;
	unsigned int id = (unsigned int)ev.xkey.keycode;
	DWORD now = GetTickCount();
	// Consume-once (check0820 P1): a mark matches only the copy -- the first
	// event for the same keycode + phase after the injection -- and is marked
	// used immediately, so a second identical event (a genuine repeat, e.g.
	// double letters or auto-repeat) is NEVER swallowed by the same record.
	// Expired entries (the injected event never came back) are released so
	// the next press is forwarded normally.
	for (int i = 0; i < (int)_countof(sPassthruLog); ++i)
	{
		PassthruMark &m = sPassthruLog[i];
		if (m.id != id || m.is_up != is_up)
			continue;
		if (m.consumed)
			continue;
		if (now - m.when >= (DWORD)PASSTHRU_COPY_MS_WINDOW)
		{
			m.id = 0; // Never came back; not this event.
			continue;
		}
		m.consumed = true;
		return true;
	}
	return false;
}

// ---------------------------------------------------------------------------
// Self-injection tracking (check_detail0821 §2-B + §2-C / R2 S3+S4)
// ---------------------------------------------------------------------------
// Every key this process injects (Send/SendEvent/SendInput/SendPlay/SendText)
// is recorded with a consume-once mark carrying its SendLevel and whether it
// came from an explicit SendInput.  On X11 the injected events come back
// through the passive grab asynchronously (often after the send call has
// already returned), so an in-flight flag is insufficient.  LinuxHandleKeyEvent
// then:
//   - drops the event entirely if it was SendInput ("unload the hook during
//     SendInput": no own hotkey/hotstring may fire, §2-B);
//   - otherwise level-gates hotkeys by #InputLevel and the InputHook by
//     MinSendLevel (SendLevel semantics, §2-C).
// The list is unbounded (a batch can be hundreds of keys) and pruned by the
// match window like the passthru log.  Consume-once means a genuine repeat or
// a later physical press of the same key is never swallowed by a stale record.
struct SelfMark
{
	unsigned int id;   // KeyCode.
	DWORD when;        // GetTickCount() at injection.
	bool is_up;
	bool is_sendinput; // SendInput transport (hook-unloaded semantic).
	bool is_sendplay;  // SendPlay transport (journal semantic).
	int  level;        // g->SendLevel at injection.
	bool consumed;
};
static std::vector<SelfMark> sSelfLog;
static std::vector<SelfMark> sRawSelfLog;
#define SELF_MARK_MS_WINDOW 1000

void LinuxSelfTrack(unsigned int aKeycode, bool aIsPress, int aLevel
	, bool aIsSendInput, bool aIsSendPlay)
{
	DWORD now = GetTickCount();
	// Opportunistic prune of expired marks (keeps the list bounded).
	auto prune = [now](std::vector<SelfMark> &log) {
		for (size_t i = 0; i < log.size(); )
			if (now - log[i].when >= (DWORD)SELF_MARK_MS_WINDOW)
				log.erase(log.begin() + (long)i);
			else
				++i;
	};
	prune(sSelfLog);
	prune(sRawSelfLog);
	SelfMark m;
	m.id = aKeycode;
	m.when = now;
	m.is_up = !aIsPress;
	m.is_sendinput = aIsSendInput;
	m.is_sendplay = aIsSendPlay;
	m.level = aLevel;
	m.consumed = false;
	sSelfLog.push_back(m);
	sRawSelfLog.push_back(m);
}

void LinuxSelfClear()
{
	sSelfLog.clear();
	sRawSelfLog.clear();
}

// True when a grabbed event is a copy of a key this process injected
// (consume-once per mark, same expiry discipline as the passthru log); fills
// aLevel/aIsSendInput from the matched mark.  Scans most-recent-first so a
// stale mark from an earlier send of the same key+phase can never shadow the
// current injection.  Only keys that carry a passive grab ever reach this
// check, so an unconsumed mark can never swallow a non-grabbed key's traffic.
static bool LinuxSelfLookup(XEvent &ev, int &aLevel, bool &aIsSendInput, bool &aIsSendPlay)
{
	unsigned int id = (unsigned int)ev.xkey.keycode;
	bool is_up = ev.type == KeyRelease;
	DWORD now = GetTickCount();
	for (size_t i = sSelfLog.size(); i-- > 0; )
	{
		SelfMark &m = sSelfLog[i];
		if (m.id != id || m.is_up != is_up || m.consumed)
			continue;
		if (now - m.when >= (DWORD)SELF_MARK_MS_WINDOW)
			continue; // Expired; pruned on the next Track().
		m.consumed = true;
		aLevel = m.level;
		aIsSendInput = m.is_sendinput;
		aIsSendPlay = m.is_sendplay;
		return true;
	}
	return false;
}

bool LinuxSelfLookupRaw(unsigned int aKeycode, bool aIsPress
	, int &aLevel, bool &aIsSendInput, bool &aIsSendPlay)
{
	bool is_up = !aIsPress;
	DWORD now = GetTickCount();
	// XI2 raw events preserve X request order, including re-entrant replacement
	// sends. Match oldest-first so an already queued physical/original Space
	// cannot consume a future level-0 replacement Space mark.
	for (size_t i = 0; i < sRawSelfLog.size(); ++i)
	{
		SelfMark &m = sRawSelfLog[i];
		if (m.id != aKeycode || m.is_up != is_up || m.consumed)
			continue;
		if (now - m.when >= (DWORD)SELF_MARK_MS_WINDOW)
			continue;
		m.consumed = true;
		aLevel = m.level;
		aIsSendInput = m.is_sendinput;
		aIsSendPlay = m.is_sendplay;
		return true;
	}
	return false;
}

// ---------------------------------------------------------------------------
// Lazy reconcile + state-change hooks
// ---------------------------------------------------------------------------

// Set when the grab set may have changed (hotkey/hotstring/suspend state,
// capture mode, mapping, button-passthrough).  The dispatch loop re-runs
// LinuxReconcileHotkeyGrabs only when this is set, so hotkey-state changes
// must go through LinuxHotkeyStateChanged()/LinuxSetReconcileDirty().
bool sReconcileDirty = true;

void LinuxSetReconcileDirty()
{
	sReconcileDirty = true;
}

// Hotkey::ManifestAllHotkeysHotstringsHooks() calls this after every
// hotkey/hotstring/suspend change.
void LinuxHotkeyStateChanged()
{
	sReconcileDirty = true;
	LinuxCaptureStateChanged();
	// Global hotkeys are routed through the unified input backend (X11
	// XGrabKey / Wayland portal / GNOME Shell extension), see input_backend.h.
	LinuxInputBackendSync();
}

// ---------------------------------------------------------------------------
// Typed-text capture grab set (all keys x all main-modifier combos)
// ---------------------------------------------------------------------------

std::set<GrabSpec> sCaptureSpecs;
bool sCaptureSpecsDirty = true;

void LinuxCaptureAddSpecs(std::set<GrabSpec> &aDesired)
{
	if (!LinuxCaptureNeedsGrabs())
		return;
	if (sCaptureSpecsDirty)
	{
		sCaptureSpecs.clear();
		Display *d = LinuxHotkeyDisplay();
		if (!d)
			return;
		unsigned int prim[4] = { ControlMask, ShiftMask, sAltMask, sSuperMask };
		unsigned int lock_combos[8];
		int nlock = 1;
		lock_combos[0] = 0;
		for (int i = 0; i < sLockMaskCount && nlock < 8; ++i)
		{
			int n = nlock;
			for (int j = 0; j < n; ++j)
				lock_combos[nlock++] = lock_combos[j] | sLockMasks[i];
		}
		std::set<KeyCode> selected;
		LinuxCaptureGrabKeycodes(d, selected);
		for (KeyCode kc : selected)
			for (int t = 0; t < 16; ++t)
			{
				unsigned int m = 0;
				for (int p = 0; p < 4; ++p)
					if (t & (1 << p))
						m |= prim[p];
				for (int c = 0; c < nlock; ++c)
					sCaptureSpecs.insert(GrabSpec{(KeyCode)kc, 0, m | lock_combos[c]});
			}
		sCaptureSpecsDirty = false;
	}
	for (std::set<GrabSpec>::const_iterator it = sCaptureSpecs.begin(); it != sCaptureSpecs.end(); ++it)
		aDesired.insert(*it);
}

void LinuxCaptureMappingNotify()
{
	sCaptureSpecsDirty = true;
	sReconcileDirty = true;
	LinuxCaptureKeymapChanged();
}

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

void LinuxReconcileHotkeyGrabs(bool aReportConflict)
{
	Display *d = LinuxHotkeyDisplay();
	if (!d)
		return;
	Window root = DefaultRootWindow(d);

	std::set<GrabSpec> desired;
	LinuxBuildDesired(d, desired);
	LinuxCaptureAddSpecs(desired);
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
	for (std::set<GrabSpec>::iterator it = sConflicted.begin(); it != sConflicted.end();)
	{
		if (!desired.count(*it))
			sConflicted.erase(it++);
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
			// XNextRequest is the serial the following grab request will use.
			unsigned long serial = (unsigned long)XNextRequest(d);
			if (it->button)
				XGrabButton(d, it->button, it->modifiers, root, False
					, ButtonPressMask | ButtonReleaseMask, GrabModeAsync, GrabModeAsync, None, None);
			else
				XGrabKey(d, it->keycode, it->modifiers, root, False, GrabModeAsync, GrabModeAsync);
			pending.push_back(PendingGrab{serial, *it});
		}
		// Grabs are asynchronous. Flush before classifying every request, not
		// in the trap destructor after the result has already been consumed.
		XSync(d, False);
		for (const PendingGrab &grab : pending)
		{
			if (trap.HasBadAccessFor(grab.serial))
			{
				sConflicted.insert(grab.spec);
				// Only the synchronous Hotkey() call reports an error. Background
				// retries remain silent and cannot leave a stale error for a later
				// unrelated registration.
				if (aReportConflict && !sLastConflictName[0])
				{
					if (grab.spec.button)
						_sntprintf(sLastConflictName, _countof(sLastConflictName), _T("mouse button %u (modifiers %X)")
							, grab.spec.button, grab.spec.modifiers);
					else
					{
						KeySym ks = XkbKeycodeToKeysym(d, grab.spec.keycode, 0, 0);
						_sntprintf(sLastConflictName, _countof(sLastConflictName), _T("%s (modifiers %X)")
							, ks ? XKeysymToString(ks) : _T("key"), grab.spec.modifiers);
					}
				}
			}
			else
			{
				sInstalled.insert(grab.spec);
				sConflicted.erase(grab.spec);
			}
		}
	}

	if (!sConflicted.empty())
		sNextConflictRetry = GetTickCount() + CONFLICT_RETRY_MS;
	else
		sNextConflictRetry = 0;
	if (changed || !pending.empty())
		sIndexDirty = true;
}

void LinuxDispatchHotkeys()
{
	Display *d = LinuxHotkeyDisplay();
	if (!d)
		return;
	if (!sConflicted.empty() && sNextConflictRetry
		&& (LONG)(GetTickCount() - sNextConflictRetry) >= 0)
		sReconcileDirty = true;
	if (sReconcileDirty)
	{
		LinuxReconcileHotkeyGrabs();
		sReconcileDirty = false;
	}
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
					bool is_press = cookie->evtype == XI_RawKeyPress;
					KeyCode raw_keycode = (KeyCode)re->detail;
					LinuxTrackModifierKeycode(raw_keycode, is_press);
					unsigned int raw_mod = sRawModMaskByKeycode[(unsigned int)raw_keycode];
					if (raw_mod)
					{
						if (is_press) sRawDepressedMods |= raw_mod;
						else sRawDepressedMods &= ~raw_mod;
					}
					XkbStateRec raw_state;
					if (XkbGetState(d, XkbUseCoreKbd, &raw_state) == Success)
					{
						sRawLockedMods = raw_state.locked_mods;
						sRawGroup = raw_state.group;
					}
					bool is_xtest = LinuxIsXTestDevice((int)re->sourceid);
					// §3: record the source classification (sourceid is only
					// valid from XI 2.1; the tap is disabled otherwise).
					LinuxXTestTapRecord((unsigned int)raw_keycode, is_press, is_xtest);
					// In broker mode the broker stream owns the character
					// capture (EvdevBrokerEventAdapter); the XI2 observer only
					// records provenance, so no double capture here.
					if (!LinuxInputdClientActive()
						&& (LinuxCaptureUsesRaw() || LinuxInputEventTraceEnabled()))
					{
						int raw_level = -1;
						bool raw_sendinput = false;
						bool raw_sendplay = false;
						bool is_self = LinuxSelfLookupRaw((unsigned int)raw_keycode, is_press
							, raw_level, raw_sendinput, raw_sendplay);
						AhkInputSource source = is_self ? AhkInputSource::SELF_INJECT
							: (is_xtest ? AhkInputSource::OTHER_INJECT : AhkInputSource::PHYSICAL);
						unsigned int core_state = XkbBuildCoreState(
							sRawDepressedMods | sRawLockedMods, sRawGroup);
						LinuxCaptureRawKeyEvent(d, raw_keycode, is_press, re->time
							, core_state, raw_level, raw_sendinput, raw_sendplay, source
							, (uint32_t)re->sourceid);
					}
				}
				else if (cookie->extension == sXI2Opcode && cookie->evtype == XI_HierarchyChanged)
				{
					// A device was added/removed: re-enumerate the XTEST ids.
					LinuxXI2EnumXTest(d);
				}
				XFreeEventData(d, cookie);
			}
			break;
		}
		case MappingNotify:
			// Keyboard map/layout changed: refresh the modifier slots and
			// rebuild every grab from scratch.  EXCEPTION (round-34): the
			// Send engine's Unicode borrows (XChangeKeyboardMapping) also
			// broadcast MappingNotify, but they only retarget a spare
			// keycode -- modifier slots and grab targets are unaffected,
			// and rebuilding the ~2000 capture grabs per borrow floods the
			// X connection (a classic TCP fill deadlock observed in the
			// doc-check under Xvfb).  Skip the full rebuild while a borrow
			// is recent; a real layout switch outside that window still
			// rebuilds as before.
			XRefreshKeyboardMapping(&ev.xmapping);
			if (LinuxBorrowRecent())
				break;
			LinuxUpdateModifierMap(d);
			LinuxUpdateModifierKeycodes(d);
			LinuxKeyModelX11Refresh(d);
			sIndexDirty = true;
			sReconcileDirty = true;
			LinuxCaptureMappingNotify();
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
	// Global hotkeys are served by the unified input backend (input_backend.h):
	//   X11       XGrabKey/XRecord (this file) - needs an X11 display.
	//   portal    XDG Global Shortcuts (core_gshortcut_linux.cpp)
	//   gnome-shell  GNOME Shell extension broker (input_backend_gnome_shell.cpp)
	//   evdev     native /dev/input reading lane (core_evdev_linux.cpp)
	// Only the X11 backend requires a display here: the Wayland backends and
	// the evdev lane have no X11 dependency, so a Wayland session must NOT
	// be rejected out of hand (it previously never reached the backend at all).
	// UNKNOWN kinds (auto not yet resolved) cannot register anything.
	// The portal / gnome-shell / evdev backends do not create an X11 display
	// and need no X11 check here: their registrations are pushed through
	// LinuxInputBackendSync() below, which knows each backend's own
	// availability and errors.
	TCHAR name_buf[1024];
	LPTSTR name = TokenToString(*aParam[0], name_buf, nullptr);
	auto is_hex = [](wchar_t c) {
		return (c >= L'0' && c <= L'9') || (c >= L'a' && c <= L'f')
			|| (c >= L'A' && c <= L'F');
	};
	bool explicit_scan = false;
	for (const wchar_t *p = name; p && p[0] && p[1]; ++p)
		if ((p[0] == L's' || p[0] == L'S') && (p[1] == L'c' || p[1] == L'C')
			&& is_hex(p[2]))
		{
			explicit_scan = true;
			break;
		}
	bool custom_combo = wcsstr(name, L" & ") != nullptr;
	bool key_up = wcslen(name) >= 3 && !_tcsicmp(name + wcslen(name) - 3, L" up");
	bool wildcard = wcschr(name, L'*') != nullptr;
	bool passthrough = wcschr(name, L'~') != nullptr;
	AhkInputBackendKind target_hint = LinuxInputBackendRoute(passthrough, key_up,
		false, wildcard, explicit_scan, custom_combo);
	if (target_hint == AhkInputBackendKind::AUTO)
	{
		aResultToken.Error(_T("No available Linux input backend satisfies this hotkey's capability requirements."), name, ErrorPrototype::OS);
		return;
	}
	if (target_hint == AhkInputBackendKind::X11 && !LinuxHotkeyDisplay())
	{
		aResultToken.Error(_T("The routed hotkey requires X11, but XOpenDisplay failed."), name, ErrorPrototype::OS);
		return;
	}
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
	// Registration can migrate existing prefix/suffix hotkeys between lanes,
	// so recompute the complete mux before reconciling X11 and backend state.
	UCHAR no_suppress = 0;
	bool hook_mandatory = false;
	Hotkey *registered = Hotkey::FindHotkeyByTrueNature(name, no_suppress, hook_mandatory);
	AhkInputBackendKind target = registered
		? LinuxInputBackendForHotkey(registered) : target_hint;
	LinuxInputBackendSync();
	LinuxReconcileHotkeyGrabs(target == AhkInputBackendKind::X11);
	if (target == AhkInputBackendKind::X11 && sLastConflictName[0])
	{
		aResultToken.Error(_T("Hotkey could not be registered: the key combination is already grabbed by another client (X11 BadAccess): ")
			, sLastConflictName, ErrorPrototype::OS);
		sLastConflictName[0] = _T('\0');
		return;
	}
	const wchar_t *backend_err = LinuxInputBackendLastErrorFor(target);
	if (backend_err && backend_err[0])
	{
		aResultToken.Error(backend_err, _T(""), ErrorPrototype::OS);
		return;
	}
}