// X11 typed-text capture (check_detail0824 M2-R).
//
// Hotstrings and visible InputHook consume the XI2.1 raw stream. Raw events
// are observation-only and multi-client: original physical events reach the
// target with send_event=False. Hotstring matches erase the already-visible
// trigger with Backspace and send the replacement, matching the Windows
// model. Only an InputHook which requests suppression retains the legacy
// all-key passive-grab compatibility lane until M2-L narrows it per KeyOpt.
//
// Matching covers C/*/O/X/B0, case conformity, inside-word, end characters,
// HotIf and longest-trigger priority. The three-layer key model supplies
// canonical physical SC, logical VK and layout-aware Unicode text.
#include "../../stdafx.h"
#include "../../script.h"
#include "../../globaldata.h"
#include "../../script_func_impl.h"
#include "../../hotkey.h"
#include "core_hotkey_linux.h"
#include "core_capture_linux.h"
#include "core_input_linux.h"
#include "core_keymodel_linux.h"
#include "core_ime_linux.h"
#include "input_event.h"
#include "../../application.h"
#include <X11/Xlib.h>
#include <X11/keysym.h>
#include <X11/XKBlib.h>
#include <set>
#include <vector>
#include <algorithm>
#include <string>
#include <cstring>
#include <cwctype>
#include "../../hook.h"

wchar_t LinuxCharFromKeySym(KeySym aKs);
vk_type LinuxKeysymToVk(KeySym aKs);
bool LinuxCaptureFeedInput(Display *d, XEvent &ev
	, const AhkLinuxKeyIdentity *aPredecoded = nullptr);

