// Unified input backend selection + routing (see input_backend.h).
//
// This file owns the *decision* of which backend handles global hotkeys.
// Backends stay independent:
//   - X11:        core_hotkey_linux.cpp (XGrabKey/XRecord) - untouched.
//   - Portal:     core_gshortcut_linux.cpp - frozen (no GNOME-49 hacks).
//   - GNOME Shell: input_backend_gnome_shell.cpp - thin D-Bus broker.
//   - evdev:      core_evdev_linux.cpp - native /dev/input capture lane
//                 (check0820 direction-B: hotkeys fire on any compositor;
//                 suppression via EVIOCGRAB + uinput replay).
//
// Selection policy (see header): explicit AHK_INPUT_BACKEND wins,
// AHK_FORCE_GLOBAL_SHORTCUTS=1 means "require the Portal backend" and wins
// over auto; auto picks X11 for X11 sessions and gnome-shell (when the
// extension is on the bus) or Portal for Wayland sessions.
#include "input_backend.h"
#include "core_gshortcut_linux.h"
#include "input_backend_gnome_shell.h"
#include "core_evdev_linux.h"
#include "core_clipboard_linux.h"
#include "core_tray_linux.h"
#include "../../script.h"
#include "../../globaldata.h"
#include "../../script_func_impl.h"
#include "../../hotkey.h"
#include "../../keyboard_mouse.h"
#include "../../application.h"
#include <dbus/dbus.h>
#include <cstring>
#include <cstdlib>
#include <cstdio>
#include <strings.h>
#include <string>

namespace
{

wchar_t sLastErrorBuf[512] = { 0 };

void SetError(const char *aText)
{
	mbstowcs(sLastErrorBuf, aText ? aText : "", _countof(sLastErrorBuf) - 1);
	sLastErrorBuf[_countof(sLastErrorBuf) - 1] = 0;
}

// --- R1-6 probes (check_detail0821 §1.2-C / A.8-2) ---------------------------
// A functional session-bus probe beats version sniffing: what we actually
// need to know is whether a GlobalShortcuts portal backend is live.  Per the
// portal spec each interface's version is exposed as a `version` property on
// /org/freedesktop/portal/desktop; xdg-desktop-portal-gnome >= 48 (GNOME 48+)
// ships that backend, GNOME 45-47 does not.

bool SessionBusGetStringProp(const char *aOwner, const char *aPath,
	const char *aIface, const char *aProp, char *aOut, size_t aOutSize)
{
	aOut[0] = 0;
	DBusError err; dbus_error_init(&err);
	DBusConnection *c = dbus_bus_get(DBUS_BUS_SESSION, &err);
	if (!c) { dbus_error_free(&err); return false; }
	dbus_error_free(&err);
	DBusMessage *msg = dbus_message_new_method_call(aOwner, aPath,
		"org.freedesktop.DBus.Properties", "Get");
	DBusMessageIter it;
	dbus_message_iter_init_append(msg, &it);
	dbus_message_iter_append_basic(&it, DBUS_TYPE_STRING, &aIface);
	dbus_message_iter_append_basic(&it, DBUS_TYPE_STRING, &aProp);
	DBusMessage *rep = dbus_connection_send_with_reply_and_block(c, msg, 3000, &err);
	dbus_message_unref(msg);
	bool ok = false;
	if (rep)
	{
		DBusMessageIter rit, var;
		dbus_message_iter_init(rep, &rit);
		if (dbus_message_iter_get_arg_type(&rit) == DBUS_TYPE_VARIANT)
		{
			dbus_message_iter_recurse(&rit, &var);
			if (dbus_message_iter_get_arg_type(&var) == DBUS_TYPE_STRING)
			{
				const char *v = nullptr;
				dbus_message_iter_get_basic(&var, &v);
				if (v) { snprintf(aOut, aOutSize, "%s", v); ok = true; }
			}
		}
		dbus_message_unref(rep);
	}
	dbus_error_free(&err);
	dbus_connection_unref(c);
	return ok;
}

int ParseLeadingInt(const char *aText)
{
	int v = 0;
	for (const char *p = aText; *p >= '0' && *p <= '9'; ++p)
		v = v * 10 + (*p - '0');
	return v;
}

} // namespace

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

// Parse a boolean-ish environment flag.  Only 1/true/yes/on (case-
// insensitive) count as true; 0/false/no/off are false; anything else is
// rejected with a visible warning (a typo must never silently flip a
// backend choice - check0819 P0-4).
bool EnvFlag(const char *aName, bool &aExisted)
{
	const char *v = getenv(aName);
	if (!v || !*v)
	{
		aExisted = false;
		return false;
	}
	aExisted = true;
	if (!strcasecmp(v, "1") || !strcasecmp(v, "true")
		|| !strcasecmp(v, "yes") || !strcasecmp(v, "on"))
		return true;
	if (!strcasecmp(v, "0") || !strcasecmp(v, "false")
		|| !strcasecmp(v, "no") || !strcasecmp(v, "off"))
		return false;
	fprintf(stderr,
		"AHK warning: %s=\"%s\" is not a recognized boolean "
		"(expected 1/true/yes/on or 0/false/no/off); treating it as false\n",
		aName, v);
	return false;
}

