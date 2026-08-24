// Native evdev keyboard-capture lane (check0820 direction-B).
//
// Reads EV_KEY events from /dev/input/event* (the kernel input stream seen
// by every compositor), matches them against the Hotkey table and fires the
// hook with the same semantics as the other input backends.  With
// EVIOCGRAB (suppress mode) matched hotkeys are exclusive and non-matched
// keys are replayed through /dev/uinput; without it the lane is a
// non-suppressing observer (hotkeys fire, keys still reach the app - the
// `~` pass-through semantics).
//
// Integration: the main loop pumps LinuxEvdevDispatch() from MsgSleep,
// using the same 10 ms slice as the Wayland/clipboard dispatches, so no
// extra thread or pipe is needed.

#include "../../stdafx.h"
#include "../../application.h" // InitNewThread/ResumeUnderlyingThread
#include "../../script.h"
#include "../../globaldata.h"
#include "../../script_func_impl.h"
#include "../../hotkey.h"
#include "core_evdev_linux.h"
#include "core_uinput_linux.h"
#include "core_wayland_linux.h" // LinuxWaylandKeycodeForVk (vk->evdev)
#include "core_keymodel_linux.h"
#include "input_backend.h" // AhkBackendHotkeyEnabled
#include "input_event.h"
#include <linux/input.h>
#include <sys/poll.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <dirent.h>
#include <cerrno>
#include <cstring>
#include <cstdio>
#include <cstdarg>
#include <string>
#include <vector>

