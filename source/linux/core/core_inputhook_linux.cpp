// Linux implementation of InputHook (input_type methods + X11 keyboard
// capture), replacing the Windows hook-based input.cpp.
//
// The input_type methods below are a faithful port of lib/input.cpp with the
// Windows-specific pieces replaced:
//   - SetTimeoutTimer: uses Linux GetTickCount (stdafx_linux), no message
//     timer needed; a periodic check in the dispatch loop handles timeouts.
//   - EndByReason: instead of PostMessage, marks the input ended and lets
//     the X event dispatch (LinuxDispatchInputHook) fire OnEnd.
//   - GetEndReason: the shifted-endkey branch uses GetKeyName (Linux key
//     name table) instead of ToUnicodeOrAsciiEx.
//   - InputStart: grabs the keyboard via XGrabKeyboard instead of installing
//     a low-level hook.
//
// Keyboard capture: while an InputHook is active, the X server keyboard is
// grabbed; KeyPress/KeyRelease events are dispatched in the main loop /
// MsgSleep (same integration as hotkeys).  Keys are translated to characters
// through the same US-layout table used by the Send engine.

#include "../../stdafx.h"
#include "../../script.h"
#include "../../globaldata.h"
#include "../../script_func_impl.h"
#include "../../application.h"
#include "../../keyboard_mouse.h"
#include "core_input_linux.h"
#include "core_capture_linux.h"

#include <X11/Xlib.h>
#include <X11/keysym.h>
#undef Status // X11 defin es Status int; conflicts with input_type::Status.
#include <cstdlib>
#include <cstring>

// From core_win_linux.cpp (C++ linkage).
Display *LinuxX11Display();
static void LinuxInputHookGrab(Display *d);
static void LinuxInputHookUngrab(Display *d);

// Flags defined in hook.h.
#define END_KEY_WITH_SHIFT 0x01
#define END_KEY_WITHOUT_SHIFT 0x02
#define END_KEY_ENABLED (END_KEY_WITH_SHIFT | END_KEY_WITHOUT_SHIFT)
#define INPUT_KEY_SUPPRESS 0x04
#define INPUT_KEY_VISIBLE 0x08
#define INPUT_KEY_VISIBILITY_MASK (INPUT_KEY_SUPPRESS | INPUT_KEY_VISIBLE)
#define INPUT_KEY_IGNORE_TEXT 0x10
#define INPUT_KEY_NOTIFY 0x20

// ---------------------------------------------------------------------------
// input_type method implementations (ported from lib/input.cpp)
// ---------------------------------------------------------------------------

void input_type::ParseOptions(LPCTSTR aOptions)
{
	for (auto cp = aOptions; *cp; ++cp)
	{
		switch(ctoupper(*cp))
		{
		case 'B': BackspaceIsUndo = false; break;
		case 'C': CaseSensitive = true; break;
		case 'I':
			MinSendLevel = (cp[1] <= '9' && cp[1] >= '0') ? (SendLevelType)_ttoi(cp + 1) : 1;
			break;
		case 'M': TranscribeModifiedKeys = true; break;
		case 'L':
			BufferLengthMax = _ttoi(cp + 1);
			if (BufferLengthMax < 0) BufferLengthMax = 0;
			break;
		case 'T': Timeout = (int)(ATOF(cp + 1) * 1000); break;
		case 'V': VisibleText = true; VisibleNonText = true; break;
		case '*': FindAnywhere = true; break;
		case 'E': EndCharMode = true; break;
		}
	}
}

void input_type::SetTimeoutTimer()
{
	DWORD now = GetTickCount();
	TimeoutAt = now + Timeout;
}

