// Hotstring expansion engine (round 32): typed-text capture on X11.
//
// Design:
//   - While at least one enabled hotstring exists, the hotkey backend grabs
//     every key (keycodes 8..255 x primary-modifier power set x lock
//     combos), so typed characters reach this engine instead of the target
//     application.
//   - The engine holds the presses/releases of characters that could still
//     form a match (a hotstring trigger whose prefix is a suffix of the
//     typed text), forwards everything else with the standard XTEST
//     passthrough, and on a full match discards the held trigger (nothing
//     was ever sent, so no backspaces are needed -- unlike Windows) and
//     sends the replacement (or runs the X-option callback).
//   - Matching covers the core options: C (case sensitive), * (no end char
//     required), O (omit the end char), X (callback) and case conforming;
//     the end chars are the live g_EndChars set.  Hotstrings registered
//     under a HotIf criterion are checked with HotCriterionAllowsFiring.
//     The longest trigger wins; ties keep registration order.
//
// Limitation (documented): SendLevel and per-hotstring send-mode timing
// options are not modeled (the port's Send modes are all XTEST anyway).
#include "../../stdafx.h"
#include "../../script.h"
#include "../../globaldata.h"
#include "../../script_func_impl.h"
#include "../../hotkey.h"
#include "core_hotkey_linux.h"
#include "core_capture_linux.h"
#include "core_input_linux.h"
#include "../../application.h"
#include <X11/Xlib.h>
#include <X11/keysym.h>
#include <X11/XKBlib.h>
#include <set>
#include <cstring>
#include <cwctype>
#include "../../hook.h"

