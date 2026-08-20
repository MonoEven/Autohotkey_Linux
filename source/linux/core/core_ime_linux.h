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