namespace {

struct EvdevDevice
{
	int fd = -1;
	bool grabbed = false; // EVIOCGRAB succeeded (suppress mode for this device).
};

std::vector<EvdevDevice> sDevices;
bool sScanned = false;
bool sAnyGrabbed = false; // At least one device suppressed.
bool sPanicked = false;   // Panic sequence fired: grabs released, fail-open.
char sError[256] = { 0 };
DWORD sLastRescanMs = 0;  // Periodic device discovery (check0820 direction-B:
                          // uinput test devices appear after startup; the
                          // broker must pick them up without a restart).

// Panic escape key (check_detail0821 §1-B / R4, keyd-style): the sequence
// Backspace -> Escape -> Enter releases every EVIOCGRAB and drops the lane
// into fail-open, so a stuck grab can always be recovered physically.  The
// stage advances on each down event and resets on any other key or after a
// timeout.
static int sPanicStage = 0;
static DWORD sPanicStageMs = 0;
#define PANIC_BACKSPACE 14 // KEY_BACKSPACE
#define PANIC_ESCAPE    1  // KEY_ESC
#define PANIC_ENTER     28 // KEY_ENTER
#define PANIC_TIMEOUT_MS 1500

// Which vk are currently physically down (modifiers + keys), updated from
// the EV_KEY stream.  Used for modifier matching and key-up hotkeys.
unsigned char sDown[256 / 8] = { 0 }; // bitmask by vk (0x00-0xFF ok).
unsigned char sPrefixDown[KEY_CNT] = { 0 };
unsigned char sPrefixUsed[KEY_CNT] = { 0 };
unsigned char sPrefixPassthrough[KEY_CNT] = { 0 };
unsigned int sPrefixMods[KEY_CNT] = { 0 };
unsigned char sComboSuppressed[KEY_CNT] = { 0 };

void SetDown(unsigned int vk, bool down)
{
	if (vk < 256)
	{
		if (down) sDown[vk / 8] |= (unsigned char)(1u << (vk % 8));
		else      sDown[vk / 8] &= (unsigned char)~(1u << (vk % 8));
	}
}

bool IsDown(unsigned int vk) { return vk < 256 && (sDown[vk / 8] & (1u << (vk % 8))) != 0; }

// Current modifier state as the Windows MOD_* bits (hotkey.mModifiers).
unsigned int ActiveMods()
{
	unsigned int m = 0;
	if (IsDown(0x10) || IsDown(0xA0) || IsDown(0xA1)) m |= MOD_SHIFT;   // 0x4
	if (IsDown(0x11) || IsDown(0xA2) || IsDown(0xA3)) m |= MOD_CONTROL; // 0x2
	if (IsDown(0x12) || IsDown(0xA4) || IsDown(0xA5)) m |= MOD_ALT;     // 0x1
	if (IsDown(0x5B) || IsDown(0x5C)) m |= MOD_WIN;                     // 0x8
	return m;
}

// Are the LR sides consistent with what the matcher expects?  (Simple
// version: both sides of a modifier are reported through the generic vk
// range so <^a and >^a cannot be distinguished; the evdev lane documents
// this limitation and matches on consolidated masks.)
void SetModifierFromEvdev(unsigned int ev, bool down)
{
	switch (ev)
	{
	case KEY_LEFTSHIFT:   SetDown(0xA0, down); SetDown(0x10, down); break;
	case KEY_RIGHTSHIFT:  SetDown(0xA1, down); SetDown(0x10, down); break;
	case KEY_LEFTCTRL:    SetDown(0xA2, down); SetDown(0x11, down); break;
	case KEY_RIGHTCTRL:   SetDown(0xA3, down); SetDown(0x11, down); break;
	case KEY_LEFTALT:     SetDown(0xA4, down); SetDown(0x12, down); break;
	case KEY_RIGHTALT:    SetDown(0xA5, down); SetDown(0x12, down); break;
	case KEY_LEFTMETA:    SetDown(0x5B, down); break;
	case KEY_RIGHTMETA:   SetDown(0x5C, down); break;
	}
}

unsigned int EvdevForHotkeyKey(Hotkey *aHotkey, bool aPrefix)
{
	if (!aHotkey)
		return 0;
	vk_type vk = aPrefix ? aHotkey->mModifierVK : aHotkey->mVK;
	sc_type sc = aPrefix ? aHotkey->mModifierSC : aHotkey->mSC;
	if (sc)
		return LinuxEvdevCodeForScanCode(sc);
	return vk ? LinuxWaylandKeycodeForVk(vk) : 0;
}

bool EvdevVariantFor(Hotkey *aHotkey, HotkeyVariant *&aVariant)
{
	aVariant = nullptr;
	if (!aHotkey || !AhkBackendHotkeyEnabled(aHotkey))
		return false;
	HotkeyVariant *variant = aHotkey->FindVariant();
	if (!variant || !variant->mEnabled || !aHotkey->PerformIsAllowed(*variant))
		return false;
	aVariant = variant;
	return true;
}

void FireEvdevVariant(Hotkey *aHotkey, HotkeyVariant *aVariant)
{
	++g_nThreads;
	++g;
	InitNewThread(aVariant->mPriority, false, false);
	aHotkey->PerformInNewThreadMadeByCaller(*aVariant);
	ResumeUnderlyingThread();
}

void FireEvdevStandalonePrefix(unsigned int aCode, unsigned int aMods, int aPhase)
{
	for (int i = 0; i < Hotkey::sHotkeyCount; ++i)
	{
		Hotkey *hk = Hotkey::shk[i];
		if (!hk || hk->mModifierVK || hk->mModifierSC
			|| EvdevForHotkeyKey(hk, false) != aCode)
			continue;
		if ((aPhase == 0 && hk->mKeyUp) || (aPhase == 1 && !hk->mKeyUp))
			continue;
		unsigned int required = (unsigned int)hk->mModifiers;
		if ((aMods & required) != required
			|| (!hk->mAllowExtraModifiers && (aMods & ~required)))
			continue;
		HotkeyVariant *variant = nullptr;
		if (EvdevVariantFor(hk, variant))
			FireEvdevVariant(hk, variant);
	}
}

bool EvdevPrefixNativeByDefault(unsigned int aCode)
{
	switch (aCode)
	{
	case KEY_LEFTSHIFT: case KEY_RIGHTSHIFT:
	case KEY_LEFTCTRL: case KEY_RIGHTCTRL:
	case KEY_LEFTALT: case KEY_RIGHTALT:
	case KEY_LEFTMETA: case KEY_RIGHTMETA:
	case KEY_CAPSLOCK: case KEY_NUMLOCK: case KEY_SCROLLLOCK:
		return true;
	default:
		return false;
	}
}

bool EvdevPrefixProperties(unsigned int aCode, bool &aPassthrough)
{
	aPassthrough = EvdevPrefixNativeByDefault(aCode);
	bool found = false;
	for (int i = 0; i < Hotkey::sHotkeyCount; ++i)
	{
		Hotkey *hk = Hotkey::shk[i];
		if (!hk || (!hk->mModifierVK && !hk->mModifierSC)
			|| EvdevForHotkeyKey(hk, true) != aCode)
			continue;
		HotkeyVariant *variant = nullptr;
		if (!EvdevVariantFor(hk, variant))
			continue;
		found = true;
		if (hk->mNoSuppress & AT_LEAST_ONE_COMBO_HAS_TILDE)
			aPassthrough = true;
	}
	return found;
}

// evdev code -> Win32 virtual key (inverse of LinuxWaylandKeycodeForVk).
unsigned int VkForEvdev(unsigned int code)
{
	// letters: KEY_A..KEY_L = 30..38, KEY_Z=44, ... (non-contiguous).
	static const struct { unsigned int ev; unsigned int vk; } kPairs[] = {
		{ KEY_A, 'A' }, { KEY_B, 'B' }, { KEY_C, 'C' }, { KEY_D, 'D' },
		{ KEY_E, 'E' }, { KEY_F, 'F' }, { KEY_G, 'G' }, { KEY_H, 'H' },
		{ KEY_I, 'I' }, { KEY_J, 'J' }, { KEY_K, 'K' }, { KEY_L, 'L' },
		{ KEY_M, 'M' }, { KEY_N, 'N' }, { KEY_O, 'O' }, { KEY_P, 'P' },
		{ KEY_Q, 'Q' }, { KEY_R, 'R' }, { KEY_S, 'S' }, { KEY_T, 'T' },
		{ KEY_U, 'U' }, { KEY_V, 'V' }, { KEY_W, 'W' }, { KEY_X, 'X' },
		{ KEY_Y, 'Y' }, { KEY_Z, 'Z' },
		{ KEY_0, '0' }, { KEY_1, '1' }, { KEY_2, '2' }, { KEY_3, '3' },
		{ KEY_4, '4' }, { KEY_5, '5' }, { KEY_6, '6' }, { KEY_7, '7' },
		{ KEY_8, '8' }, { KEY_9, '9' },
		{ KEY_F1, 0x70 }, { KEY_F2, 0x71 }, { KEY_F3, 0x72 }, { KEY_F4, 0x73 },
		{ KEY_F5, 0x74 }, { KEY_F6, 0x75 }, { KEY_F7, 0x76 }, { KEY_F8, 0x77 },
		{ KEY_F9, 0x78 }, { KEY_F10, 0x79 }, { KEY_F11, 0x7A }, { KEY_F12, 0x7B },
		{ KEY_F13, 0x7C }, { KEY_F14, 0x7D }, { KEY_F15, 0x7E }, { KEY_F16, 0x7F },
		{ KEY_F17, 0x80 }, { KEY_F18, 0x81 }, { KEY_F19, 0x82 }, { KEY_F20, 0x83 },
		{ KEY_F21, 0x84 }, { KEY_F22, 0x85 }, { KEY_F23, 0x86 }, { KEY_F24, 0x87 },
		{ KEY_BACKSPACE, 0x08 }, { KEY_TAB, 0x09 }, { KEY_ENTER, 0x0D },
		{ KEY_ESC, 0x1B }, { KEY_SPACE, 0x20 },
		{ KEY_LEFTSHIFT, 0xA0 }, { KEY_RIGHTSHIFT, 0xA1 },
		{ KEY_LEFTCTRL, 0xA2 }, { KEY_RIGHTCTRL, 0xA3 },
		{ KEY_LEFTALT, 0xA4 }, { KEY_RIGHTALT, 0xA5 },
		{ KEY_LEFTMETA, 0x5B }, { KEY_RIGHTMETA, 0x5C },
		{ KEY_LEFT, 0x25 }, { KEY_UP, 0x26 }, { KEY_RIGHT, 0x27 }, { KEY_DOWN, 0x28 },
		{ KEY_PAGEUP, 0x21 }, { KEY_PAGEDOWN, 0x22 }, { KEY_HOME, 0x24 }, { KEY_END, 0x23 },
		{ KEY_INSERT, 0x2D }, { KEY_DELETE, 0x2E }, { KEY_GRAVE, 0xC0 },
		{ KEY_MINUS, 0xBD }, { KEY_EQUAL, 0xBB }, { KEY_LEFTBRACE, 0xDB },
		{ KEY_RIGHTBRACE, 0xDD }, { KEY_BACKSLASH, 0xDC }, { KEY_SEMICOLON, 0xBA },
		{ KEY_APOSTROPHE, 0xDE }, { KEY_COMMA, 0xBC }, { KEY_DOT, 0xBE },
		{ KEY_SLASH, 0xBF }, { KEY_KP0, 0x60 }, { KEY_KP1, 0x61 }, { KEY_KP2, 0x62 },
		{ KEY_KP3, 0x63 }, { KEY_KP4, 0x64 }, { KEY_KP5, 0x65 }, { KEY_KP6, 0x66 },
		{ KEY_KP7, 0x67 }, { KEY_KP8, 0x68 }, { KEY_KP9, 0x69 },
		{ KEY_KPASTERISK, 0x6A }, { KEY_KPPLUS, 0x6B }, { KEY_KPMINUS, 0x6D },
		{ KEY_KPDOT, 0x6E }, { KEY_KPSLASH, 0x6F },
		{ KEY_NUMLOCK, 0x90 }, { KEY_CAPSLOCK, 0x14 }, { KEY_SCROLLLOCK, 0x91 },
	};
	for (const auto &p : kPairs)
		if (p.ev == code)
			return p.vk;
	return 0;
}

// Should this device be ignored?  Our own uinput injection devices have
// "AHK" in the name and must never be captured (they generate the output
// of the replay path).
bool DeviceNameSkipped(const char *name)
{
	if (!name || !*name)
		return true; // No name: not a keyboard.
	if (strstr(name, "AHK"))
		return true;
	if (strstr(name, "ahk"))
		return true;
	return false;
}

bool OpenDevice(const char *path)
{
	// name via sysfs
	char name[128] = { 0 };
	char namepath[512];
	snprintf(namepath, sizeof(namepath), "/sys/class/input/%s/device/name",
		strrchr(path, '/') ? strrchr(path, '/') + 1 : path);
	FILE *f = fopen(namepath, "r");
	if (f)
	{
		size_t n = fread(name, 1, sizeof(name) - 1, f);
		name[n] = 0;
		fclose(f);
		// trim
		for (char *p = name; *p; ++p)
			if (*p == '\n' || *p == '\r') { *p = 0; break; }
	}
	if (DeviceNameSkipped(name))
		return false;

	int fd = open(path, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
	if (fd < 0)
	{
		snprintf(sError, sizeof(sError),
			"AHK_INPUT_BACKEND=evdev: cannot open %s (%s); "
			"reading /dev/input/event* requires the 'input' group or root",
			path, strerror(errno));
		return false;
	}
	// Only accept devices that can produce key events (try a grab; if it
	// fails with ENODEV the device is not a keyboard we can use, or simply
	// no read permission -> the grab error tells us which).
	int grab = 0;
	if (ioctl(fd, EVIOCGRAB, 1) == 0)
	{
		grab = 1; // Suppress mode for this device.
	}
	else
	{
		grab = 0; // Listen mode (no permission / grab unsupported).
	}
	EvdevDevice dev;
	dev.fd = fd;
	dev.grabbed = grab != 0;
	sDevices.push_back(dev);
	sAnyGrabbed = sAnyGrabbed || dev.grabbed;
	const char *t = getenv("AHK_EVDEV_TRACE");
	if (t && strcmp(t, "0") != 0)
		fprintf(stderr, "[evdev] opened %s grab=%d\n", path, dev.grabbed);
	return true;
}

bool ScanDevices()
{
	DIR *d = opendir("/dev/input");
	if (!d)
	{
		snprintf(sError, sizeof(sError), "AHK_INPUT_BACKEND=evdev: /dev/input not readable (%s)", strerror(errno));
		return false;
	}
	struct dirent *ent;
	while ((ent = readdir(d)) != nullptr)
	{
		if (strncmp(ent->d_name, "event", 5) != 0)
			continue;
		char path[256];
		snprintf(path, sizeof(path), "/dev/input/%s", ent->d_name);
		// Skip devices we already hold (compare the fd of the open file).
		bool have = false;
		for (auto &dev : sDevices)
		{
			char fdpath[64];
			snprintf(fdpath, sizeof(fdpath), "/proc/self/fd/%d", dev.fd);
			char target[512];
			ssize_t n = readlink(fdpath, target, sizeof(target) - 1);
			if (n > 0)
			{
				target[n] = 0;
				if (strcmp(target, path) == 0) { have = true; break; }
			}
		}
		if (have)
			continue;
		OpenDevice(path);
	}
	closedir(d);
	return !sDevices.empty();
}

// Check for newly-added input devices every ~2 s so uinput test keyboards
// (and hot-plugged real keyboards) are captured without a restart.
void RescanIfDue()
{
	DWORD now = GetTickCount();
	if (now - sLastRescanMs < 2000)
		return;
	sLastRescanMs = now;
	ScanDevices();
}

// Match and fire one hotkey; returns true when the event was CONSUMED
// (a hotkey with suppression fired, or a key-up variant consumed the
// release).  In listen mode nothing is consumed.
static bool sEvT = false;
static bool EvTraceActive() { return sEvT; }
static void EvTraceStart() { if (!sEvT) { const char *v = getenv("AHK_EVDEV_TRACE"); sEvT = v && strcmp(v, "0") != 0; } }
static void EvTrace(const char *fmt, ...)
{
	if (!sEvT) return;
	va_list ap; va_start(ap, fmt); vfprintf(stderr, fmt, ap); va_end(ap);
	fprintf(stderr, "\n"); fflush(stderr);
}

// -1 = not a combo event, 0 = handled/pass through, 1 = handled/suppress.
int HandleEvdevCombo(unsigned int aCode, bool aDown, bool aRepeat, unsigned int aMods)
{
	if (aCode >= KEY_CNT)
		return -1;
	if (!aDown && sPrefixDown[aCode])
	{
		bool passthrough = sPrefixPassthrough[aCode] != 0;
		if (passthrough)
			FireEvdevStandalonePrefix(aCode, sPrefixMods[aCode], 1); // key-up only
		else if (!sPrefixUsed[aCode])
			FireEvdevStandalonePrefix(aCode, sPrefixMods[aCode], -1); // delayed down + up
		sPrefixDown[aCode] = 0;
		sPrefixUsed[aCode] = 0;
		return passthrough ? 0 : 1;
	}
	bool prefix_passthrough = false;
	bool is_prefix = EvdevPrefixProperties(aCode, prefix_passthrough);
	if (is_prefix)
	{
		if (aDown && !aRepeat)
		{
			sPrefixDown[aCode] = 1;
			sPrefixUsed[aCode] = 0;
			sPrefixPassthrough[aCode] = prefix_passthrough ? 1 : 0;
			sPrefixMods[aCode] = aMods;
			if (prefix_passthrough)
				FireEvdevStandalonePrefix(aCode, aMods, 0); // down immediately
		}
		else if (!aDown)
			sPrefixDown[aCode] = 0;
		// A custom prefix loses its native action unless prefixed with tilde.
		return sPrefixPassthrough[aCode] ? 0 : 1;
	}

	bool release_suppressed = !aDown && sComboSuppressed[aCode];
	if (release_suppressed)
		sComboSuppressed[aCode] = 0;

	for (int i = 0; i < Hotkey::sHotkeyCount; ++i)
	{
		Hotkey *hk = Hotkey::shk[i];
		if (!hk || (!hk->mModifierVK && !hk->mModifierSC)
			|| EvdevForHotkeyKey(hk, false) != aCode)
			continue;
		unsigned int prefix = EvdevForHotkeyKey(hk, true);
		if (!prefix || prefix >= KEY_CNT || !sPrefixDown[prefix])
			continue;
		// Custom combos are wildcard-like: additional modifiers are accepted,
		// but any explicit neutral modifiers still have to be present.
		unsigned int required = (unsigned int)hk->mModifiers;
		if ((aMods & required) != required)
			continue;
		HotkeyVariant *variant = nullptr;
		if (!EvdevVariantFor(hk, variant))
			continue;
		bool passthrough = (variant->mNoSuppress & (NO_SUPPRESS_PREFIX
			| AT_LEAST_ONE_VARIANT_HAS_TILDE)) != 0;
		// A key-up combo owns the suffix press but defers its callback until
		// release, preserving a balanced suppression pair.
		if (aDown && hk->mKeyUp)
		{
			sPrefixUsed[prefix] = 1;
			if (!passthrough)
				sComboSuppressed[aCode] = 1;
			return passthrough ? 0 : 1;
		}
		if (hk->mKeyUp != !aDown)
			continue;
		sPrefixUsed[prefix] = 1;
		if (aDown && !passthrough)
			sComboSuppressed[aCode] = 1;
		FireEvdevVariant(hk, variant);
		return passthrough ? 0 : 1;
	}

	if (aDown && !aRepeat)
		for (unsigned int code = 0; code < KEY_CNT; ++code)
			if (sPrefixDown[code] && code != aCode)
				sPrefixUsed[code] = 1;
	return release_suppressed ? 1 : -1;
}

bool HandleEvdevKey(unsigned int evcode, bool down, bool isRepeat)
{
	EvTraceStart();
	unsigned int vk = VkForEvdev(evcode);
	if (vk > 0xFF)
		vk = 0;
	// Track modifier state first so the matcher sees the updated state. A key
	// without a VK may still match an explicit physical scXXX hotkey.
	SetModifierFromEvdev(evcode, down);
	if (vk)
		SetDown(vk, down);
	unsigned int mods = ActiveMods();
	EvTrace("ev %u vk=%u down=%d mods=%u", evcode, vk, (int)down, mods);
	int combo_result = HandleEvdevCombo(evcode, down, isRepeat, mods);
	if (combo_result >= 0)
		return combo_result != 0;
	if (vk == 0x10 || vk == 0x11 || vk == 0x12 || vk == 0x5B || vk == 0x5C
		|| (vk >= 0xA0 && vk <= 0xA5))
		return false;
	Hotkey *hk_fire = nullptr;
	HotkeyVariant *vp_fire = nullptr;

	for (int i = 0; i < Hotkey::sHotkeyCount; ++i)
	{
		Hotkey *hk = Hotkey::shk[i];
		if (!hk || hk->mModifierVK || hk->mModifierSC || hk->IsCompletelyDisabled())
			continue;
		if (hk->mKeyUp == down) // up-variants need a release; down need press.
			continue;
		bool key_match = hk->mVK
			? (unsigned int)hk->mVK == vk
			: hk->mSC && LinuxEvdevCodeForScanCode(hk->mSC) == evcode;
		if (!key_match)
			continue;
		// Modifier match: the required neutral bits must be held; extra primary
	// modifiers disqualify unless the hotkey is a wildcard (*).
	unsigned int req = (unsigned int)hk->mModifiers;
	if ((mods & req) != req)
		continue;
	if (!hk->mAllowExtraModifiers && (mods & ~req))
		continue;
		if (!AhkBackendHotkeyEnabled(hk))
			continue;
		HotkeyVariant *vp = hk->FindVariant();
		if (!vp || !vp->mEnabled)
			continue;
		if (!hk->PerformIsAllowed(*vp))
			continue;
		// Unique resolution: first match wins (registration order).
		hk_fire = hk;
		vp_fire = vp;
		break;
	}

	if (!hk_fire || !vp_fire) { EvTrace("no-match vk=%u", vk); return false; }


	// Fire in a new quasi-thread (same pattern as the other backends).
	++g_nThreads;
	++g;
	EvTrace("FIRE vk=%u mods=%u", vk, mods);
	InitNewThread(vp_fire->mPriority, false, false);
	hk_fire->PerformInNewThreadMadeByCaller(*vp_fire);
	ResumeUnderlyingThread();

	// Suppress mode: a matching hotkey consumes the key; listen mode
	// always passes through (stream still reaches the app).
	return sAnyGrabbed;
}

} // namespace

bool LinuxEvdevActive()
{
	if (!sScanned)
	{
		sScanned = true;
		ScanDevices();
		fprintf(stderr, "[evdev] %zu device(s) opened, %s%s\n",
			sDevices.size(), sAnyGrabbed ? "suppress" : "listen-only",
			EvTraceActive() ? " (trace-on)" : " (trace-off)");
		if (sDevices.empty())
			fprintf(stderr, "[evdev] %s\n", sError[0] ? sError : "no keyboard devices");
		fflush(stderr);
	}
	return !sDevices.empty();
}

bool LinuxEvdevCanSuppress()
{
	LinuxEvdevActive();
	return sAnyGrabbed;
}

// Panic escape key state machine (defined below; used by LinuxEvdevDispatch).
bool EvdevPanicStep(unsigned int aCode, bool aDown);

void LinuxEvdevDispatch()
{
	if (!LinuxEvdevActive() || sDevices.empty())
		return;
	RescanIfDue(); // Pick up uinput/hot-plugged devices (check0820).
	struct pollfd pfds[64];
	size_t n = sDevices.size();
	if (n > 64) n = 64;
	for (size_t i = 0; i < n; ++i)
	{
		pfds[i].fd = sDevices[i].fd;
		pfds[i].events = POLLIN;
		pfds[i].revents = 0;
	}
	int pr = poll(pfds, (nfds_t)n, 0);
	if (sEvT)
		fprintf(stderr, "[evdev] poll n=%zu pr=%d\n", n, pr);
	if (pr <= 0)
		return;

	struct input_event ev;
	for (size_t i = 0; i < n; ++i)
	{
		if (sEvT && pfds[i].revents)
			fprintf(stderr, "[evdev] fd%d revents=0x%x\n", pfds[i].fd, pfds[i].revents);
		if (!(pfds[i].revents & POLLIN))
			continue;
		for (;;)
		{
			ssize_t rd = read(sDevices[i].fd, &ev, sizeof(ev));
			if (rd != (ssize_t)sizeof(ev))
				break; // EAGAIN or short read: next poll.
			if (ev.type != EV_KEY)
				continue;
			unsigned int event_vk = VkForEvdev(ev.code);
			AhkInputEvent normalized = {
				LinuxInputEventMonotonicUs(), (uint32_t)ev.code,
				(vk_type)(event_vk <= 0xff ? event_vk : 0),
				LinuxScanCodeForEvdev((uint32_t)ev.code), 0,
				ev.value == 0, ev.value == 2, AhkInputSource::PHYSICAL, -1,
				(uint32_t)(i + 1), AhkInputOrigin::EVDEV
			};
			LinuxInputEventTrace(normalized);
			// Panic escape key: Backspace->Escape->Enter releases the grabs
			// (check_detail0821 §1-B / R4).  Checked before hotkey dispatch so
			// the sequence always wins.
			EvdevPanicStep((unsigned int)ev.code, ev.value != 0);
			if (sPanicked)
				continue; // Fail-open: everything passes through.
			bool suppressed = HandleEvdevKey(ev.code, ev.value != 0, ev.value == 2);
			// Suppression cover: when this device is grabbed and the key
			// was not consumed, replay it through /dev/uinput so the
			// compositor still receives the key.
			if (sDevices[i].grabbed && !suppressed)
			{
				unsigned int vk = VkForEvdev(ev.code);
				if (vk && vk <= 0xFF)
					LinuxUinputKeyEvent(vk, ev.value != 0);
			}
		}
	}
}

void LinuxEvdevShutdown()
{
	for (auto &dev : sDevices)
	{
		if (dev.grabbed)
			ioctl(dev.fd, EVIOCGRAB, 0); // Release the grab (fail-open).
		close(dev.fd);
	}
	sDevices.clear();
	sAnyGrabbed = false;
	sScanned = false;
	memset(sDown, 0, sizeof(sDown));
	memset(sPrefixDown, 0, sizeof(sPrefixDown));
	memset(sPrefixUsed, 0, sizeof(sPrefixUsed));
	memset(sPrefixPassthrough, 0, sizeof(sPrefixPassthrough));
	memset(sPrefixMods, 0, sizeof(sPrefixMods));
	memset(sComboSuppressed, 0, sizeof(sComboSuppressed));
}

// Panic escape key state machine (keyd-style Backspace->Escape->Enter).
// Returns true when the sequence just fired (grabs released, fail-open).
bool EvdevPanicStep(unsigned int aCode, bool aDown)
{
	DWORD now = GetTickCount();
	if (!aDown)
		return false; // Only advance on presses.
	if (sPanicStage > 0 && now - sPanicStageMs > PANIC_TIMEOUT_MS)
		sPanicStage = 0; // Stale sequence: reset.
	switch (sPanicStage)
	{
	case 0:
		if (aCode == PANIC_BACKSPACE)
		{
			sPanicStage = 1;
			sPanicStageMs = now;
		}
		break;
	case 1:
		if (aCode == PANIC_ESCAPE)
		{
			sPanicStage = 2;
			sPanicStageMs = now;
		}
		else if (aCode == PANIC_BACKSPACE)
		{
			sPanicStage = 1; // Repeated backspace keeps the stage.
			sPanicStageMs = now;
		}
		else
			sPanicStage = 0;
		break;
	case 2:
		sPanicStage = 0;
		if (aCode == PANIC_ENTER)
		{
			fprintf(stderr, "AHK evdev: PANIC sequence fired -- releasing all EVIOCGRAB (fail-open).\n");
			for (auto &dev : sDevices)
			{
				if (dev.grabbed)
					ioctl(dev.fd, EVIOCGRAB, 0);
				dev.grabbed = false;
			}
			sAnyGrabbed = false;
			sPanicked = true;
			return true;
		}
		break;
	}
	return false;
}

const char *LinuxEvdevLastError()
{
	return sError;
}