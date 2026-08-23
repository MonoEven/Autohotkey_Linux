// Unified input backend abstraction for the Linux port.
//
// Backend kinds and selection policy:
//
//   AHK_INPUT_BACKEND=auto          (default; see below)
//   AHK_INPUT_BACKEND=x11           force X11 XGrabKey/XRecord path
//   AHK_INPUT_BACKEND=portal        force XDG Global Shortcuts Portal
//   AHK_INPUT_BACKEND=gnome-shell   force GNOME Shell extension backend
//   AHK_INPUT_BACKEND=evdev         force the native evdev/uinput lane:
//                                   reads /dev/input/event* (physical keys)
//                                   and fires hotkeys directly; with
//                                   EVIOCGRAB (input group / root) matched
//                                   hotkeys are suppressed and unmatched
//                                   keys are replayed through /dev/uinput
//                                   (check0820 direction-B)
//
//   AHK_FORCE_GLOBAL_SHORTCUTS=1    keeps its documented meaning: "explicitly
//                                   require the standard GlobalShortcuts
//                                   Portal backend".  It is NOT reused for the
//                                   GNOME Shell backend (different privilege
//                                   level), so it wins over auto selection.
//                                   Only 1/true/yes/on (case-insensitive)
//                                   count as true; 0/false/no/off and any
//                                   other value do NOT force Portal.
//   Unknown AHK_INPUT_BACKEND values print a clear startup warning and leave
//   a sticky error (LinuxInputBackendLastError()) instead of silently
//   running a different backend than the user asked for.
//
// auto selection:
//   X11 session                       -> X11
//   Wayland session + GNOME Shell ext -> gnome-shell (zero-confirm bare keys)
//   GNOME w/o ext + portal GS backend -> portal (GNOME 48+ backend present)
//   GNOME w/o ext, no portal backend  -> portal + loud error with install
//                                        guidance (GNOME <48 / no backend;
//                                        check_detail0821 §1.2-C)
//   KDE / other Wayland compositors   -> portal (safe baseline)
//
// The X11 path is deeply integrated in core_hotkey_linux.cpp (XGrabKey /
// XRecord / reconcile) and is not re-routed through this interface: when the
// resolved kind is X11, LinuxInputBackendSync()/Dispatch() are no-ops and the
// X11 machinery keeps running as today.
//
// The Portal backend (core_gshortcut_linux.cpp) is frozen: no new
// GNOME-49-specific workarounds go into it.  The GNOME Shell backend
// (input_backend_gnome_shell.cpp) talks over the session bus to the "thin"
// extension shipped in extension/ (Register/Unregister/ClearOwner D-Bus
// methods, Activated/Deactivated signals, grab_accelerator + allowKeybinding,
// no AHK parsing/execution inside the shell).
#pragma once

#include "../../stdafx.h"
#include <string>

enum class AhkInputBackendKind
{
	AUTO,        // Not yet resolved (LinuxInputBackendKind() resolves it).
	X11,         // XGrabKey / XRecord (existing core_hotkey_linux.cpp).
	PORTAL,      // XDG Global Shortcuts Portal.
	GNOME_SHELL, // GNOME Shell extension broker.
	EVDEV,       // Native evdev/uinput lane (core_evdev_linux.cpp).
};

// What a backend can do.  The evdev lane (check0820 direction-B) can
// register global hotkeys on ANY compositor (it reads the kernel input
// stream); suppression needs EVIOCGRAB (input group / root) and unmatched
// keys are replayed through /dev/uinput.  Without grab permission it is a
// listen-only lane (hotkeys still fire; keys pass through to the app).
struct AhkInputBackendCaps
{
	bool global_hotkeys;   // Can register global hotkeys.
	bool suppress;         // Hotkey consumes the key (exclusive).
	bool passthrough;      // ~1:: observe without consuming.
	bool key_up;           // 1 up:: (accelerator-deactivated semantics).
	bool wildcard;         // *1:: modifier-wildcard.
	bool bare_keys;        // Keys without modifiers (1, a, F12, ...).
	bool zero_confirm;     // No per-binding confirmation dialog.
	bool dynamic;          // Runtime register/unregister.
	bool multi_owner;      // Multiple scripts can register concurrently.
};

// Resolve the effective backend from the environment (see file header).
AhkInputBackendKind LinuxInputBackendKind();

// Version of the GlobalShortcuts portal backend live on the session bus
// (0 = absent/unreachable).  Functional probe (check_detail0821 §1.2-C).
unsigned LinuxPortalGlobalShortcutsVersion();
bool LinuxPortalGlobalShortcutsAvailable();

// Major GNOME Shell version (49 for "49.0"), 0 when unknown.
int LinuxGnomeMajorVersion();

// Capabilities of the currently effective backend (kind + caps).
const AhkInputBackendCaps *LinuxInputBackendCaps();
const char *LinuxInputBackendName();

// Capabilities of a specific backend kind (for per-hotkey routing queries).
const AhkInputBackendCaps *LinuxInputBackendCapsFor(AhkInputBackendKind aKind);

// Per-hotkey backend routing (check_detail0821 §1-A / R3): pick the best
// backend whose capabilities satisfy the hotkey's needs (tilde passthrough,
// key-up, bare key, wildcard).  Starts from the effective backend; falls back
// through the other lanes in priority order when the effective one cannot
// satisfy the flags.
AhkInputBackendKind LinuxInputBackendRoute(bool aPassthrough, bool aKeyUp, bool aBare, bool aWildcard);

// Hotkey set changed (called from LinuxHotkeyStateChanged): let the active
// backend reconcile its registrations.  No-op for X11/EVDEV.
void LinuxInputBackendSync();

// Pump the active backend's async events from the main event loop.
void LinuxInputBackendDispatch();

// Release everything (script exit / reload).
void LinuxInputBackendShutdown();

// Human-readable failure reason (empty when the last operation succeeded).
const wchar_t *LinuxInputBackendLastError();

// True when the active backend currently has shortcuts bound.
bool LinuxInputBackendActive();

// --- shared helpers (used by the portal + gnome-shell backends) -------------
// Kept here so neither backend duplicates the hotkey fire logic; the X11
// machinery in core_hotkey_linux.cpp is not affected.

struct Hotkey;      // ../../hotkey.h
struct HotkeyVariant;

// Enabled/suspended + normal/hook type check.
bool AhkBackendHotkeyEnabled(Hotkey *aHk);

// Variant-level fireability (enabled, suspend-exempt, HotIf criterion).
bool AhkBackendVariantFireable(Hotkey *aHk, HotkeyVariant *aV);

// Fire the hotkey body in a new thread (same semantics as the portal path).
void AhkBackendFireHotkey(Hotkey *aHk);

// GTK-accelerator string for a hotkey ("1", "<Control>1", ...).  Returns an
// empty string when the key cannot be represented.
std::string AhkBackendComboForHotkey(Hotkey *aHk);