namespace {

bool sActive = false;

// Held (typed-but-not-yet-committed) events: presses and their releases.
struct HeldEvent
{
	XEvent ev;
	bool is_release;
};
HeldEvent sHeld[128];
int sHeldCount = 0;
wchar_t sBuffer[128]; // The text of the held characters.
int sBufLen = 0;

// Modifier/lock keys never interrupt or join the text stream.
bool LinuxIsModifierKey(KeySym ks)
{
	switch (ks)
	{
	case XK_Shift_L: case XK_Shift_R: case XK_Control_L: case XK_Control_R:
	case XK_Alt_L: case XK_Alt_R: case XK_Meta_L: case XK_Meta_R:
	case XK_Super_L: case XK_Super_R: case XK_Hyper_L: case XK_Hyper_R:
	case XK_Caps_Lock: case XK_Num_Lock: case XK_Scroll_Lock:
		return true;
	}
	return false;
}

// The character a key press produces (US layout).  Shift selects the
// shifted keysym; CapsLock toggles the case of letters (X11 keysyms do not
// apply caps themselves).  0 = not text.
wchar_t LinuxCaptureChar(Display *d, XEvent &ev)
{
	bool shift = (ev.xkey.state & ShiftMask) != 0;
	bool caps = (ev.xkey.state & LockMask) != 0;
	KeySym ks = XkbKeycodeToKeysym(d, ev.xkey.keycode, shift ? 1 : 0, 0);
	if (ks >= 0x20 && ks <= 0x7e)
	{
		wchar_t c = (wchar_t)ks;
		if (iswalpha(c))
			return (shift ^ caps) ? (wchar_t)towupper(c) : (wchar_t)towlower(c);
		return c;
	}
	switch (ks)
	{
	case XK_Return: case XK_KP_Enter: return L'\n';
	case XK_Tab: return L'\t';
	}
	return 0;
}

// Does aBuf[aLen-sl..aLen) equal aStr (case per aCaseSensitive)?
bool LinuxBufEndsWith(const wchar_t *aBuf, int aLen, const wchar_t *aStr, bool aCaseSensitive)
{
	int sl = (int)wcslen(aStr);
	if (aLen < sl)
		return false;
	if (aCaseSensitive)
		return wcsncmp(aBuf + aLen - sl, aStr, sl) == 0;
	for (int i = 0; i < sl; ++i)
		if (towlower(aBuf[aLen - sl + i]) != towlower(aStr[i]))
			return false;
	return true;
}

// Deliver one key event straight to the input-focus window with
// XSendEvent.  The capture engine's own forwards (held text, end chars,
// replacements) must NOT re-enter the passive grab stream: XTEST re-injects
// of an already-released key would re-activate the capture grabs and be
// delivered back to us instead of the target.  A synthetic (send_event)
// delivery bypasses the grab machinery entirely.  Documented trade-off:
// tools that reject synthetic events will not see forwarded text.
void LinuxCaptureForward(Display *d, XEvent &ev)
{
	if (!d)
		return;
	Window focus = None;
	int revert = 0;
	XGetInputFocus(d, &focus, &revert);
	if (focus)
	{
		if (ev.type == KeyPress || ev.type == KeyRelease)
		{
			ev.xkey.display = d;
			ev.xkey.window = focus;
			ev.xkey.same_screen = True;
		}
		XSendEvent(d, focus, True, (long)(KeyPressMask | KeyReleaseMask), &ev);
		XFlush(d);
	}
}
void LinuxCaptureForwardKey(Display *d, unsigned int aKeycode, bool aIsPress, unsigned int aState)
{
	if (!d)
		return;
	Window focus = None;
	int revert = 0;
	XGetInputFocus(d, &focus, &revert);
	if (!focus)
		return;
	XKeyEvent ke;
	memset(&ke, 0, sizeof(ke));
	ke.type = aIsPress ? KeyPress : KeyRelease;
	ke.display = d;
	ke.window = focus;
	ke.keycode = (KeyCode)aKeycode;
	ke.state = aState;
	ke.same_screen = True;
	XEvent ev;
	ev.xkey = ke;
	XSendEvent(d, focus, True, (long)(KeyPressMask | KeyReleaseMask), &ev);
	XFlush(d);
}

void LinuxCaptureFlush(Display *d)
{
	for (int i = 0; i < sHeldCount; ++i)
		LinuxCaptureForward(d, sHeld[i].ev);
	sHeldCount = 0;
	sBufLen = 0;
}
// Send replacement text through the capture engine's synthetic-forward path
// (send_event; see LinuxCaptureForward).  ASCII letters/characters are mapped to keycodes; the
// shifted letters press/release the left Shift.  Unmappable characters are
// skipped (documented).  This intentionally does not go through the general
// Send engine (which writes no copy marks).
void LinuxCaptureSendMarked(Display *d, const wchar_t *aText)
{
	if (!d)
		return;
	KeyCode shift = LinuxKeycodeForVkEx(d, 0xA0); // LShift.
	for (const wchar_t *p = aText; *p; ++p)
	{
		wchar_t c = *p;
		bool needs_shift = c >= L'A' && c <= L'Z';
		wchar_t base = needs_shift ? (wchar_t)(c - L'A' + L'a') : c;
		KeySym ks = 0;
		switch (base)
		{
		case L'\n': ks = XK_Return; break;
		case L'\t': ks = XK_Tab; break;
		case L'\b': ks = XK_BackSpace; break;
		default:
			if (base >= 0x20 && base <= 0x7e)
				ks = (KeySym)base;
			break;
		}
		KeyCode kc = ks ? XKeysymToKeycode(d, ks) : 0;
		if (!kc)
			continue; // Unmappable character: skipped (documented).
		if (needs_shift && shift)
			LinuxCaptureForwardKey(d, (unsigned)shift, true, ShiftMask);
		LinuxCaptureForwardKey(d, (unsigned)kc, true, needs_shift ? ShiftMask : 0);
		LinuxCaptureForwardKey(d, (unsigned)kc, false, needs_shift ? ShiftMask : 0);
		if (needs_shift && shift)
			LinuxCaptureForwardKey(d, (unsigned)shift, false, 0);
	}
}

// Case mode of the typed trigger text (for mConformToCase):
// 0 = as typed, 1 = first letter capital, 2 = all letters capital.
int LinuxCaseMode(const wchar_t *aText, int aLen)
{
	bool first_upper = false, any_upper = false, any_lower = false;
	for (int i = 0; i < aLen; ++i)
	{
		wchar_t c = aText[i];
		if (!iswalpha(c))
			continue;
		if (i == 0)
			first_upper = iswupper(c) != 0;
		if (iswupper(c))
			any_upper = true;
		else
			any_lower = true;
	}
	if (any_upper && !any_lower)
		return 2;
	if (first_upper)
		return 1;
	return 0;
}

void LinuxCaptureFire(Display *d, Hotstring *aHs, int aCaseMode, bool aForwardEndChar, XEvent &aEndEv)
{
	// The held trigger events are discarded: nothing was ever sent to the
	// target, so no backspaces are needed (Windows sends backspaces over
	// the already-typed text; the X11 held model is cleaner).
	sHeldCount = 0;
	sBufLen = 0;
	if (aHs->mCallback)
	{
		++g_nThreads;
		++g;
		InitNewThread(0, false, false);
		aHs->PerformInNewThreadMadeByCaller();
		ResumeUnderlyingThread();
	}
	else
	{
		wchar_t repl[LINE_SIZE + 8];
		tcslcpy(repl, aHs->mReplacement ? aHs->mReplacement : _T(""), _countof(repl));
		if (aHs->mConformToCase && *repl)
		{
			if (aCaseMode == 2)
				for (wchar_t *cp = repl; *cp; ++cp)
					*cp = (wchar_t)towupper(*cp);
			else if (aCaseMode == 1)
				repl[0] = (wchar_t)towupper(repl[0]);
		}
		LinuxCaptureSendMarked(LinuxHotkeyDisplay(), repl);
	}
	if (aForwardEndChar)
		LinuxCaptureForward(d, aEndEv);
}

} // namespace