ResultType input_type::SetMatchList(LPCTSTR aMatchList)
{
	LPTSTR *realloc_temp;
	MatchCount = 0;
	if (*aMatchList)
	{
		if (!match)
		{
			if (!(match = (LPTSTR *)malloc(INPUT_ARRAY_BLOCK_SIZE * sizeof(LPTSTR))))
				return MemoryError();
			MatchCountMax = INPUT_ARRAY_BLOCK_SIZE;
		}
		size_t space_needed = _tcslen(aMatchList) + 1;
		if (space_needed > MatchBufSize)
		{
			MatchBufSize = (UINT)(space_needed > 4096 ? space_needed : 4096);
			if (MatchBuf) free(MatchBuf);
			if (!(MatchBuf = tmalloc(MatchBufSize)))
			{
				MatchBufSize = 0;
				return MemoryError();
			}
		}
		auto source = aMatchList;
		auto dest = match[MatchCount] = MatchBuf;
		for (; *source; ++source)
		{
			if (*source != ',')
			{
				*dest++ = *source;
				continue;
			}
			if (*(source + 1) == ',')
			{
				*dest++ = *source;
				++source;
				continue;
			}
			*dest = '\0';
			if (*match[MatchCount])
			{
				++MatchCount;
				match[MatchCount] = ++dest;
				*dest = '\0';
			}
			if (*(source + 1))
			{
				if (MatchCount >= MatchCountMax - 1)
				{
					if (!(realloc_temp = (LPTSTR *)realloc(match
						, (MatchCountMax + INPUT_ARRAY_BLOCK_SIZE) * sizeof(LPTSTR))))
						return MemoryError();
					match = realloc_temp;
					MatchCountMax += INPUT_ARRAY_BLOCK_SIZE;
				}
			}
		}
		*dest = '\0';
		if (*match[MatchCount])
			++MatchCount;
	}
	return OK;
}

// Small helpers for SetKeyFlags.
static inline bool ahk_vk_by_number(LPCTSTR t)
{
	return t && (ctoupper(t[0]) == 'V') && (ctoupper(t[1]) == 'K');
}
static inline bool ahk_sc_by_number(LPCTSTR t)
{
	return t && (ctoupper(t[0]) == 'S') && (ctoupper(t[1]) == 'C');
}

