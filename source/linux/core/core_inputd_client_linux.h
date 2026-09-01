// core_inputd_client_linux.h
#pragma once

// Client side of the ahk-inputd broker protocol (check_detail0824 §1-C / M4-C).
// When a broker socket is reachable the evdev lane becomes a passive observer:
// it subscribes the EVDEV-assigned hotkey codes (with want_suppress flags) and
// consumes EVENT frames; the broker owns EVIOCGRAB, replay and arbitration, so
// several scripts can share the stream without BadAccess.  Without a broker
// the lane falls back to its in-process device scan.
//
// Protocol v2 (check0901 P0-3): the client negotiates with the "AHK2" magic,
// receives a broker-assigned client_id bound to a per-process script nonce,
// and gets an authoritative provenance envelope with every event
// (authority+generation+event_seq, source/confidence, send_level).  A v1-only
// broker falls back to the legacy 14-byte EVENT frames transparently.
//
// Broker-lane physical and broker-injected transactions share the v2
// envelope; synthetic events carry send_level >= 0 and are consumer-gated.
// M2 health snapshots expose local connection generation, authoritative
// broker generation/sequence, coverage and registration/held reconciliation.

// One dispatched broker event.  v2 fills provenance authoritatively; v1 fills
// only code/value/tsUs (provenance fields stay unknown).
struct LinuxInputdEvent
{
	unsigned int code;
	int value;         // 0/1/2 (evdev raw value)
	long long tsUs;
	int sendLevel;     // v2 envelope send_level; -1 = not synthetic
	unsigned char source;     // INPUTD_V2_SOURCE_*
	unsigned char confidence; // INPUTD_V2_CONF_*
	unsigned int deviceId;    // broker-assigned device id (v2); 0 on v1
	unsigned long long authorityGeneration;
	unsigned long long eventSeq;
	unsigned long long producerClientId; // 0 for physical events (M4)
	unsigned long long transactionId;    // 0 for physical events (M4)
	unsigned long long parentTransactionId;
	bool v2;
};

typedef void (*LinuxInputdEventFn)(const LinuxInputdEvent &aEvent, void *aUser);

// Try to connect + HELLO a running broker (v2 first, v1 fallback).
// Returns true in broker mode.
bool LinuxInputdClientConnect();
bool LinuxInputdClientActive();
uint64_t LinuxInputdClientConnectionGeneration();
uint64_t LinuxInputdClientAuthorityGeneration();
uint64_t LinuxInputdClientBrokerHealthSeq();
unsigned LinuxInputdClientCapsGranted();

// Recompute and push the subscription rule set from the Hotkey table
// (EVDEV-assigned hotkeys only).  No-op when not connected.
void LinuxInputdClientUpdateRules();

// Poll and dispatch pending frames; each EVENT invokes aFn.  Handles broker
// disconnect by falling back to inactive (caller re-enters device mode).
void LinuxInputdClientDispatch(LinuxInputdEventFn aFn, void *aUser);

void LinuxInputdClientShutdown();
