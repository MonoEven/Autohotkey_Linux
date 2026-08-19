// Unified input backend selection + routing (see input_backend.h).
//
// This file owns the *decision* of which backend handles global hotkeys.
// Backends stay independent:
//   - X11:        core_hotkey_linux.cpp (XGrabKey/XRecord) - untouched.
//   - Portal:     core_gshortcut_linux.cpp - frozen (no GNOME-49 hacks).
//   - GNOME Shell: input_backend_gnome_shell.cpp - thin D-Bus broker.
//   - evdev:      future ahk-inputd lane (reports "not installed" today).
//
// Selection policy (see header): explicit AHK_INPUT_BACKEND wins,
// AHK_FORCE_GLOBAL_SHORTCUTS=1 means "require the Portal backend" and wins
// over auto; auto picks X11 for X11 sessions and gnome-shell (when the
// extension is on the bus) or Portal for Wayland sessions.
#include "input_backend.h"
#include "core_gshortcut_linux.h"
#include "input_backend_gnome_shell.h"
#include "../../script.h"
#include "../../globaldata.h"
#include "../../script_func_impl.h"
#include "../../hotkey.h"
#include "../../keyboard_mouse.h"
#include "../../application.h"
#include <cstring>
#include <cstdlib>
#include <string>

namespace
{

wchar_t sLastErrorBuf[512] = { 0 };

void SetError(const char *aText)
{
	mbstowcs(sLastErrorBuf, aText ? aText : "", _countof(sLastErrorBuf) - 1);
	sLastErrorBuf[_countof(sLastErrorBuf) - 1] = 0;
}

bool SessionIsWayland()
{
	const char *st = getenv("XDG_SESSION_TYPE");
	if (st && !strcmp(st, "wayland"))
		return true;
	if (getenv("WAYLAND_DISPLAY"))
		return true;
	return false;
}

bool DesktopIsGNOME()
{
	const char *d = getenv("XDG_CURRENT_DESKTOP");
	if (!d)
		return false;
	// "GNOME", "GNOME:ubuntu", "ubuntu:GNOME", ...
	return strstr(d, "GNOME") != nullptr;
}

AhkInputBackendKind ResolveBackend()
{
	// Explicit override.
	if (const char *b = getenv("AHK_INPUT_BACKEND"))
	{
		if (!strcmp(b, "x11")) return AhkInputBackendKind::X11;
		if (!strcmp(b, "portal")) return AhkInputBackendKind::PORTAL;
		if (!strcmp(b, "gnome-shell")) return AhkInputBackendKind::GNOME_SHELL;
		if (!strcmp(b, "evdev")) return AhkInputBackendKind::EVDEV;
		if (strcmp(b, "auto") != 0)
			SetError("AHK_INPUT_BACKEND: unknown value (auto|x11|portal|gnome-shell|evdev)");
		// "auto" and unknown values fall through to auto selection.
	}
	// AHK_FORCE_GLOBAL_SHORTCUTS=1 keeps its documented meaning: explicitly
	// require the standard GlobalShortcuts Portal backend.  It is NOT reused
	// for the GNOME Shell backend, so it wins over auto selection.
	if (getenv("AHK_FORCE_GLOBAL_SHORTCUTS"))
		return AhkInputBackendKind::PORTAL;
	if (!SessionIsWayland())
		return AhkInputBackendKind::X11; // X11 session (or unknown).
	// Wayland session: prefer the GNOME Shell extension when it is actually
	// available on the session bus (zero-confirm, bare keys).
	if (DesktopIsGNOME() && LinuxGnomeShellAvailable())
		return AhkInputBackendKind::GNOME_SHELL;
	// KDE, GNOME without the extension, other compositors: portal baseline.
	return AhkInputBackendKind::PORTAL;
}

AhkInputBackendKind &CurrentKind()
{
	static AhkInputBackendKind sKind = ResolveBackend();
	return sKind;
}

const AhkInputBackendCaps *KindCaps(AhkInputBackendKind aKind)
{
	// Static table so pointers stay valid forever.
	static const AhkInputBackendCaps sX11 =
		{ true, true, true, true, true, true, true, true, true };
	static const AhkInputBackendCaps sPortal =
		{ true, true, false, false, false, false, false, true, false };
	static const AhkInputBackendCaps sGnomeShell =
		{ true, true, false, false, false, true, true, true, true };
	static const AhkInputBackendCaps sEvdev =
		{ false, false, false, false, false, false, false, false, false };
	switch (aKind)
	{
	case AhkInputBackendKind::X11: return &sX11;
	case AhkInputBackendKind::PORTAL: return &sPortal;
	case AhkInputBackendKind::GNOME_SHELL: return &sGnomeShell;
	default: return &sEvdev;
	}
}

} // namespace

// --- shared hotkey helpers (used by portal + gnome-shell backends) ----------

bool AhkBackendHotkeyEnabled(Hotkey *aHk)
{
	return aHk && !g_IsSuspended && (aHk->mType == HK_NORMAL || aHk->mType == HK_KEYBD_HOOK);
}