AhkInputBackendKind ResolveBackend()
{
	// Explicit override.  An unknown value is a loud startup warning + a
	// sticky error (visible through LinuxInputBackendLastError()), NOT a
	// silent fall-through: the user must never believe a forced backend is
	// active while another one runs (check0819 P0-4).
	if (const char *b = getenv("AHK_INPUT_BACKEND"))
	{
		if (!strcmp(b, "x11")) return AhkInputBackendKind::X11;
		if (!strcmp(b, "portal")) return AhkInputBackendKind::PORTAL;
		if (!strcmp(b, "gnome-shell")) return AhkInputBackendKind::GNOME_SHELL;
		if (!strcmp(b, "evdev")) return AhkInputBackendKind::EVDEV;
		if (strcmp(b, "auto") != 0)
		{
			fprintf(stderr,
				"AHK warning: AHK_INPUT_BACKEND=\"%s\" is not one of "
				"auto|x11|portal|gnome-shell|evdev; falling back to auto "
				"selection\n", b);
			SetError("AHK_INPUT_BACKEND: unknown value (auto|x11|portal|gnome-shell|evdev)");
		}
		// "auto" (and unknown values) fall through to auto selection.
	}
	// AHK_FORCE_GLOBAL_SHORTCUTS keeps its documented meaning: explicitly
	// require the standard GlobalShortcuts Portal backend.  It is NOT reused
	// for the GNOME Shell backend, so it wins over auto selection.  Only
	// 1/true/yes/on force it; 0/false/no/off and junk values do not
	// (previously ANY set value, including "0", forced Portal - check0819).
	bool existed = false;
	if (EnvFlag("AHK_FORCE_GLOBAL_SHORTCUTS", existed) && existed)
		return AhkInputBackendKind::PORTAL;
	if (!SessionIsWayland())
		return AhkInputBackendKind::X11; // X11 session (or unknown).
	// Wayland session: prefer the GNOME Shell extension when it is actually
	// available on the session bus (zero-confirm, bare keys).
	if (DesktopIsGNOME() && LinuxGnomeShellAvailable())
		return AhkInputBackendKind::GNOME_SHELL;
	// GNOME without the extension (check_detail0821 §1.2-C): the
	// GlobalShortcuts portal backend only exists on GNOME 48+ (it is shipped
	// by xdg-desktop-portal-gnome >= 48).  Selecting portal on GNOME 45-47
	// without the extension used to be a silent no-op -- make it a loud,
	// actionable error.  The functional probe below (is a GlobalShortcuts
	// backend actually live on the session bus?) is the real test; the GNOME
	// major version only improves the message.
	if (DesktopIsGNOME() && !LinuxPortalGlobalShortcutsAvailable())
	{
		int major = LinuxGnomeMajorVersion();
		char msg[512];
		if (major > 0 && major < 48)
			snprintf(msg, sizeof(msg),
				"GNOME %d without the AHK shell extension and without a "
				"GlobalShortcuts portal backend: no native global-hotkey path. "
				"Install the extension (see docs-v2/docs/linux-port.htm) or use "
				"AHK_INPUT_BACKEND=evdev; the portal path cannot register "
				"hotkeys on GNOME < 48.", major);
		else
			snprintf(msg, sizeof(msg),
				"GNOME without the AHK shell extension and without a "
				"GlobalShortcuts portal backend on the session bus: no native "
				"global-hotkey path. Install the extension "
				"(see docs-v2/docs/linux-port.htm) or use "
				"AHK_INPUT_BACKEND=evdev.");
		fprintf(stderr, "AHK warning: %s\n", msg);
		SetError(msg);
	}
	// KDE, GNOME 48+ without the extension, other compositors: portal baseline.
	return AhkInputBackendKind::PORTAL;
}

AhkInputBackendKind &CurrentKind()
{
	static AhkInputBackendKind sKind = ResolveBackend();
	return sKind;
}

struct InputCapsEntry
{
	AhkInputBackendKind kind;
	const char *name;
	AhkInputBackendCaps caps;
};