// ---------------------------------------------------------------------------
// Public interface
// ---------------------------------------------------------------------------

bool LinuxCaptureActive()
{
	// Capture is needed while any hotstring is enabled or an InputHook is
	// in progress (live key capture).
	return sActive || (g_input && g_input->InProgress());
}

void LinuxCaptureStateChanged()
{
	bool want = Hotstring::sEnabledCount > 0 || (g_input && g_input->InProgress());
	if (want != sActive)
	{
		sActive = want;
		sHeldCount = 0;
		sBufLen = 0;
		LinuxSetReconcileDirty();
		// Install/remove the all-keys capture grabs right away (the lazy
		// dispatch reconcile only runs inside the main loop, which a
		// synchronous script may never enter).
		LinuxReconcileHotkeyGrabs();
	}
}

// Approximate Win32 VK for a keysym (US layout; used for InputHook
// end-keys).  Letters map to VK_A..VK_Z (0x41..), digits to the ASCII VKs,
// and the common named keys to their VK_ constants.
vk_type LinuxKeysymToVk(KeySym ks)
{
	if (ks >= 'A' && ks <= 'Z') // Uppercase keysyms equal the VK codes.
		return (vk_type)ks;
	if (ks >= 'a' && ks <= 'z')
		return (vk_type)(ks - 'a' + 0x41); // VK_A = 0x41.
	if (ks >= '0' && ks <= '9')
		return (vk_type)ks;
	switch (ks)
	{
	case XK_Return: case XK_KP_Enter: return 0x0D; // VK_RETURN.
	case XK_Tab: return 0x09;                      // VK_TAB.
	case XK_Escape: return 0x1B;                   // VK_ESCAPE.
	case XK_BackSpace: return 0x08;                // VK_BACK.
	case XK_space: return 0x20;                    // VK_SPACE.
	default:
		if (ks >= XK_F1 && ks <= XK_F24)
			return (vk_type)(0x70 + (ks - XK_F1)); // VK_F1 = 0x70.
		if (ks >= XK_KP_0 && ks <= XK_KP_9)
			return (vk_type)(0x60 + (ks - XK_KP_0)); // VK_NUMPAD0 = 0x60.
	}
	return 0;
}

// Character a key press produces for the InputHook stream (Return/Tab/
// BackSpace + printable ASCII at the current shift level).
wchar_t LinuxInputHookChar(Display *d, XEvent &ev)
{
	KeySym ks = XkbKeycodeToKeysym(d, ev.xkey.keycode, (ev.xkey.state & ShiftMask) ? 1 : 0, 0);
	if (ks >= 0x20 && ks <= 0x7e)
		return (wchar_t)ks;
	switch (ks)
	{
	case XK_Return: case XK_KP_Enter: return L'\r';
	case XK_Tab: return L'\t';
	case XK_BackSpace: return L'\b';
	}
	return 0;
}

