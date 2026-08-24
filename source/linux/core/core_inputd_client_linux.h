// core_inputd_client_linux.h
#pragma once

// Client side of the ahk-inputd broker protocol (check_detail0824 §1-C / M4-C).
// When a broker socket is reachable the evdev lane becomes a passive observer:
// it subscribes the EVDEV-assigned hotkey codes (with want_suppress flags) and
// consumes EVENT frames; the broker owns EVIOCGRAB, replay and arbitration, so
// several scripts can share the stream without BadAccess.  Without a broker
// the lane falls back to its in-process device scan.

typedef void (*LinuxInputdEventFn)(unsigned int aCode, int aValue,
	long long aTsUs, void *aUser);

// Try to connect + HELLO a running broker.  Returns true in broker mode.
bool LinuxInputdClientConnect();
bool LinuxInputdClientActive();

// Recompute and push the subscription rule set from the Hotkey table
// (EVDEV-assigned hotkeys only).  No-op when not connected.
void LinuxInputdClientUpdateRules();

// Poll and dispatch pending frames; each EVENT invokes aFn.  Handles broker
// disconnect by falling back to inactive (caller re-enters device mode).
void LinuxInputdClientDispatch(LinuxInputdEventFn aFn, void *aUser);

void LinuxInputdClientShutdown();
