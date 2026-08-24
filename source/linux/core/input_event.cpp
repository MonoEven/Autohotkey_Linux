#include "../../stdafx.h"
#include "input_event.h"
#include <cstdio>
#include <cstdlib>
#include <ctime>

uint64_t LinuxInputEventMonotonicUs()
{
	struct timespec ts;
	if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0)
		return 0;
	return (uint64_t)ts.tv_sec * 1000000ULL + (uint64_t)ts.tv_nsec / 1000ULL;
}

const char *LinuxInputSourceName(AhkInputSource aSource)
{
	switch (aSource)
	{
	case AhkInputSource::PHYSICAL: return "physical";
	case AhkInputSource::SELF_INJECT: return "self_inject";
	case AhkInputSource::OTHER_INJECT: return "other_inject";
	case AhkInputSource::IME_COMMIT: return "ime_commit";
	default: return "unknown";
	}
}

const char *LinuxInputOriginName(AhkInputOrigin aOrigin)
{
	switch (aOrigin)
	{
	case AhkInputOrigin::X11: return "x11";
	case AhkInputOrigin::PORTAL: return "portal";
	case AhkInputOrigin::GNOME_SHELL: return "gnome-shell";
	case AhkInputOrigin::EVDEV: return "evdev";
	case AhkInputOrigin::BROKER: return "broker";
	case AhkInputOrigin::IBUS: return "ibus";
	case AhkInputOrigin::FCITX5: return "fcitx5";
	default: return "unknown";
	}
}

bool LinuxInputEventTraceEnabled()
{
	const char *path = getenv("AHK_INPUT_EVENT_TRACE");
	return path && *path;
}

void LinuxInputEventTrace(const AhkInputEvent &aEvent)
{
	const char *path = getenv("AHK_INPUT_EVENT_TRACE");
	if (!path || !*path)
		return;
	FILE *f = fopen(path, "a");
	if (!f)
		return;
	fprintf(f,
		"{\"schema\":%u,\"timestamp_us\":%llu,\"evdev_code\":%u,"
		"\"vk\":%u,\"sc\":%u,\"text\":%u,\"release\":%s,"
		"\"repeat\":%s,\"source\":\"%s\",\"send_level\":%d,"
		"\"device_id\":%u,\"origin\":\"%s\"}\n",
		AHK_INPUT_EVENT_VERSION, (unsigned long long)aEvent.timestamp_us,
		aEvent.evdev_code, (unsigned)aEvent.vk, (unsigned)aEvent.sc,
		(unsigned)aEvent.text, aEvent.is_release ? "true" : "false",
		aEvent.is_repeat ? "true" : "false", LinuxInputSourceName(aEvent.source),
		(int)aEvent.send_level, aEvent.device_id, LinuxInputOriginName(aEvent.origin));
	fclose(f);
}
