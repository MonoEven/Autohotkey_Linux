#pragma once

#include "../../stdafx.h"
#include "../../keyboard_mouse.h"
#include <stdint.h>

constexpr unsigned AHK_INPUT_EVENT_VERSION = 1;

enum class AhkInputSource : uint8_t
{
	PHYSICAL = 0,
	SELF_INJECT = 1,
	OTHER_INJECT = 2,
	IME_COMMIT = 3,
};

enum class AhkInputOrigin : uint8_t
{
	X11 = 1,
	PORTAL = 2,
	GNOME_SHELL = 3,
	EVDEV = 4,
	BROKER = 5,
};

struct AhkInputEvent
{
	uint64_t timestamp_us;   // CLOCK_MONOTONIC at normalization.
	uint32_t evdev_code;     // Canonical physical KEY_* number, 0 if absent.
	vk_type vk;              // Logical Win32-compatible virtual key.
	sc_type sc;              // Canonical AHK/set-1 scan code.
	char32_t text;           // Unicode scalar produced by this event, or 0.
	bool is_release;
	bool is_repeat;
	AhkInputSource source;
	int16_t send_level;      // -1 physical/unknown; 0..100 synthetic level.
	uint32_t device_id;      // XI2 sourceid or evdev device identifier.
	AhkInputOrigin origin;
};

uint64_t LinuxInputEventMonotonicUs();
const char *LinuxInputSourceName(AhkInputSource aSource);
const char *LinuxInputOriginName(AhkInputOrigin aOrigin);

// Optional JSONL oracle. Enabled only when AHK_INPUT_EVENT_TRACE names a file.
bool LinuxInputEventTraceEnabled();
void LinuxInputEventTrace(const AhkInputEvent &aEvent);
