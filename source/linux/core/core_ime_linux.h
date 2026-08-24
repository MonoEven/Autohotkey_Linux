// Linux input-method (IME) state query (see core_ime_linux.cpp).
#pragma once

enum
{
	LINUX_IME_NONE = 0,
	LINUX_IME_IBUS = 1,
	LINUX_IME_FCITX5 = 2,
};

// Current XKB group index on the X display (-1 when no working X display).
int LinuxImeXkbGroup();

// Detected active IME framework: LINUX_IME_NONE / IBUS / FCITX5.
int LinuxImeFramework();

// Start/pump the framework signal listener when Hotstrings/InputHook need the
// committed character stream. IBus uses its private bus with eavesdrop=true;
// Fcitx5 uses a private connection to the session bus.
bool LinuxImeStartListener();
void LinuxImeDispatch();
void LinuxImeShutdown();

// Rich status fields used by ImeStatus(). Returned engine pointer remains valid.
const char *LinuxImeEngine();
bool LinuxImePreeditActive();
bool LinuxImeListening();
bool LinuxImeCommitCaptureActive();
const char *LinuxImeListenerScope();
unsigned long LinuxImeCommitCount();
unsigned long LinuxImePreeditCount();
const char *LinuxImeLastCommit();