namespace {

bool sActive = false;
bool sNeedsGrabs = false;
bool sGrabKeycodesDirty = true;
std::set<KeyCode> sGrabKeycodes;
std::wstring sRawBuffer;
bool sImePreeditActive = false;
uint64_t sImePhysicalSuppressUntilUs = 0;
struct PendingImeCommit
{
	std::wstring text;
	AhkInputOrigin origin;
};
std::vector<PendingImeCommit> sPendingImeCommits;
struct PendingImeHotstringChar
{
	AhkLinuxKeyIdentity key;
	int self_level;
	uint64_t ready_us;
};
std::vector<PendingImeHotstringChar> sPendingImeHotstringChars;

// Held (typed-but-not-yet-committed) events: compatibility path for an
// InputHook which explicitly suppresses input. Hotstrings never use it.
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

bool InputHookNeedsGrabs()
{
	input_type *input = g_input;
	if (!input || !input->InProgress())
		return false;
	if (!input->VisibleText || !input->VisibleNonText)
		return true;
	for (int i = 0; i < VK_ARRAY_COUNT; ++i)
		if (input->KeyVK[i] & INPUT_KEY_SUPPRESS)
			return true;
	for (int i = 0; i < SC_ARRAY_COUNT; ++i)
		if (input->KeySC[i] & INPUT_KEY_SUPPRESS)
			return true;
	return false;
}

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

// One normalization point for capture consumers: physical evdev/sc, logical
// VK/keysym and text from the live xkbcommon state. The process-local borrow
// log remains first because a transient Unicode CORE mapping may already have
// been restored when this queued event is consumed.
bool LinuxEventKeyIdentity(Display *d, XEvent &ev, AhkLinuxKeyIdentity &aOut)
{
	memset(&aOut, 0, sizeof(aOut));
	// One borrow-log entry represents one typed character, not both phases.
	// Consuming on KeyRelease steals the next character's entry in a rapid
	// Unicode run ("你好" became only "你").
	if (ev.type == KeyPress)
		if (KeySym borrowed = LinuxConsumeBorrowedKeySym(ev.xkey.keycode))
		{
			aOut.evdev_code = ev.xkey.keycode >= 8 ? ev.xkey.keycode - 8 : 0;
			aOut.sc = LinuxScanCodeForEvdev(aOut.evdev_code);
			aOut.keysym = borrowed;
			aOut.vk = LinuxKeysymToVk(borrowed);
			aOut.text = (uint32_t)LinuxCharFromKeySym(borrowed);
			return true;
		}
	if (LinuxKeyModelX11Decode(d, ev.xkey.keycode, ev.xkey.state, aOut))
		return true;
	// Defensive fallback for X servers without xkbcommon-x11 support.
	bool shift = (ev.xkey.state & ShiftMask) != 0;
	bool caps = (ev.xkey.state & LockMask) != 0;
	aOut.evdev_code = ev.xkey.keycode >= 8 ? ev.xkey.keycode - 8 : 0;
	aOut.sc = LinuxScanCodeForEvdev(aOut.evdev_code);
	aOut.keysym = XkbKeycodeToKeysym(d, ev.xkey.keycode, shift ? 1 : 0, 0);
	aOut.vk = LinuxKeysymToVk(aOut.keysym);
	wchar_t c = LinuxCharFromKeySym(aOut.keysym);
	if (c >= 0x20 && c <= 0x7e && iswalpha(c))
		c = (shift ^ caps) ? (wchar_t)towupper(c) : (wchar_t)towlower(c);
	aOut.text = (uint32_t)c;
	return aOut.keysym != NoSymbol;
}

// ---------------------------------------------------------------------------
// Deferred InputHook notifications (OnChar/OnKeyDown/OnKeyUp), round-34
// ---------------------------------------------------------------------------
// Script callbacks must NOT run from the native capture dispatch: invoking
// the interpreter from there (inside X event feeding) hangs.  Notifications
// are therefore queued here while events are fed, and fired from the
// main-loop dispatch via LinuxCaptureDispatchInputNotifies() (called by
// LinuxDispatchInputHook), the same context hotkeys fire from.  The queue
// only ever references the CURRENT hook (g_input): after an input ends its
// object may be released at any time, and a stale pointer must never be
// dereferenced (the dispatch drops notifications for a hook that is no
// longer InProgress).
enum InputNotifyKind { NOTIFY_KEYDOWN = 0, NOTIFY_KEYUP = 1, NOTIFY_CHAR = 2 };
struct InputNotify
{
	input_type *input;
	int kind;
	vk_type vk;
	sc_type sc;
	wchar_t ch;
	uint64_t ready_us;
	bool ime_candidate;
};
std::vector<InputNotify> sInputNotifies;

static void LinuxInputNotifyQueue(input_type *aInput, int aKind
	, vk_type aVk, sc_type aSc, wchar_t aCh, bool aImeCandidate = false)
{
	if (!aInput || !aInput->ScriptObject)
		return;
	uint64_t ready = LinuxInputEventMonotonicUs();
	if (aImeCandidate)
		ready += 500000ULL; // Let a slow toolkit's first preedit signal win the race.
	InputNotify n = { aInput, aKind, aVk, aSc, aCh, ready, aImeCandidate };
	sInputNotifies.push_back(n);
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
// (send_event; see LinuxCaptureForward).  ASCII letters/characters are mapped
// to keycodes; the shifted letters press/release the left Shift.  Non-ASCII
// characters are delivered with the Unicode keysym transaction (see
// core_input_linux.cpp): a spare keycode is temporarily remapped to the
// character's keysym, the key events are forwarded with XSendEvent, and the
// mapping is reverted.  This intentionally does not go through the general
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
		if (!kc && ks == 0 && base != 0)
		{
			// Non-ASCII character: Unicode keysym transaction.  The mapping
			// reverts only after a short window so clients (including our
			// own capture dispatch) resolve the keycode while the keysym is
			// still installed (see LinuxSendCharUnicode in core_input).
			KeySym uks = LinuxCharToKeySym(base);
			bool remapped = false;
			KeyCode ukc = LinuxUnicodeKeycode(d, uks, remapped);
			if (ukc)
			{
				LinuxCaptureForwardKey(d, (unsigned)ukc, true, 0);
				LinuxCaptureForwardKey(d, (unsigned)ukc, false, 0);
				if (remapped)
				{
					XSync(d, False);
					usleep(30000); // Client processing window (see above).
					LinuxUnicodeRestore(d, ukc);
				}
			}
			continue;
		}
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

void LinuxPrepareHotstringThread(Hotstring *aHotstring, wchar_t aEndChar)
{
	g_script.mPriorHotkeyName = g_script.mThisHotkeyName;
	g_script.mPriorHotkeyStartTime = g_script.mThisHotkeyStartTime;
	g_script.mThisHotkeyName = aHotstring->mName;
	g_script.mThisHotkeyStartTime = GetTickCount();
	g_script.mThisHotkeyModifiersLR = 0;
	g_script.mEndChar = aEndChar;
}

void LinuxRawSendBackspaces(Display *d, int aCount)
{
	if (aCount <= 0)
		return;
	wchar_t spec[64];
	_sntprintf(spec, _countof(spec), L"{Backspace %d}", aCount);
	LinuxSendKeysString(d, spec);
}

void LinuxRawFireHotstring(Display *d, Hotstring *aHs, int aCaseMode
	, int aEraseCount, wchar_t aEndChar, bool aViaEnd)
{
	// Auto-replacement output is level 0 by definition and therefore cannot
	// recursively trigger the raw Hotstring buffer.
	SendLevelType saved_level = g->SendLevel;
	g->SendLevel = 0;
	if (aHs->mDoBackspace)
		LinuxRawSendBackspaces(d, aEraseCount);
	if (aHs->mCallback)
	{
		LinuxPrepareHotstringThread(aHs, aViaEnd ? aEndChar : 0);
		++g_nThreads;
		++g;
		InitNewThread(aHs->mPriority, false, false);
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
		LinuxSendCharsString(d, repl);
	}
	// With backspacing, the end char was erased and must be restored unless O.
	// B0 leaves the original stream in place, so re-sending would duplicate it.
	if (aViaEnd && aHs->mDoBackspace && !aHs->mOmitEndChar)
	{
		wchar_t end_text[2] = { aEndChar, 0 };
		LinuxSendCharsString(d, end_text);
	}
	g->SendLevel = saved_level;
}

void LinuxRawFeedHotstrings(Display *d, const AhkLinuxKeyIdentity &aKey, int aSelfLevel)
{
	if (sImePreeditActive)
		return;
	if (!Hotstring::sEnabledCount || !aKey.text)
		return;
	wchar_t ch = (wchar_t)aKey.text;
	if (ch == L'\b')
	{
		if (!sRawBuffer.empty())
			sRawBuffer.pop_back();
		return;
	}
	if (ch < 0x20 && ch != L'\n' && ch != L'\t')
	{
		sRawBuffer.clear();
		return;
	}
	if (sRawBuffer.size() >= 512)
		sRawBuffer.erase(0, sRawBuffer.size() - 511);
	sRawBuffer.push_back(ch);
	bool is_endchar = wcschr(g_EndChars, ch) != nullptr;
	Hotstring *best = nullptr;
	int best_len = 0;
	bool best_via_end = false;
	for (int i = 0; i < Hotstring::sHotstringCount; ++i)
	{
		Hotstring *hs = Hotstring::shs[i];
		if (!hs || (hs->mSuspended & (HS_SUSPENDED | HS_TURNED_OFF | HS_TEMPORARILY_DISABLED)))
			continue;
		if (hs->mHotCriterion && !HotCriterionAllowsFiring(hs->mHotCriterion, hs->mName))
			continue;
		// Windows rule: a synthetic event can trigger only when its SendLevel is
		// strictly greater than the Hotstring's input level. Physical/other-
		// process input is represented by -1 and always qualifies.
		if (aSelfLevel >= 0 && aSelfLevel <= (int)hs->mInputLevel)
			continue;
		int sl = (int)wcslen(hs->mString);
		int text_len = (int)sRawBuffer.size() - (hs->mEndCharRequired ? 1 : 0);
		if (sl < 1 || text_len < sl || (hs->mEndCharRequired && !is_endchar))
			continue;
		if (!LinuxBufEndsWith(sRawBuffer.c_str(), text_len, hs->mString, hs->mCaseSensitive))
			continue;
		int start = text_len - sl;
		if (!hs->mDetectWhenInsideWord && start > 0)
		{
			wchar_t prev = sRawBuffer[(size_t)start - 1];
			if (iswalnum(prev) || prev == L'_')
				continue;
		}
		if (sl >= best_len)
		{
			best = hs;
			best_len = sl;
			best_via_end = hs->mEndCharRequired;
		}
	}
	if (!best)
		return;
	int typed_len = (int)sRawBuffer.size() - (best_via_end ? 1 : 0);
	int case_mode = best->mConformToCase
		? LinuxCaseMode(sRawBuffer.c_str(), typed_len) : 0;
	int erase_count = best_len + (best_via_end ? 1 : 0);
	LinuxRawFireHotstring(d, best, case_mode, erase_count, ch, best_via_end);
	if (best->mDoReset)
		sRawBuffer.clear();
	else
	{
		// Replacement is injected and ignored by the raw buffer; retain only
		// the pre-trigger context so later inside-word checks stay meaningful.
		sRawBuffer.resize((size_t)(typed_len - best_len));
		if (best_via_end && !best->mOmitEndChar)
			sRawBuffer.push_back(ch);
	}
}

void LinuxCaptureFire(Display *d, Hotstring *aHs, int aCaseMode, wchar_t aEndChar
	, bool aForwardEndChar, XEvent &aEndEv)
{
	// The held trigger events are discarded: nothing was ever sent to the
	// target, so no backspaces are needed (Windows sends backspaces over
	// the already-typed text; the X11 held model is cleaner).
	sHeldCount = 0;
	sBufLen = 0;
	if (aHs->mCallback)
	{
		LinuxPrepareHotstringThread(aHs, aEndChar);
		++g_nThreads;
		++g;
		InitNewThread(aHs->mPriority, false, false);
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

static bool LinuxImeUtf8ToWide(const char *aUtf8, std::wstring &aWide)
{
	aWide.clear();
	if (!aUtf8)
		return false;
	int bytes = (int)strlen(aUtf8);
	if (!bytes)
		return true;
	int chars = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
		aUtf8, bytes, nullptr, 0);
	if (chars <= 0)
		return false;
	aWide.resize((size_t)chars);
	return MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
		aUtf8, bytes, &aWide[0], chars) == chars;
}

bool LinuxCaptureImePreeditActive()
{
	return sImePreeditActive;
}

void LinuxCaptureImePreedit(const char *aUtf8, bool aVisible)
{
	std::wstring preedit;
	if (!LinuxImeUtf8ToWide(aUtf8 ? aUtf8 : "", preedit))
		return;
	bool active = aVisible && !preedit.empty();
	if (active || sImePreeditActive)
		sImePhysicalSuppressUntilUs = LinuxInputEventMonotonicUs() + 200000ULL;
	if (active && !sImePreeditActive)
	{
		sPendingImeHotstringChars.clear();
		// XI2 can drain several physical events before the first IBus signal is
		// dispatched. Strip the complete trailing phonetic token, not merely
		// preedit.size() (candidate "你" may represent raw "ni"). A preceding
		// space/punctuation remains as Hotstring/InputHook context.
		auto preedit_start = [](const wchar_t *text, size_t length, size_t fallback) {
			size_t pos = length;
			while (pos && (text[pos - 1] == L' ' || text[pos - 1] == L'\t')) --pos;
			size_t token_end = pos;
			while (pos)
			{
				wchar_t ch = text[pos - 1];
				if (!((ch >= L'a' && ch <= L'z') || (ch >= L'A' && ch <= L'Z')
					|| (ch >= L'0' && ch <= L'9') || ch == L'\''))
					break;
				--pos;
			}
			if (pos != token_end)
				return pos;
			return length > fallback ? length - fallback : 0;
		};
		size_t raw_restore_length = preedit_start(sRawBuffer.c_str(),
			sRawBuffer.size(), preedit.size());
		sRawBuffer.resize(raw_restore_length);
		if (g_input && g_input->InProgress())
		{
			bool had_deferred_candidate = std::any_of(sInputNotifies.begin(), sInputNotifies.end(),
				[](const InputNotify &notify) {
					return notify.input == g_input && notify.kind == NOTIFY_CHAR
						&& notify.ime_candidate;
				});
			// A late listener can miss the chance to defer the first physical key;
			// only then is there anything to roll back from InputHook.Buffer.
			if (!had_deferred_candidate)
			{
				int input_restore_length = (int)preedit_start(g_input->Buffer,
					(size_t)g_input->BufferLength, preedit.size());
				g_input->BufferLength = input_restore_length;
				g_input->Buffer[input_restore_length] = L'\0';
			}
			// Drop physical candidates while preserving KeyDown/KeyUp semantics.
			sInputNotifies.erase(std::remove_if(sInputNotifies.begin(), sInputNotifies.end(),
				[](const InputNotify &notify) {
					return notify.input == g_input && notify.kind == NOTIFY_CHAR
						&& notify.ime_candidate;
				}), sInputNotifies.end());
		}
	}
	sImePreeditActive = active;
}

void LinuxCaptureImeCommit(const char *aUtf8, AhkInputOrigin aOrigin)
{
	std::wstring committed;
	if (!LinuxImeUtf8ToWide(aUtf8, committed) || committed.empty())
	{
		sImePreeditActive = false;
		return;
	}
	sImePreeditActive = false;
	sImePhysicalSuppressUntilUs = LinuxInputEventMonotonicUs() + 200000ULL;
	sPendingImeHotstringChars.clear();
	// DBus dispatch must not invoke script callbacks recursively. Queue the
	// immutable commit and consume it from LinuxCaptureDispatchInputNotifies(),
	// which is already the safe InputHook callback context.
	sPendingImeCommits.push_back(PendingImeCommit{committed, aOrigin});
	if (g_input && g_input->InProgress())
		sInputNotifies.erase(std::remove_if(sInputNotifies.begin(), sInputNotifies.end(),
			[](const InputNotify &notify) {
				return notify.input == g_input && notify.kind == NOTIFY_CHAR
					&& notify.ime_candidate;
			}), sInputNotifies.end());
}

wchar_t LinuxCharFromKeySym(KeySym aKs)
{
	if (aKs >= 0x20 && aKs <= 0x7e)
		return (wchar_t)aKs;
	if (aKs >= 0xa0 && aKs <= 0xff)
		return (wchar_t)aKs; // Latin-1 supplement (é, ü, ¥, ...).
	if (aKs >= 0x01000000u && aKs <= 0x0110ffffu)
		return (wchar_t)(aKs - 0x01000000u); // Unicode keysym range.
	switch (aKs)
	{
	case XK_Return: case XK_KP_Enter: return L'\n';
	case XK_Tab: return L'\t';
	case XK_BackSpace: return L'\b';
	}
	return 0;
}

static void LinuxInputCollectAndNotify(input_type *aInput, wchar_t aChar)
{
	if (!aInput || !aInput->InProgress())
		return;
	auto *script_input = aInput->ScriptObject;
	IObject *script_object = script_input;
	IObject *callback = script_input ? script_input->onChar : nullptr;
	IObjectRef script_guard(script_object);
	IObjectRef callback_guard(callback);
	TCHAR text[2] = { (TCHAR)aChar, 0 };
	aInput->CollectChar(text, 1);
	// Collect first so OnChar observes Input including this character. The
	// guards keep both objects valid when CollectChar ends the hook.
	if (callback_guard)
	{
		ExprTokenType params[] = { script_guard.ToObject(), text };
		callback_guard->ExecuteInNewThread(_T("InputHook"), params, _countof(params));
	}
}

static void LinuxCaptureDispatchImeCommits()
{
	if (sPendingImeCommits.empty())
		return;
	std::vector<PendingImeCommit> commits;
	commits.swap(sPendingImeCommits);
	for (const auto &commit : commits)
		for (wchar_t ch : commit.text)
		{
			AhkInputEvent event = {
				LinuxInputEventMonotonicUs(), 0, 0, 0, (char32_t)ch,
				false, false, AhkInputSource::IME_COMMIT, -1, 0, commit.origin
			};
			LinuxInputEventTrace(event);
			if (g_input && g_input->InProgress())
				LinuxInputCollectAndNotify(g_input, ch == L'\n' ? L'\r' : ch);
			else if (Hotstring::sEnabledCount)
			{
				AhkLinuxKeyIdentity key {};
				key.text = (uint32_t)ch;
				LinuxRawFeedHotstrings(LinuxHotkeyDisplay(), key, -1);
			}
		}
}

static void LinuxCaptureDispatchImeHotstringChars()
{
	if (sPendingImeHotstringChars.empty())
		return;
	uint64_t now = LinuxInputEventMonotonicUs();
	std::vector<PendingImeHotstringChar> waiting;
	waiting.reserve(sPendingImeHotstringChars.size());
	for (const auto &candidate : sPendingImeHotstringChars)
		if (candidate.ready_us <= now)
		{
			if (!sImePreeditActive)
				LinuxRawFeedHotstrings(LinuxHotkeyDisplay(), candidate.key, candidate.self_level);
		}
		else
			waiting.push_back(candidate);
	sPendingImeHotstringChars.swap(waiting);
}

// Fire the queued InputHook notifications (see the queue comment above).
void LinuxCaptureDispatchInputNotifies()
{
	LinuxCaptureDispatchImeCommits();
	LinuxCaptureDispatchImeHotstringChars();
	if (sInputNotifies.empty())
		return;
	uint64_t now = LinuxInputEventMonotonicUs();
	std::vector<InputNotify> batch;
	std::vector<InputNotify> waiting;
	batch.reserve(sInputNotifies.size());
	waiting.reserve(sInputNotifies.size());
	for (const auto &notify : sInputNotifies)
		(notify.ready_us <= now ? batch : waiting).push_back(notify);
	sInputNotifies.swap(waiting); // Callbacks may append new notifications.
	for (size_t i = 0; i < batch.size(); ++i)
	{
		InputNotify &n = batch[i];
		// Only the CURRENT hook may be notified (see the queue comment).
		if (n.input != g_input || !n.input->InProgress() || !n.input->ScriptObject)
			continue;
		if (n.kind == NOTIFY_CHAR && n.ime_candidate)
		{
			LinuxInputCollectAndNotify(n.input, n.ch);
			continue;
		}
		IObject *cb = n.kind == NOTIFY_CHAR ? n.input->ScriptObject->onChar
			: (n.kind == NOTIFY_KEYDOWN ? n.input->ScriptObject->onKeyDown
			                            : n.input->ScriptObject->onKeyUp);
		if (!cb)
			continue;
		if (n.kind == NOTIFY_CHAR)
		{
			// Windows semantics: OnChar(This, Char).
			TCHAR chars[2] = { n.ch, L'\0' };
			ExprTokenType params[] = { n.input->ScriptObject, chars };
			IObjectPtr(cb)->ExecuteInNewThread(_T("InputHook"), params, _countof(params));
		}
		else
		{
			// Windows semantics: OnKeyDown/OnKeyUp(This, VK, SC).
			ExprTokenType params[] =
			{
				n.input->ScriptObject,
				(__int64)n.vk,
				(__int64)n.sc,
			};
			IObjectPtr(cb)->ExecuteInNewThread(_T("InputHook"), params, _countof(params));
		}
	}
}

void LinuxCaptureRawKeyEvent(Display *d, KeyCode aKeycode, bool aIsPress,
	Time aTime, unsigned int aCoreState, int aSelfLevel, bool aIsSendInput,
	AhkInputSource aSource, uint32_t aDeviceId)
{
	bool capture = LinuxCaptureUsesRaw();
	if (!capture && !LinuxInputEventTraceEnabled())
		return;
	XEvent ev;
	memset(&ev, 0, sizeof(ev));
	ev.type = aIsPress ? KeyPress : KeyRelease;
	ev.xkey.type = ev.type;
	ev.xkey.display = d;
	ev.xkey.keycode = aKeycode;
	ev.xkey.state = aCoreState;
	ev.xkey.time = aTime;
	ev.xkey.same_screen = True;
	AhkLinuxKeyIdentity identity;
	LinuxEventKeyIdentity(d, ev, identity);
	AhkInputEvent normalized = {
		LinuxInputEventMonotonicUs(), identity.evdev_code, identity.vk,
		identity.sc, (char32_t)identity.text, !aIsPress, false, aSource,
		(int16_t)aSelfLevel, aDeviceId, AhkInputOrigin::X11
	};
	LinuxInputEventTrace(normalized);
	if (!capture || aIsSendInput)
		return;
	if (g_input && g_input->InProgress())
	{
		// Selected suppression keys are fed by their normal passive-grab event;
		// raw must stand aside or callbacks/buffer would receive them twice.
		if (LinuxCaptureKeycodeNeedsGrab(d, aKeycode))
			return;
		if (aSelfLevel >= 0 && g_input->MinSendLevel > 0
			&& aSelfLevel < (int)g_input->MinSendLevel)
			return;
		LinuxCaptureFeedInput(d, ev, &identity);
		return;
	}
	if (!aIsPress || aSelfLevel == 0)
		return;
	if (identity.text && LinuxInputEventMonotonicUs() < sImePhysicalSuppressUntilUs)
		return;
	if (!identity.text)
	{
		if (!LinuxIsModifierKey(identity.keysym))
		{
			sRawBuffer.clear();
			sPendingImeHotstringChars.clear();
		}
		return;
	}
	if (LinuxImeCommitCaptureActive())
	{
		sPendingImeHotstringChars.push_back(PendingImeHotstringChar{
			identity, aSelfLevel, LinuxInputEventMonotonicUs() + 500000ULL
		});
		return;
	}
	LinuxRawFeedHotstrings(d, identity, aSelfLevel);
}

bool LinuxInputHookKeyNeedsGrab(Display *d, KeyCode aKeycode)
{
	input_type *input = g_input;
	if (!input || !input->InProgress())
		return false;
	AhkLinuxKeyIdentity key;
	if (!LinuxKeyModelX11Decode(d, aKeycode, 0, key))
		return false;
	UCHAR flags = input->KeyVK[key.vk] | input->KeySC[key.sc];
	if (flags & INPUT_KEY_VISIBILITY_MASK)
		return (flags & INPUT_KEY_SUPPRESS) != 0;
	if (LinuxIsModifierKey(key.keysym))
		return false;
	if (key.vk == VK_BACK && input->BackspaceIsUndo)
		return !input->VisibleText;
	bool text = LinuxKeyModelX11KeyProducesText(d, aKeycode)
		&& !(flags & INPUT_KEY_IGNORE_TEXT);
	return !(text ? input->VisibleText : input->VisibleNonText);
}

void LinuxCaptureRefreshGrabKeycodes(Display *d)
{
	if (!sGrabKeycodesDirty)
		return;
	sGrabKeycodes.clear();
	if (InputHookNeedsGrabs() && d)
		for (int code = 8; code <= 255; ++code)
			if (LinuxInputHookKeyNeedsGrab(d, (KeyCode)code))
				sGrabKeycodes.insert((KeyCode)code);
	sGrabKeycodesDirty = false;
	static bool warned_broad = false;
	if (!warned_broad && sGrabKeycodes.size() > 96)
	{
		fprintf(stderr, "AHK warning: InputHook suppression requires %zu X11 passive key grabs; use option V plus KeyOpt(..., 'S') to narrow suppression.\n"
			, sGrabKeycodes.size());
		warned_broad = true;
	}
}

bool LinuxCaptureNeedsGrabs()
{
	return InputHookNeedsGrabs();
}

bool LinuxCaptureKeycodeNeedsGrab(Display *d, KeyCode aKeycode)
{
	LinuxCaptureRefreshGrabKeycodes(d);
	return sGrabKeycodes.count(aKeycode) != 0;
}

void LinuxCaptureGrabKeycodes(Display *d, std::set<KeyCode> &aOut)
{
	LinuxCaptureRefreshGrabKeycodes(d);
	aOut.insert(sGrabKeycodes.begin(), sGrabKeycodes.end());
}

void LinuxCaptureKeymapChanged()
{
	sGrabKeycodesDirty = true;
}

bool LinuxCaptureUsesRaw()
{
	return Hotstring::sEnabledCount > 0 || (g_input && g_input->InProgress());
}

bool LinuxCaptureActive()
{
	return sActive;
}

void LinuxCaptureStateChanged()
{
	bool want = Hotstring::sEnabledCount > 0 || (g_input && g_input->InProgress());
	bool needs_grabs = InputHookNeedsGrabs();
	bool mode_changed = want != sActive || needs_grabs != sNeedsGrabs;
	sGrabKeycodesDirty = true; // KeyOpt may change the set without mode change.
	if (mode_changed)
	{
		sActive = want;
		sNeedsGrabs = needs_grabs;
		sHeldCount = 0;
		sBufLen = 0;
		sRawBuffer.clear();
	}
	if (mode_changed || needs_grabs)
	{
		LinuxCaptureMappingNotify(); // also rebuild the selected GrabSpec set.
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

// Character a key press produces for the InputHook stream: printable
// ASCII/Latin-1/Unicode at the current shift level (Unicode keysyms from
// IME-composed keys and the Send engine's borrowed keycodes are converted
// by LinuxCharFromKeySym), plus the control characters for the named keys
// (Return collects CR, matching Windows).
wchar_t LinuxInputHookChar(const AhkLinuxKeyIdentity &aKey)
{
	wchar_t c = (wchar_t)aKey.text;
	if (c == L'\n') // Enter: Windows InputHook collects CR.
		return L'\r';
	return c;
}

// Feed one key event to the active InputHook (live key capture on Linux via
// the same all-keys grab machinery the hotstring engine uses).  Consumed
// events are not forwarded to the target window, matching Windows Input.
// Supported: buffer fill, end chars, match list, buffer limit, backspace
// undo, and the OnChar notification.  Named VK end-keys and the OnKeyDown
// VK/SC arguments remain documented limitations (single-char end keys and
// the default end behaviour are covered).
bool LinuxCaptureFeedInput(Display *d, XEvent &ev
	, const AhkLinuxKeyIdentity *aPredecoded)
{
	input_type *active = g_input;
	if (!active || !active->InProgress())
		return false;
	AhkLinuxKeyIdentity decoded;
	if (!aPredecoded)
	{
		LinuxEventKeyIdentity(d, ev, decoded);
		aPredecoded = &decoded;
	}
	const AhkLinuxKeyIdentity &key = *aPredecoded;
	if (ev.type != KeyPress)
	{
		// KeyRelease: notify canonical logical VK + set-1 scan code.
		if (ev.type == KeyRelease && active->ScriptObject && active->ScriptObject->onKeyUp)
			LinuxInputNotifyQueue(active, NOTIFY_KEYUP, key.vk, key.sc, 0);
		return true; // Releases are consumed while the input is active.
	}
	if (active->ScriptObject && active->ScriptObject->onKeyDown)
		LinuxInputNotifyQueue(active, NOTIFY_KEYDOWN, key.vk, key.sc, 0);
	// Modifier keys do not join the character stream, but Windows InputHook
	// still reports them through OnKeyDown/OnKeyUp.
	if (LinuxIsModifierKey(key.keysym))
		return true;
	wchar_t ch = LinuxInputHookChar(key);
	// KeyDown/EndKey above remain physical-key driven, but preedit text and its
	// Backspaces never mutate the character buffer. CommitText is fed later.
	if (ch && (sImePreeditActive
		|| LinuxInputEventMonotonicUs() < sImePhysicalSuppressUntilUs))
		return true;
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
	if (key.vk || key.sc)
	{
		bool with_shift = (ev.xkey.state & ShiftMask) != 0;
		UCHAR flag = with_shift ? END_KEY_WITH_SHIFT : END_KEY_WITHOUT_SHIFT;
		UCHAR vk_flags = active->KeyVK[key.vk];
		UCHAR sc_flags = active->KeySC[key.sc];
		if ((vk_flags | sc_flags) & flag)
		{
			bool by_sc = (sc_flags & flag) && (key.sc || !(vk_flags & flag));
			active->EndByKey(key.vk, key.sc, by_sc, with_shift);
			return true;
		}
	}
	if (ch)
	{
		// A composing engine needs one bounded race window: defer both CollectChar
		// and OnChar so raw phonetic keys cannot satisfy Match/Limit before the
		// toolkit emits preedit. Preedit/commit drops this candidate; no signal
		// within 500ms turns it back into an ordinary physical character.
		if (LinuxImeCommitCaptureActive())
			LinuxInputNotifyQueue(active, NOTIFY_CHAR, 0, 0, ch, true);
		else
		{
			if (active->ScriptObject && active->ScriptObject->onChar)
				LinuxInputNotifyQueue(active, NOTIFY_CHAR, 0, 0, ch);
			TCHAR cbuf[2] = { (TCHAR)ch, 0 };
			active->CollectChar(cbuf, 1); // Ends on end char/match/limit internally.
		}
	}
	return true;
}

bool LinuxCaptureKeyEvent(Display *d, XEvent &ev, int aSelfLevel)
{
	if (!LinuxCaptureActive())
		return false;
	if (LinuxCaptureUsesRaw() && !LinuxCaptureKeycodeNeedsGrab(d, ev.xkey.keycode))
		return false;

	// An active InputHook captures the typed-text stream and consumes every
	// grabbed event (presses are fed; releases discarded) -- it wins over
	// hotstrings while in progress, like Windows.
	if (g_input && g_input->InProgress())
	{
		// MinSendLevel (InputHook "I" option, check_detail0821 §2-C): input
		// this process generated at a SendLevel below the hook's MinSendLevel
		// is ignored (consumed by the capture grab but not fed); physical
		// input (aSelfLevel < 0) always feeds.
		if (aSelfLevel >= 0 && g_input->MinSendLevel > 0
			&& aSelfLevel < (int)g_input->MinSendLevel)
			return true;
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

	// KeyPress: one normalized physical/logical/text decode for every consumer.
	AhkLinuxKeyIdentity key;
	LinuxEventKeyIdentity(d, ev, key);
	if (LinuxIsModifierKey(key.keysym))
		return false; // Modifiers flow through; they do not break matching.

	wchar_t ch = (wchar_t)key.text;
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
		LinuxCaptureFire(d, best, case_mode, best_endchar ? ch : 0, forward_cur, ev);
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