bool AhkBackendVariantFireable(Hotkey *aHk, HotkeyVariant *aV)
{
	if (!aV || !aV->mEnabled) return false;
	if (g_IsSuspended && !aV->mSuspendExempt) return false;
	if (aV->mHotCriterion && !HotCriterionAllowsFiring(aV->mHotCriterion, aHk->mName)) return false;
	return true;
}

void AhkBackendFireHotkey(Hotkey *aHk)
{
	if (!AhkBackendHotkeyEnabled(aHk)) return;
	HotkeyVariant *vp = nullptr;
	for (HotkeyVariant *v = aHk->mFirstVariant; v; v = v->mNextVariant)
		if (AhkBackendVariantFireable(aHk, v)) { vp = v; break; }
	if (!vp) return;
	++g_nThreads;
	++g;
	InitNewThread(vp->mPriority, false, false);
	aHk->PerformInNewThreadMadeByCaller(*vp);
	ResumeUnderlyingThread();
}

void WideToUtf8(const wchar_t *aIn, char *aOut, size_t aSize)
{
	size_t o = 0;
	for (const wchar_t *p = aIn; *p && o + 4 < aSize; ++p)
	{
		unsigned int cp = (unsigned int)*p;
		if (cp < 0x80) aOut[o++] = (char)cp;
		else if (cp < 0x800) { aOut[o++] = (char)(0xC0 | (cp >> 6)); aOut[o++] = (char)(0x80 | (cp & 0x3F)); }
		else if (cp < 0x10000) { aOut[o++] = (char)(0xE0 | (cp >> 12)); aOut[o++] = (char)(0x80 | ((cp >> 6) & 0x3F)); aOut[o++] = (char)(0x80 | (cp & 0x3F)); }
		else { aOut[o++] = (char)(0xF0 | (cp >> 18)); aOut[o++] = (char)(0x80 | ((cp >> 12) & 0x3F)); aOut[o++] = (char)(0x80 | ((cp >> 6) & 0x3F)); aOut[o++] = (char)(0x80 | (cp & 0x3F)); }
	}
	aOut[o] = '\0';
}

std::string AhkBackendComboForHotkey(Hotkey *aHk)
{
	std::string combo;
	unsigned int mods = (unsigned int)aHk->mModifiers;
	if (mods & MOD_CONTROL) combo += "<Control>";
	if (mods & MOD_ALT) combo += "<Alt>";
	if (mods & MOD_SHIFT) combo += "<Shift>";
	if (mods & MOD_WIN) combo += "<Super>";
	TCHAR kb[128] = { 0 };
	GetKeyName(aHk->mVK, 0, kb, _countof(kb), _T(""));
	if (kb[0])
	{
		char n[196];
		WideToUtf8(kb, n, sizeof(n));
		combo += n;
	}
	else
	{
		char n[32];
		snprintf(n, sizeof(n), "k%u", (unsigned)aHk->mVK);
		combo += n;
	}
	return combo;
}

// --- public API -------------------------------------------------------------

AhkInputBackendKind LinuxInputBackendKind()
{
	return CurrentKind();
}

const AhkInputBackendCaps *LinuxInputBackendCaps()
{
	return KindCaps(CurrentKind());
}

const char *LinuxInputBackendName()
{
	switch (CurrentKind())
	{
	case AhkInputBackendKind::X11: return "x11";
	case AhkInputBackendKind::PORTAL: return "portal";
	case AhkInputBackendKind::GNOME_SHELL: return "gnome-shell";
	case AhkInputBackendKind::EVDEV: return "evdev";
	default: return "auto";
	}
}

void LinuxInputBackendSync()
{
	switch (CurrentKind())
	{
	case AhkInputBackendKind::PORTAL:
		LinuxGShortcutSync();
		break;
	case AhkInputBackendKind::GNOME_SHELL:
		LinuxGnomeShellSync();
		break;
	case AhkInputBackendKind::EVDEV:
		SetError("AHK_INPUT_BACKEND=evdev: ahk-inputd is not installed yet");
		break;
	default:
		break; // X11: handled by the existing XGrabKey machinery.
	}
}

void LinuxInputBackendDispatch()
{
	switch (CurrentKind())
	{
	case AhkInputBackendKind::PORTAL:
		LinuxGShortcutDispatch();
		break;
	case AhkInputBackendKind::GNOME_SHELL:
		LinuxGnomeShellDispatch();
		break;
	default:
		break;
	}
}

void LinuxInputBackendShutdown()
{
	switch (CurrentKind())
	{
	case AhkInputBackendKind::PORTAL:
		LinuxGShortcutShutdown();
		break;
	case AhkInputBackendKind::GNOME_SHELL:
		LinuxGnomeShellShutdown();
		break;
	default:
		break;
	}
}

const wchar_t *LinuxInputBackendLastError()
{
	return sLastErrorBuf;
}

bool LinuxInputBackendActive()
{
	switch (CurrentKind())
	{
	case AhkInputBackendKind::PORTAL:
		return LinuxGShortcutActive();
	case AhkInputBackendKind::GNOME_SHELL:
		return LinuxGnomeShellActive();
	default:
		return false;
	}
}