// Generated from one compact definition file so C++, script-visible caps,
// --diag and the documentation generator cannot maintain divergent tables.
#define AHK_INPUT_CAPS(kind_, name_, gh_, sup_, pass_, up_, wild_, bare_, zero_, dyn_, multi_, sc_, combo_, chars_, prov_, level_, unicode_) \
	{ AhkInputBackendKind::kind_, name_, { gh_, sup_, pass_, up_, wild_, bare_, zero_, dyn_, multi_, sc_, combo_, chars_, AhkSyntheticProvenance::prov_, level_, unicode_ } },
static const InputCapsEntry sInputCaps[] = {
#include "input_caps.def"
};
#undef AHK_INPUT_CAPS

const InputCapsEntry *KindCapsEntry(AhkInputBackendKind aKind)
{
	for (const auto &entry : sInputCaps)
		if (entry.kind == aKind)
			return &entry;
	// AUTO is resolved before normal use; preserve the legacy fallback.
	for (const auto &entry : sInputCaps)
		if (entry.kind == AhkInputBackendKind::EVDEV)
			return &entry;
	return nullptr;
}

const AhkInputBackendCaps *KindCaps(AhkInputBackendKind aKind)
{
	const InputCapsEntry *entry = KindCapsEntry(aKind);
	return entry ? &entry->caps : nullptr;
}

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

const AhkInputBackendCaps *LinuxInputBackendCapsFor(AhkInputBackendKind aKind)
{
	return KindCaps(aKind);
}

const char *LinuxInputBackendNameFor(AhkInputBackendKind aKind)
{
	if (aKind == AhkInputBackendKind::AUTO)
		return "auto";
	const InputCapsEntry *entry = KindCapsEntry(aKind);
	return entry ? entry->name : "unknown";
}

const char *LinuxInputBackendName()
{
	return LinuxInputBackendNameFor(CurrentKind());
}

unsigned LinuxInputBackendCapsVersion()
{
	return AHK_INPUT_CAPS_VERSION;
}

const char *LinuxInputBackendProvenanceName(AhkSyntheticProvenance aValue)
{
	switch (aValue)
	{
	case AhkSyntheticProvenance::HEURISTIC: return "heuristic";
	case AhkSyntheticProvenance::AUTHORITATIVE: return "authoritative";
	default: return "none";
	}
}

static bool CapsSatisfy(const AhkInputBackendCaps *c, bool aPassthrough, bool aKeyUp,
	bool aBare, bool aWildcard, bool aScanCode, bool aCustomCombo)
{
	if (!c || !c->global_hotkeys) return false;
	if (aPassthrough && !c->passthrough) return false;
	if (aKeyUp && !c->key_up) return false;
	if (aBare && !c->bare_keys) return false;
	if (aWildcard && !c->wildcard) return false;
	if (aScanCode && !c->scan_code) return false;
	if (aCustomCombo && !c->custom_combo) return false;
	return true;
}

// Per-hotkey backend routing (check_detail0821 §1-A / R3): pick the best
// backend whose caps satisfy the hotkey's needs.  The effective backend wins
// when it qualifies; otherwise walk the other lanes in priority order.
AhkInputBackendKind LinuxInputBackendRoute(bool aPassthrough, bool aKeyUp, bool aBare,
	bool aWildcard, bool aScanCode, bool aCustomCombo)
{
	const AhkInputBackendKind eff = CurrentKind();
	if (CapsSatisfy(KindCaps(eff), aPassthrough, aKeyUp, aBare, aWildcard,
		aScanCode, aCustomCombo))
		return eff;
	// Priority: prefer non-root, integration-light lanes first.
	static const AhkInputBackendKind kCandidates[] = {
		AhkInputBackendKind::X11,
		AhkInputBackendKind::GNOME_SHELL,
		AhkInputBackendKind::PORTAL,
		AhkInputBackendKind::EVDEV,
	};
	for (AhkInputBackendKind k : kCandidates)
	{
		if (k == eff) continue;
		if (k == AhkInputBackendKind::X11)
		{
			const char *display = getenv("DISPLAY");
			if (!display || !*display)
				continue;
		}
		if (CapsSatisfy(KindCaps(k), aPassthrough, aKeyUp, aBare, aWildcard,
			aScanCode, aCustomCombo))
			return k;
	}
	return eff; // keep current behavior when nothing qualifies
}


