#pragma once

#ifdef CONFIG_DEBUGGER

// Store the initial DBGp endpoint and install the SIGUSR2 reconnect trigger.
// The signal handler only sets a sig_atomic_t flag; networking occurs later in
// the ordinary Linux main-loop context.
void LinuxDebuggerConfigure(const char *aHost, const char *aPort);

// Initial pre-auto-execute connect/break. Returns false on connection failure.
bool LinuxDebuggerInitialConnect();

// Cheap signal-flag check used by script execution hooks while detached.
bool LinuxDebuggerReconnectPending();

// Pump pending commands, clean up a dead IDE socket and service an explicit
// SIGUSR2 reconnect request. Safe to call every Linux main-loop iteration.
void LinuxDebuggerPump();

#endif