// Feed one key event to the active InputHook (live key capture on Linux via
// the same all-keys grab machinery the hotstring engine uses).  Consumed
// events are not forwarded to the target window, matching Windows Input.
// Supported: buffer fill, end chars, match list, buffer limit, backspace
// undo, and the OnChar notification.  Named VK end-keys and the OnKeyDown
// VK/SC arguments remain documented limitations (single-char end keys and
// the default end behaviour are covered).
bool LinuxCaptureFeedInput(Display *d, XEvent &ev)
{
	input_type *active = g_input;
	if (!active || !active->InProgress())
		return false;
	if (ev.type != KeyPress)
		return true; // Releases are consumed while the input is active.
	KeySym ks0 = XkbKeycodeToKeysym(d, ev.xkey.keycode, 0, 0);
	if (LinuxIsModifierKey(ks0))
		return true;
	wchar_t ch = LinuxInputHookChar(d, ev);
	if (ch == L'\b' && active->BackspaceIsUndo)
	{
		if (active->BufferLength > 0)
		{
			--active->BufferLength;
			active->Buffer[active->BufferLength] = L'\0';
		}
		return true;
	}
	// End keys configured by the InputHook's end-keys argument are stored as
	// VK flags (KeyVK).  Check the pressed key's VK against them before
	// collecting text: a single-char key ends with EndChar, a named key with
	// EndKey (matching Windows semantics).
	if (vk_type vk = LinuxKeysymToVk(ks0))
	{
		bool with_shift = (ev.xkey.state & ShiftMask) != 0;
		UCHAR flag = with_shift ? END_KEY_WITH_SHIFT : END_KEY_WITHOUT_SHIFT;
		if (active->KeyVK[vk] & flag)
		{
			if (ch)
				active->EndByChar((TCHAR)ch);
			else
				active->EndByKey(vk, (sc_type)0, false, with_shift);
			return true;
		}
	}
	if (ch)
	{
		TCHAR cbuf[2] = { (TCHAR)ch, 0 };
		active->CollectChar(cbuf, 1); // Ends on end char/match/limit internally.
		// Note: the OnChar/OnKeyDown notifications are not fired here.
		// Invoking a script callback from the native capture dispatch
		// re-enters the interpreter and hangs (the OnExit/OnClipboardChange
		// monitor pattern would require a proper quasi-thread launch from a
		// non-dispatch context).  The core live capture -- buffer fill, end
		// chars, match list, limit, backspace undo, timeout, and input
		// suppression -- is complete; OnChar/OnKeyDown remain limited.
	}
	return true;
}