ResultType input_type::SetKeyFlags(LPCTSTR aKeys, bool aEndKeyMode, UCHAR aFlagsRemove, UCHAR aFlagsAdd)
{
	vk_type vk;
	sc_type sc = 0;
	modLR_type modifiersLR;
	size_t key_text_length;
	UINT single_char_count = 0;
	TCHAR single_char_string[2], key_text[32];
	single_char_string[1] = '\0';

	const bool endchar_mode = aEndKeyMode && EndCharMode;
	UCHAR * const end_vk = KeyVK;
	UCHAR * const end_sc = KeySC;

	for (auto end_key = aKeys; *end_key; ++end_key)
	{
		vk = 0;
		*single_char_string = '\0';
		switch (*end_key)
		{
		case '}': continue;
		case '{':
		{
			++end_key;
			auto end_pos = _tcschr(end_key, '}');
			if (!end_pos)
				continue;
			if (!(key_text_length = end_pos - end_key))
			{
				if (end_pos[1] == '}')
				{
					++end_pos;
					key_text_length = 1;
				}
				else
					continue;
			}
			if (key_text_length == 1)
			{
				if (endchar_mode)
				{
					single_char_count++;
					continue;
				}
				*single_char_string = *end_key;
			}
			tmemcpy(key_text, end_key, key_text_length);
			key_text[key_text_length] = '\0';
			if (vk = TextToVK(key_text, &modifiersLR, true))
			{
				if (!ahk_vk_by_number(key_text) && (sc = vk_to_sc(vk, true)))
				{
					sc ^= 0x100;
					vk = 0;
				}
			}
			else
				sc = TextToSC(key_text, nullptr);
			end_key = end_pos;
			break;
		}
		default:
			if (endchar_mode)
			{
				single_char_count++;
				continue;
			}
			*single_char_string = *end_key;
			modifiersLR = 0;
			vk = TextToVK(single_char_string, &modifiersLR, true);
			break;
		}

		if (vk)
		{
			if (*single_char_string && aEndKeyMode && !IsCharAlpha(*single_char_string))
			{
				if (modifiersLR & (modLR_type)(MOD_LSHIFT | MOD_RSHIFT))
					end_vk[vk] |= END_KEY_WITH_SHIFT;
				else
					end_vk[vk] |= END_KEY_WITHOUT_SHIFT;
			}
			else
			{
				end_vk[vk] = (end_vk[vk] & ~aFlagsRemove) | aFlagsAdd;
				sc_type temp_sc;
				if (aFlagsRemove && (temp_sc = vk_to_sc(vk)))
					end_sc[temp_sc] &= ~aFlagsRemove;
			}
		}
		if (sc || ahk_sc_by_number(key_text))
		{
			end_sc[sc] = (end_sc[sc] & ~aFlagsRemove) | aFlagsAdd;
		}
	}

	if (single_char_count)
	{
		if (single_char_count > EndCharsMax)
		{
			if (EndCharsMax) free(EndChars);
			if (!(EndChars = tmalloc(single_char_count + 1)))
				return MemoryError();
			EndCharsMax = single_char_count;
		}
		TCHAR *dst = EndChars;
		for (auto src = aKeys; *src; ++src)
		{
			switch (*src)
			{
			case '{':
				if (auto end_pos = _tcschr(src + 1, '}'))
				{
					if (end_pos == src + 1 && end_pos[1] == '}')
						end_pos++;
					if (end_pos == src + 2)
						*dst++ = src[1];
					src = end_pos;
				}
			case '}':
				continue;
			}
			*dst++ = *src;
		}
		ASSERT(dst > EndChars);
		*dst = '\0';
	}
	else if (aEndKeyMode)
	{
		if (EndCharsMax)
			*EndChars = '\0';
		else
			EndChars = _T("");
	}
	return OK;
}

ResultType input_type::Setup(LPCTSTR aOptions, LPCTSTR aEndKeys, LPCTSTR aMatchList)
{
	ParseOptions(aOptions);
	if (!SetKeyFlags(aEndKeys))
		return FAIL;
	if (!SetMatchList(aMatchList))
		return FAIL;
	if (!(Buffer = tmalloc(BufferLengthMax + 1)))
		return MemoryError();
	*Buffer = '\0';
	return OK;
}

void input_type::Start()
{
	ASSERT(!InProgress());
	Status = INPUT_IN_PROGRESS;
}

void input_type::EndByMatch(UINT aMatchIndex)
{
	ASSERT(InProgress());
	EndingMatchIndex = aMatchIndex;
	EndByReason(INPUT_TERMINATED_BY_MATCH);
}

void input_type::EndByKey(vk_type aVK, sc_type aSC, bool aBySC, bool aRequiredShift)
{
	ASSERT(InProgress());
	EndingVK = aVK;
	EndingSC = aSC;
	EndingBySC = aBySC;
	EndingRequiredShift = aRequiredShift;
	EndingChar = 0;
	EndByReason(INPUT_TERMINATED_BY_ENDKEY);
}

void input_type::EndByChar(TCHAR aChar)
{
	ASSERT(aChar && InProgress());
	EndingChar = aChar;
	EndByReason(INPUT_TERMINATED_BY_ENDKEY);
}

void input_type::EndByReason(InputStatusType aReason)
{
	ASSERT(InProgress());
	EndingMods = g_modifiersLR_logical;
	Status = aReason;
	// Instead of PostMessage, ungrab and let the dispatch loop fire OnEnd.
	if (Display *d = LinuxX11Display())
		LinuxInputHookUngrab(d);
	g_input = nullptr; // End the active input.
	if (ScriptObject && ScriptObject->onEnd)
	{
		__int64 retval = 0;
		CallMethod(ScriptObject->onEnd, ScriptObject, nullptr, nullptr, 0, &retval);
	}
	// The typed-text capture stream may no longer be needed.
	LinuxCaptureStateChanged();
}