// Version of the GlobalShortcuts portal backend on the session bus (0 when
// absent/unreachable).  Functional probe used by auto selection and --diag.
unsigned LinuxPortalGlobalShortcutsVersion()
{
	DBusError err; dbus_error_init(&err);
	DBusConnection *c = dbus_bus_get(DBUS_BUS_SESSION, &err);
	if (!c) { dbus_error_free(&err); return 0; }
	dbus_error_free(&err);
	const char *owner = "org.freedesktop.portal.Desktop";
	const char *path = "/org/freedesktop/portal/desktop";
	const char *iface = "org.freedesktop.portal.GlobalShortcuts";
	unsigned version = 0;
	DBusMessage *msg = dbus_message_new_method_call(owner, path,
		"org.freedesktop.DBus.Properties", "Get");
	if (msg)
	{
		DBusMessageIter it;
		dbus_message_iter_init_append(msg, &it);
		dbus_message_iter_append_basic(&it, DBUS_TYPE_STRING, &iface);
		const char *prop = "version";
		dbus_message_iter_append_basic(&it, DBUS_TYPE_STRING, &prop);
		DBusMessage *rep = dbus_connection_send_with_reply_and_block(c, msg, 3000, &err);
		dbus_message_unref(msg);
		if (rep)
		{
			DBusMessageIter rit, var;
			dbus_message_iter_init(rep, &rit);
			if (dbus_message_iter_get_arg_type(&rit) == DBUS_TYPE_VARIANT)
			{
				dbus_message_iter_recurse(&rit, &var);
				if (dbus_message_iter_get_arg_type(&var) == DBUS_TYPE_UINT32)
				{
					dbus_uint32_t v = 0;
					dbus_message_iter_get_basic(&var, &v);
					version = (unsigned)v;
				}
			}
			dbus_message_unref(rep);
		}
		dbus_error_free(&err);
	}
	dbus_connection_unref(c);
	return version;
}

bool LinuxPortalGlobalShortcutsAvailable()
{
	return LinuxPortalGlobalShortcutsVersion() >= 1;
}

// Major GNOME Shell version (49 for "49.0"), 0 when unknown.
int LinuxGnomeMajorVersion()
{
	char ver[64] = { 0 };
	if (SessionBusGetStringProp("org.gnome.Shell", "/org/gnome/Shell",
			"org.gnome.Shell", "ShellVersion", ver, sizeof(ver)) && ver[0])
	{
		int major = ParseLeadingInt(ver);
		if (major > 0) return major;
	}
	// Fallback: the distro may ship a plain version file.
	FILE *f = fopen("/usr/share/gnome-shell/gnome-shell-version", "r");
	if (f)
	{
		char buf[64] = { 0 };
		if (fgets(buf, sizeof(buf), f))
		{
			int major = ParseLeadingInt(buf);
			if (major > 0) { fclose(f); return major; }
		}
		fclose(f);
	}
	return 0;
}

void LinuxInputBackendSync()
{
	// Fresh view: only failures in THIS sync call remain visible to the
	// caller (portal/gnome-shell clear their own buffers; x11/evdev do not
	// set one on success and the evdev branch re-sets on any failure below).
	sLastErrorBuf[0] = 0;
	switch (CurrentKind())
	{
	case AhkInputBackendKind::PORTAL:
		LinuxGShortcutSync();
		break;
	case AhkInputBackendKind::GNOME_SHELL:
		LinuxGnomeShellSync();
		break;
	case AhkInputBackendKind::EVDEV:
		// The evdev lane is a standing reader; nothing to sync.  If it
		// could not open any device (no permission), surface the reason.
		if (!LinuxEvdevActive())
			SetError(LinuxEvdevLastError());
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
	case AhkInputBackendKind::EVDEV:
		LinuxEvdevDispatch();
		break;
	default:
		break;
	}
	// §4: the GNOME-extension clipboard listener is display-independent (it
	// rides the session bus), so pump it on every main-loop pass -- the
	// XFixes path above handles X11 and this covers pure Wayland.
	LinuxClipboardDispatchWayland();
	// §5-M5: the StatusNotifierItem tray service rides the session bus too.
	LinuxTrayDispatch();
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
	case AhkInputBackendKind::EVDEV:
		LinuxEvdevShutdown();
		break;
	default:
		break;
	}
}

const wchar_t *LinuxInputBackendLastError()
{
	// Route to the active backend's own error state (its buffer is cleared
	// at the start of every Sync(), so a non-empty result means the most
	// recent sync failed).  The unified buffer only ever holds backend-
	// selection errors (unknown AHK_INPUT_BACKEND value) and the evdev
	// failure text, which LinuxInputBackendSync() refreshes on each call.
	switch (CurrentKind())
	{
	case AhkInputBackendKind::PORTAL:
		return LinuxGShortcutLastError();
	case AhkInputBackendKind::GNOME_SHELL:
		return LinuxGnomeShellLastError();
	default:
		return sLastErrorBuf;
	}
}

bool LinuxInputBackendActive()
{
	switch (CurrentKind())
	{
	case AhkInputBackendKind::PORTAL:
		return LinuxGShortcutActive();
	case AhkInputBackendKind::GNOME_SHELL:
		return LinuxGnomeShellActive();
	case AhkInputBackendKind::EVDEV:
		return LinuxEvdevActive();
	default:
		return false;
	}
}