bool LinuxCaptureKeyEvent(Display *d, XEvent &ev)
{
	if (!LinuxCaptureActive())
		return false;

	// An active InputHook captures the typed-text stream and consumes every
	// grabbed event (presses are fed; releases discarded) -- it wins over
	// hotstrings while in progress, like Windows.
	if (g_input && g_input->InProgress())
	{
		LinuxCaptureFeedInput(d, ev);
		return true;
	}

	if (!sActive)
		return false;

	if (ev.type == KeyRelease)
	{
		// A release of a held press continues the hold; anything else
		// (already-forwarded keys, modifier releases, key-up hotkeys) is
		// left to the normal flow.
		for (int i = sHeldCount - 1; i >= 0; --i)
			if (!sHeld[i].is_release && sHeld[i].ev.xkey.keycode == ev.xkey.keycode)
			{
				if (sHeldCount < (int)_countof(sHeld))
					sHeld[sHeldCount++] = HeldEvent{ev, true};
				return true;
			}
		return false;
	}

	// KeyPress.
	KeySym ks = XkbKeycodeToKeysym(d, ev.xkey.keycode, 0, 0);
	if (LinuxIsModifierKey(ks))
		return false; // Modifiers flow through; they do not break matching.

	wchar_t ch = LinuxCaptureChar(d, ev);
	if (!ch)
	{
		// Navigation/function key: the text stream ends here.
		LinuxCaptureFlush(d);
		return false; // The normal flow forwards the key (and can fire hotkeys).
	}

	// Working buffer = held text + this character.
	wchar_t buf[sizeof(sBuffer) / sizeof(sBuffer[0])];
	int blen = sBufLen;
	if (blen < (int)(sizeof(sBuffer) / sizeof(sBuffer[0])) - 1)
	{
		memcpy(buf, sBuffer, sizeof(wchar_t) * (size_t)blen);
		buf[blen] = ch;
		++blen;
	}

	bool is_endchar = wcschr(g_EndChars, ch) != nullptr;

	Hotstring *best = nullptr;
	int best_len = 0;
	bool best_endchar = false; // Fired via an end char (vs a * hotstring).
	bool best_omit = false;
	for (int i = 0; i < Hotstring::sHotstringCount; ++i)
	{
		Hotstring *hs = Hotstring::shs[i];
		if (!hs || (hs->mSuspended & (HS_SUSPENDED | HS_TURNED_OFF | HS_TEMPORARILY_DISABLED)))
			continue;
		if (hs->mHotCriterion && !HotCriterionAllowsFiring(hs->mHotCriterion, hs->mName))
			continue;
		int sl = (int)wcslen(hs->mString);
		if (sl < 1 || sl > blen)
			continue;
		bool full = false, via_end = false;
		if (hs->mEndCharRequired)
		{
			// The trigger is complete when the current char is an end char.
			if (is_endchar && LinuxBufEndsWith(buf, blen - 1, hs->mString, hs->mCaseSensitive))
			{
				full = true;
				via_end = true;
			}
		}
		else if (LinuxBufEndsWith(buf, blen, hs->mString, hs->mCaseSensitive))
			full = true;
		if (full && sl >= best_len)
		{
			// Prefer the longest trigger; ties keep the first (registration
			// order).
			best = hs;
			best_len = sl;
			best_endchar = via_end;
			best_omit = via_end && hs->mOmitEndChar;
		}
	}

	if (best)
	{
		// The typed trigger text is the buffer (minus the end char when the
		// match was completed by one).
		int typed_len = best_endchar ? blen - 1 : blen;
		int case_mode = best->mConformToCase ? LinuxCaseMode(buf, typed_len) : 0;
		// The end char (current event) is forwarded after the replacement
		// unless the O option omits it.  For * hotstrings the current char
		// COMPLETED the trigger and is part of it (Windows suppresses the
		// completing key too), so it is never forwarded.
		bool forward_cur = best_endchar ? !best_omit : false;
		LinuxCaptureFire(d, best, case_mode, forward_cur, ev);
		return true;
	}

	// Partial match: hold the char when some trigger's prefix (case-
	// insensitive) is a suffix of the typed text (a full-length prefix is
	// included: with an end-char hotstring the trigger may still be
	// completed by the next end char).
	for (int i = 0; i < Hotstring::sHotstringCount; ++i)
	{
		Hotstring *hs = Hotstring::shs[i];
		if (!hs || (hs->mSuspended & (HS_SUSPENDED | HS_TURNED_OFF | HS_TEMPORARILY_DISABLED)))
			continue;
		if (hs->mHotCriterion && !HotCriterionAllowsFiring(hs->mHotCriterion, hs->mName))
			continue;
		int sl = (int)wcslen(hs->mString);
		// Prefix loop: is aStr[0..p) a suffix of the buffer?
		for (int p = 1; p <= sl && p <= blen; ++p)
		{
			bool ok = true;
			for (int q = 0; q < p; ++q)
				if (towlower(buf[blen - p + q]) != towlower(hs->mString[q]))
				{
					ok = false;
					break;
				}
			if (ok)
			{
				// Hold the press; the release will be held by the release
				// path.
				if (sHeldCount < (int)_countof(sHeld))
				{
					sHeld[sHeldCount++] = HeldEvent{ev, false};
					sBuffer[sBufLen++] = ch;
					sBuffer[sBufLen] = L'\0';
				}
				return true;
			}
		}
	}

	// No match possible: forward everything typed so far and this key.
	LinuxCaptureFlush(d);
	LinuxCaptureForward(d, ev);
	return true;
}