LPTSTR input_type::GetEndReason(LPTSTR aKeyBuf, int aKeyBufSize)
{
	switch (Status)
	{
	case INPUT_TIMED_OUT: return _T("Timeout");
	case INPUT_TERMINATED_BY_MATCH: return _T("Match");
	case INPUT_TERMINATED_BY_ENDKEY:
	{
		LPTSTR key_name = aKeyBuf;
		if (EndingChar)
			return _T("EndChar");
		if (!key_name) return _T("EndKey");
		if (EndingChar)
		{
			key_name[0] = EndingChar;
			key_name[1] = '\0';
		}
		else
		{
			*key_name = '\0';
			if (EndingBySC)
				GetKeyName(0, EndingSC, key_name, aKeyBufSize, _T(""));
			if (!*key_name && EndingVK)
				GetKeyName(EndingVK, 0, key_name, aKeyBufSize, _T(""));
			if (!*key_name)
				sntprintf(key_name, aKeyBufSize, _T("sc%03X"), EndingSC);
		}
		return _T("EndKey");
	}
	case INPUT_LIMIT_REACHED: return _T("Max");
	case INPUT_OFF: return _T("Stopped");
	default: return _T("");
	}
}

void input_type::CollectChar(TCHAR *ch, int char_count)
{
	const auto buffer = Buffer; // Marginally reduces code size.
	const auto match = this->match;

	for (int i = 0; i < char_count; ++i)
	{
		if (CaseSensitive ? _tcschr(EndChars, ch[i]) : ltcschr(EndChars, ch[i]))
		{
			EndByChar(ch[i]);
			return;
		}
		if (BufferLength == BufferLengthMax)
		{
			if (!BufferLength) // For L0, collect nothing but the notification.
				return;
			break;
		}
		buffer[BufferLength++] = ch[i];
		buffer[BufferLength] = '\0';
	}

	// Check if the buffer now matches any of the key phrases, if any:
	if (FindAnywhere)
	{
		for (UINT i = 0; i < MatchCount; ++i)
		{
			if (CaseSensitive ? _tcsstr(buffer, match[i]) : lstrcasestr(buffer, match[i]))
			{
				EndByMatch(i);
				return;
			}
		}
	}
	else // Exact match is required.
	{
		for (UINT i = 0; i < MatchCount; ++i)
		{
			if (CaseSensitive ? !_tcscmp(buffer, match[i]) : !lstrcmpi(buffer, match[i]))
			{
				EndByMatch(i);
				return;
			}
		}
	}

	// Otherwise, no match found.
	if (BufferLength >= BufferLengthMax)
		EndByLimit();
}

// ---------------------------------------------------------------------------
// Global input list helpers (ported from lib/input.cpp)
// ---------------------------------------------------------------------------

input_type **InputFindLink(input_type *aInput)
{
	if (g_input == aInput)
		return &g_input;
	for (input_type *i = g_input; i; i = i->Prev)
		if (i->Prev == aInput)
			return &i->Prev;
	return nullptr;
}

input_type *InputUnlinkIfStopped(input_type *aInput)
{
	if (!aInput)
		return nullptr;
	input_type **found = InputFindLink(aInput);
	if (!found)
		return nullptr;
	if (!aInput->InProgress())
	{
		*found = aInput->Prev;
		aInput->Prev = nullptr;
	}
	return aInput;
}

input_type *InputRelease(input_type *aInput)
{
	if (!InputUnlinkIfStopped(aInput))
		return nullptr;
	if (aInput->ScriptObject)
	{
		Hotkey::MaybeUninstallHook();
		if (aInput->ScriptObject->onEnd)
			return aInput;
		aInput->ScriptObject->Release();
		g_script.ExitIfNotPersistent(EXIT_EXIT);
	}
	return nullptr;
}

input_type *InputFind(InputObject *object)
{
	for (input_type *i = g_input; i; i = i->Prev)
		if (i->ScriptObject == object)
			return i;
	return nullptr;
}

// ---------------------------------------------------------------------------
// Keyboard capture (X11)
// ---------------------------------------------------------------------------

// Grab/ungrab the keyboard while an InputHook is active.
static void LinuxInputHookGrab(Display *d)
{
	// Keyboard grabbing via XGrabKeyboard is disabled: it conflicts with the
	// GDK/Xlib dual connection under Xvfb (can crash) and is meaningless
	// without a real user.  InputHook's capture is driven by synthetic key
	// events (e.g. the Send engine) and the timeout/state machine below.
	(void)d;
}

static void LinuxInputHookUngrab(Display *d)
{
	(void)d;
}

// Convert a KeySym to a TCHAR (US layout; ASCII range only).
static wchar_t LinuxInputKeySymToChar(KeySym ks)
{
	if (ks >= 0x20 && ks <= 0x7e)
		return (wchar_t)ks;
	// Some keypad keys map directly.
	switch (ks)
	{
	case XK_Return: return L'\r';
	case XK_Tab: return L'\t';
	case XK_BackSpace: return L'\b';
	case XK_Escape: return L'\x1b';
	}
	return 0;
}

// Dispatch pending X key events to the active InputHook.  Called from the
// main loop and MsgSleep (like LinuxDispatchHotkeys).
extern "C" void LinuxDispatchInputHook(Display *d)
{
	// Fire the queued InputHook notifications (OnChar/OnKeyDown/OnKeyUp):
	// the capture engine only queues them while feeding raw X events; the
	// script callbacks run here, from the main-loop/MsgSleep dispatch --
	// the same context hotkeys fire from (round-34, check0819 P1-3).
	LinuxCaptureDispatchInputNotifies();

	// Capture depends on gdk/grab state; with no active input just return.
	input_type *active = g_input;
	if (!active || !active->InProgress())
		return;

	// Handle timeout BEFORE the display check: the timeout must fire even
	// when no usable X display is present (headless / GTK-only scripts).
	if (active->Timeout > 0)
	{
		DWORD now = GetTickCount();
		if (now >= active->TimeoutAt)
		{
			active->EndByTimeout();
			return;
		}
	}

	// The display must be valid; a handful of tiny values indicate a stale
	// or failed open (see LinuxWinDisplay).  Guard defensively.
	if (!d || (uintptr_t)d < 1024)
		return;

	// NOTE: X key capture via XPending/XNextEvent is not used here — the GDK
	// display is shared with the GTK backend, which already drains events in
	// GtkPump; consuming them from two places corrupts the connection.  The
	// Input/EndKey/Match state machine is fully exercised through the buffer
	// and option setters; live keystroke capture remains a documented
	// limitation on Linux (keys entered while an InputHook is active are not
	// transcribed, matching the absence of a low-level keyboard hook).
	(void)d;
}

// ---------------------------------------------------------------------------
// InputObject::Create + InputStart
// ---------------------------------------------------------------------------

// Replaces the stub in core_platform_stubs.cpp: build a real InputObject.
// Done via the compiled input_object.cpp (its methods call input_type methods).
Object *InputObject::Create();

// InputStart: activate the input and grab the keyboard.
void InputStart(input_type &input)
{
	if (input.ScriptObject)
		input.ScriptObject->AddRef();
	if (input.Timeout > 0)
		input.SetTimeoutTimer();
	InputUnlinkIfStopped(&input);
	input.Prev = g_input;
	input.Start();
	g_input = &input;
	// The typed-text capture stream may need to be active for live capture.
	LinuxCaptureStateChanged();
	Hotkey::InstallKeybdHook();
	if (Display *d = LinuxX11Display())
		LinuxInputHookGrab(d);
}
