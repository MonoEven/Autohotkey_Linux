// Consented RemoteDesktop/libei sender backend (check_detail0901 M6a).
#include "../../stdafx.h"
#include "input_backend_libei.h"
#include "input_backend.h"
#include "input_event.h"
#include "core_wayland_linux.h"
#include "core_dbus_call_linux.h"
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <strings.h>

#ifdef HAVE_LIBEI
#include <libei.h>
#include <liboeffis.h>
#include <dbus/dbus.h>
#include <linux/input-event-codes.h>
#include <poll.h>
#include <sys/mman.h>
#include <unistd.h>
#include <xkbcommon/xkbcommon.h>
#include <xkbcommon/xkbcommon-keysyms.h>
#include <array>
#include <string>
#endif

#ifndef AHK_LIBEI_VERSION
#define AHK_LIBEI_VERSION "not-built"
#endif
#ifndef AHK_LIBOEFFIS_VERSION
#define AHK_LIBOEFFIS_VERSION "not-built"
#endif
#ifndef AHK_LIBPORTAL_VERSION
#define AHK_LIBPORTAL_VERSION "not-built"
#endif

namespace {

char sReason[256] =
#ifdef HAVE_LIBEI
	"not probed";
#else
	"libei/liboeffis support not built";
#endif
LinuxLibeiStatus sStatus = {
#ifdef HAVE_LIBEI
	true,
#else
	false,
#endif
	AHK_LIBEI_VERSION, AHK_LIBOEFFIS_VERSION, AHK_LIBPORTAL_VERSION,
	0,
#ifdef HAVE_LIBEI
	LinuxLibeiState::IDLE,
#else
	LinuxLibeiState::NOT_BUILT,
#endif
	0, 0, 0, 0, sReason, false, false,
	false, false, false, false, false, false, 0, 0, 0,
	LinuxLibeiOutcome::NONE
};
int sCurrentSendLevel = 0;
char sCurrentTransport[24] = "event";

void SetReason(const char *reason)
{
	snprintf(sReason, sizeof(sReason), "%s", reason ? reason : "");
	sStatus.reason = sReason;
}

void Trace(const char *stage, uint64_t transaction, uint32_t sequence,
	const char *event, LinuxLibeiOutcome outcome,
	int metadata_send_level = -32768, const char *metadata_transport = nullptr)
{
	const char *path = getenv("AHK_LIBEI_TRACE");
	if (!path || !*path) return;
	FILE *f = fopen(path, "a");
	if (!f) return;
	fprintf(f,
		"{\"schema\":1,\"stage\":\"%s\",\"backend\":\"libei\","
		"\"generation\":%llu,\"transaction_id\":%llu,"
		"\"emulation_sequence\":%u,\"send_level\":%d,"
		"\"transport\":\"%s\",\"metadata_scope\":\"session-sideband\","
		"\"event\":\"%s\",\"outcome\":\"%s\","
		"\"target_delivered\":\"unknown\","
		"\"target_consumed\":\"unknown\"}\n",
		stage ? stage : "unknown", (unsigned long long)sStatus.generation,
		(unsigned long long)transaction, sequence,
		transaction ? (metadata_send_level != -32768
			? metadata_send_level : sCurrentSendLevel) : -1,
		transaction ? (metadata_transport ? metadata_transport : sCurrentTransport)
			: "none", event ? event : "none",
		LinuxLibeiOutcomeName(outcome));
	fclose(f);
}

#ifdef HAVE_LIBEI

struct DeviceEntry
{
	ei_device *device;
	bool resumed;
	bool keyboard;
	bool pointer;
	bool button;
	bool scroll;
	bool text;
	uint32_t sequence;
	xkb_context *xkb_context;
	xkb_keymap *keymap;
	xkb_mod_mask_t depressed;
	xkb_mod_mask_t latched;
	xkb_mod_mask_t locked;
	xkb_layout_index_t group;
	unsigned int shift_evdev;
	unsigned int altgr_evdev;
	std::array<unsigned char, KEY_CNT> held_keys;
	std::array<unsigned char, KEY_CNT> held_buttons;
};

#ifdef AHK_LIBEI_HAS_PING
struct PendingPing
{
	uint64_t id, transaction;
	uint32_t sequence;
	int send_level;
	char transport[24];
};
#endif

ei *sEi = nullptr;
oeffis *sPortal = nullptr;
std::array<DeviceEntry, 16> sDevices = {};
#ifdef AHK_LIBEI_HAS_PING
std::array<PendingPing, 32> sPings = {};
unsigned sPingHead = 0;
#endif
uint64_t sNextTransaction = 0;
uint64_t sCurrentTransaction = 0;
bool sCurrentSubmitted = false;
bool sCurrentFailed = false;
char sCurrentError[160] = "";
uint32_t sCurrentSequence = 0;
uint32_t sNextSequence = 0;
std::array<unsigned char, KEY_CNT> sNeutralizedKeyUps = {};
std::array<unsigned char, KEY_CNT> sNeutralizedButtonUps = {};
bool sPortalStarted = false;
bool sTerminal = false;
bool sEiApiError = false;
char sEiApiErrorText[256] = "";

void MarkFailed(const char *reason);

void EiLogHandler(struct ei *, enum ei_log_priority priority,
	const char *message, struct ei_log_context *)
{
	if (priority >= EI_LOG_PRIORITY_ERROR
		|| (message && (strstr(message, "Broken pipe")
			|| strstr(message, "failed to send")
			|| strstr(message, "socket disconnected")
			|| strstr(message, "not emulating"))))
	{
		sEiApiError = true;
		snprintf(sEiApiErrorText, sizeof(sEiApiErrorText), "%s",
			message ? message : "libei transport error");
	}
	if (getenv("AHK_LIBEI_LOG") || priority >= EI_LOG_PRIORITY_ERROR)
		fprintf(stderr, "[libei] %s\n", message ? message : "");
}

bool EnvFalse(const char *value)
{
	return value && (!strcasecmp(value, "0") || !strcasecmp(value, "false")
		|| !strcasecmp(value, "off") || !strcasecmp(value, "no"));
}

bool EnvTrue(const char *value)
{
	return value && (!strcasecmp(value, "1") || !strcasecmp(value, "true")
		|| !strcasecmp(value, "on") || !strcasecmp(value, "yes")
		|| !strcasecmp(value, "required") || !strcasecmp(value, "force"));
}

bool CaseContains(const char *text, const char *needle)
{
	if (!text || !needle || !*needle) return false;
	size_t n = strlen(needle);
	for (const char *p = text; *p; ++p)
		if (!strncasecmp(p, needle, n)) return true;
	return false;
}

unsigned PortalVersion()
{
	DBusError error; dbus_error_init(&error);
	DBusConnection *bus = dbus_bus_get(DBUS_BUS_SESSION, &error);
	if (!bus) { dbus_error_free(&error); return 0; }
	dbus_connection_set_exit_on_disconnect(bus, FALSE);
	DBusMessage *msg = dbus_message_new_method_call(
		"org.freedesktop.portal.Desktop", "/org/freedesktop/portal/desktop",
		"org.freedesktop.DBus.Properties", "Get");
	if (!msg) { dbus_connection_unref(bus); return 0; }
	const char *iface = "org.freedesktop.portal.RemoteDesktop";
	const char *prop = "version";
	dbus_message_append_args(msg, DBUS_TYPE_STRING, &iface,
		DBUS_TYPE_STRING, &prop, DBUS_TYPE_INVALID);
	DBusMessage *reply = LinuxDbusPendingReply(bus, msg,
		1500, &error);
	dbus_message_unref(msg);
	unsigned version = 0;
	if (reply)
	{
		DBusMessageIter iter, variant;
		if (dbus_message_iter_init(reply, &iter)
			&& dbus_message_iter_get_arg_type(&iter) == DBUS_TYPE_VARIANT)
		{
			dbus_message_iter_recurse(&iter, &variant);
			if (dbus_message_iter_get_arg_type(&variant) == DBUS_TYPE_UINT32)
				dbus_message_iter_get_basic(&variant, &version);
		}
		dbus_message_unref(reply);
	}
	dbus_error_free(&error);
	dbus_connection_unref(bus);
	return version;
}

void Report(LinuxLibeiState state, const char *reason, int error = 0)
{
	sStatus.state = state;
	sStatus.last_errno = error;
	++sStatus.health_seq;
	SetReason(reason);
	if (state == LinuxLibeiState::READY)
		sStatus.last_success_us = LinuxInputEventMonotonicUs();
	AhkPermissionState permission = AhkPermissionState::UNKNOWN;
	if (state == LinuxLibeiState::READY || state == LinuxLibeiState::BINDING
		|| state == LinuxLibeiState::PAUSED)
		permission = AhkPermissionState::GRANTED;
	else if (state == LinuxLibeiState::PERMISSION_DENIED)
		permission = AhkPermissionState::DENIED;
	else if (state == LinuxLibeiState::REAUTH_REQUIRED)
		permission = AhkPermissionState::REAUTH_REQUIRED;
	unsigned count = 0;
	for (const auto &entry : sDevices) if (entry.device) ++count;
	LinuxInputBackendReportHealth(AhkInputBackendKind::LIBEI, state == LinuxLibeiState::READY
		? AhkBackendState::HEALTHY
		: state == LinuxLibeiState::PORTAL_CONNECTING
			? AhkBackendState::PROBING
			: state == LinuxLibeiState::EIS_CONNECTING
				? AhkBackendState::BINDING
				: state == LinuxLibeiState::BINDING
					? AhkBackendState::BINDING
					: state == LinuxLibeiState::PAUSED
						? AhkBackendState::DEGRADED
						: state == LinuxLibeiState::PERMISSION_DENIED
							? AhkBackendState::PERMISSION_DENIED
							: state == LinuxLibeiState::REAUTH_REQUIRED
								? AhkBackendState::REAUTH_REQUIRED
								: state == LinuxLibeiState::UNSUPPORTED
									? AhkBackendState::UNSUPPORTED
									: AhkBackendState::DISCONNECTED,
		sStatus.generation, sStatus.health_seq, sStatus.last_success_us,
		error, sReason, AhkDeviceCoverage{count, 0, 0,
			sCurrentTransaction ? 1u : 0u}, permission,
		state == LinuxLibeiState::READY,
		state == LinuxLibeiState::READY, state == LinuxLibeiState::READY);
}

void RefreshKeymapStatus()
{
	sStatus.keymap = false;
	for (const auto &entry : sDevices)
		if (entry.device && entry.resumed && entry.keyboard && entry.keymap)
		{ sStatus.keymap = true; break; }
}

void ClearKeymap(DeviceEntry &entry)
{
	if (entry.keymap) { xkb_keymap_unref(entry.keymap); entry.keymap = nullptr; }
	if (entry.xkb_context)
	{ xkb_context_unref(entry.xkb_context); entry.xkb_context = nullptr; }
	entry.shift_evdev = entry.altgr_evdev = 0;
	entry.depressed = entry.latched = entry.locked = 0;
	entry.group = 0;
	RefreshKeymapStatus();
}

unsigned int FindKeysymEvdev(xkb_keymap *keymap, xkb_keysym_t wanted)
{
	if (!keymap) return 0;
	for (xkb_keycode_t key = xkb_keymap_min_keycode(keymap);
		key <= xkb_keymap_max_keycode(keymap); ++key)
		for (xkb_layout_index_t layout = 0;
			layout < xkb_keymap_num_layouts_for_key(keymap, key); ++layout)
			for (xkb_level_index_t level = 0;
				level < xkb_keymap_num_levels_for_key(keymap, key, layout); ++level)
			{
				const xkb_keysym_t *syms = nullptr;
				int count = xkb_keymap_key_get_syms_by_level(keymap, key,
					layout, level, &syms);
				for (int i = 0; i < count; ++i)
					if (syms[i] == wanted && key >= 8 && key - 8 < KEY_CNT)
						return (unsigned int)key - 8;
			}
	return 0;
}

void LoadKeymap(DeviceEntry &entry)
{
	if (!entry.device || !entry.keyboard) return;
	ei_keymap *map = ei_device_keyboard_get_keymap(entry.device);
	if (!map || ei_keymap_get_type(map) != EI_KEYMAP_TYPE_XKB) return;
	int fd = ei_keymap_get_fd(map);
	size_t size = ei_keymap_get_size(map);
	if (fd < 0 || !size) return;
	void *mapped = mmap(nullptr, size, PROT_READ, MAP_PRIVATE, fd, 0);
	if (mapped == MAP_FAILED) return;
	std::string text((const char *)mapped, size);
	munmap(mapped, size);
	text.push_back('\0');
	xkb_context *context = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
	xkb_keymap *keymap = context ? xkb_keymap_new_from_string(context,
		text.c_str(), XKB_KEYMAP_FORMAT_TEXT_V1, XKB_KEYMAP_COMPILE_NO_FLAGS)
		: nullptr;
	if (!keymap)
	{
		if (context) xkb_context_unref(context);
		return;
	}
	ClearKeymap(entry);
	entry.xkb_context = context;
	entry.keymap = keymap;
	entry.shift_evdev = FindKeysymEvdev(entry.keymap, XKB_KEY_Shift_L);
	if (!entry.shift_evdev)
		entry.shift_evdev = FindKeysymEvdev(entry.keymap, XKB_KEY_Shift_R);
	entry.altgr_evdev = FindKeysymEvdev(entry.keymap, XKB_KEY_ISO_Level3_Shift);
	if (!entry.altgr_evdev)
		entry.altgr_evdev = FindKeysymEvdev(entry.keymap, XKB_KEY_Mode_switch);
	++sStatus.keymap_generation;
	RefreshKeymapStatus();
}

DeviceEntry *FindDevice(ei_device *device)
{
	for (auto &entry : sDevices) if (entry.device == device) return &entry;
	return nullptr;
}

DeviceEntry *AddDevice(ei_device *device)
{
	if (DeviceEntry *entry = FindDevice(device)) return entry;
	for (auto &entry : sDevices)
		if (!entry.device)
		{
			entry = DeviceEntry{};
			entry.device = ei_device_ref(device);
			entry.keyboard = ei_device_has_capability(device, EI_DEVICE_CAP_KEYBOARD);
			entry.pointer = ei_device_has_capability(device, EI_DEVICE_CAP_POINTER);
			entry.button = ei_device_has_capability(device, EI_DEVICE_CAP_BUTTON);
			entry.scroll = ei_device_has_capability(device, EI_DEVICE_CAP_SCROLL);
#ifdef AHK_LIBEI_HAS_TEXT
			entry.text = ei_device_has_capability(device, EI_DEVICE_CAP_TEXT);
#endif
			if (entry.keyboard) LoadKeymap(entry);
			return &entry;
		}
	return nullptr;
}

void InvalidateDeviceState(DeviceEntry &entry, const char *reason)
{
	bool held = false;
	for (unsigned code = 0; code < KEY_CNT; ++code)
	{
		if (entry.held_keys[code])
		{ sNeutralizedKeyUps[code] = 1; held = true; }
		if (entry.held_buttons[code])
		{ sNeutralizedButtonUps[code] = 1; held = true; }
	}
	entry.held_keys.fill(0);
	entry.held_buttons.fill(0);
	if (sCurrentTransaction)
		MarkFailed(reason ? reason : (held
			? "EIS device neutralized held state during transaction"
			: "EIS device changed during transaction"));
}

void RemoveDevice(ei_device *device)
{
	for (auto &entry : sDevices)
		if (entry.device == device)
		{
			InvalidateDeviceState(entry,
				"EIS device removed during transaction");
			ClearKeymap(entry);
			entry.device = ei_device_unref(entry.device);
			entry = DeviceEntry{};
			break;
		}
}

void UpdateCapabilities()
{
	sStatus.keyboard = sStatus.pointer = sStatus.button = sStatus.scroll
		= sStatus.text = false;
	bool ready = false, have = false;
	for (const auto &entry : sDevices)
	{
		if (!entry.device) continue;
		have = true;
		if (!entry.resumed) continue;
		ready = true;
		sStatus.keyboard |= entry.keyboard;
		sStatus.pointer |= entry.pointer;
		sStatus.button |= entry.button;
		sStatus.scroll |= entry.scroll;
		sStatus.text |= entry.text;
	}
	RefreshKeymapStatus();
	if (ready)
		Report(LinuxLibeiState::READY, "EIS devices resumed; target delivery unknown");
	else if (have)
		Report(LinuxLibeiState::PAUSED, "EIS devices are paused");
	else
		Report(LinuxLibeiState::BINDING, "waiting for EIS devices");
}

void ReleaseEntry(DeviceEntry &entry)
{
	if (!entry.device || !entry.resumed) return;
	bool changed = false;
	if (entry.keyboard)
		for (unsigned code = 0; code < entry.held_keys.size(); ++code)
			if (entry.held_keys[code])
			{
				ei_device_keyboard_key(entry.device, code, false);
				entry.held_keys[code] = 0; changed = true;
			}
	if (entry.button)
		for (unsigned code = 0; code < entry.held_buttons.size(); ++code)
			if (entry.held_buttons[code])
			{
				ei_device_button_button(entry.device, code, false);
				entry.held_buttons[code] = 0; changed = true;
			}
	if (changed) ei_device_frame(entry.device, ei_now(sEi));
	if (!sEiApiError) ei_device_stop_emulating(entry.device);
	entry.resumed = false;
}

void DestroyEi(bool neutral)
{
	if (!sEi) return;
	if (neutral)
		for (auto &entry : sDevices) ReleaseEntry(entry);
	for (auto &entry : sDevices)
	{
		ClearKeymap(entry);
		if (entry.device) entry.device = ei_device_unref(entry.device);
		entry = DeviceEntry{};
	}
#ifdef AHK_LIBEI_HAS_PING
	// Public disconnect was added together with ping in libei 1.4.
	ei_disconnect(sEi);
#endif
	sEi = ei_unref(sEi);
#ifdef AHK_LIBEI_HAS_PING
	sPings = {};
	sPingHead = 0;
#endif
	sStatus.eis_connected = false;
	sStatus.keyboard = sStatus.pointer = sStatus.button = sStatus.scroll
		= sStatus.text = false;
	RefreshKeymapStatus();
}

bool SetupEiFd(int fd)
{
	sEi = ei_new_sender(nullptr);
	if (!sEi) { close(fd); Report(LinuxLibeiState::DISCONNECTED,
		"ei_new_sender failed", ENOMEM); return false; }
	sEiApiError = false; sEiApiErrorText[0] = 0;
	ei_log_set_handler(sEi, EiLogHandler);
	ei_log_set_priority(sEi, EI_LOG_PRIORITY_WARNING);
	ei_configure_name(sEi, "AutoHotkey Linux");
	int setup_result = ei_setup_backend_fd(sEi, fd);
	if (setup_result != 0)
	{
		// libei takes ownership of fd even on setup failure; unref closes it.
		int error = setup_result < 0 ? -setup_result : EIO;
		sEi = ei_unref(sEi);
		Report(LinuxLibeiState::DISCONNECTED, "ei_setup_backend_fd failed", error);
		return false;
	}
	sStatus.eis_connected = true;
	Report(LinuxLibeiState::EIS_CONNECTING, "EIS fd connected; negotiating");
	return true;
}

bool SetupEiSocket(const char *path)
{
	sEi = ei_new_sender(nullptr);
	if (!sEi) { Report(LinuxLibeiState::DISCONNECTED,
		"ei_new_sender failed", ENOMEM); return false; }
	sEiApiError = false; sEiApiErrorText[0] = 0;
	ei_log_set_handler(sEi, EiLogHandler);
	ei_log_set_priority(sEi, EI_LOG_PRIORITY_WARNING);
	ei_configure_name(sEi, "AutoHotkey Linux oracle");
	int setup_result = ei_setup_backend_socket(sEi, path);
	if (setup_result != 0)
	{
		int error = setup_result < 0 ? -setup_result : EIO;
		sEi = ei_unref(sEi);
		Report(LinuxLibeiState::DISCONNECTED,
			"test EIS socket connection failed", error);
		return false;
	}
	sStatus.eis_connected = true;
	// Test-only direct socket may model an EIS fd owned by an already-active
	// portal session so disconnect handling can be verified without UI consent.
	sStatus.portal_session = EnvTrue(getenv("AHK_LIBEI_TEST_PORTAL_SESSION"));
	Report(LinuxLibeiState::EIS_CONNECTING,
		"test EIS socket connected; negotiating");
	return true;
}

void DispatchPortal()
{
	if (!sPortal) return;
	struct pollfd pfd = { oeffis_get_fd(sPortal), POLLIN | POLLHUP | POLLERR, 0 };
	if (pfd.fd < 0 || poll(&pfd, 1, 0) <= 0) return;
	bool portal_transport_dead =
		(pfd.revents & (POLLHUP | POLLERR | POLLNVAL)) != 0;
	oeffis_dispatch(sPortal);
	bool terminal = false;
	LinuxLibeiState terminal_state = LinuxLibeiState::DISCONNECTED;
	char terminal_reason[256] = "portal disconnected";
	for (oeffis_event_type event = oeffis_get_event(sPortal);
		!terminal && event != OEFFIS_EVENT_NONE;
		event = oeffis_get_event(sPortal))
	{
		switch (event)
		{
		case OEFFIS_EVENT_CONNECTED_TO_EIS:
		{
			int fd = oeffis_get_eis_fd(sPortal);
			if (fd < 0)
			{
				terminal = true;
				terminal_state = LinuxLibeiState::DISCONNECTED;
				snprintf(terminal_reason, sizeof(terminal_reason),
					"portal returned no EIS fd: %s", strerror(errno));
			}
			else
			{
				sStatus.portal_session = true;
				if (!SetupEiFd(fd))
				{
					terminal = true;
					terminal_state = LinuxLibeiState::DISCONNECTED;
					snprintf(terminal_reason, sizeof(terminal_reason),
						"failed to initialize portal EIS fd");
				}
			}
			break;
		}
		case OEFFIS_EVENT_CLOSED:
			terminal = true;
			terminal_state = LinuxLibeiState::REAUTH_REQUIRED;
			snprintf(terminal_reason, sizeof(terminal_reason),
				"RemoteDesktop session closed; new consent required");
			break;
		case OEFFIS_EVENT_DISCONNECTED:
		{
			terminal = true;
			const char *message = oeffis_get_error_message(sPortal);
			snprintf(terminal_reason, sizeof(terminal_reason), "%s",
				message && *message ? message : "RemoteDesktop portal disconnected");
			bool denied = CaseContains(message, "denied")
				|| CaseContains(message, "permission")
				|| CaseContains(message, "cancelled")
				|| CaseContains(message, "canceled")
				|| CaseContains(message, "not allowed");
			terminal_state = denied ? LinuxLibeiState::PERMISSION_DENIED
				: LinuxLibeiState::DISCONNECTED;
			break;
		}
		default: break;
		}
	}
	if (portal_transport_dead && !terminal)
	{
		terminal = true;
		terminal_state = LinuxLibeiState::DISCONNECTED;
		snprintf(terminal_reason, sizeof(terminal_reason),
			"RemoteDesktop portal transport disconnected");
	}
	if (terminal)
	{
		sTerminal = true; // M6b owns persistence-aware reconnection.
		DestroyEi(false); // terminal EIS state is already neutral/invalid.
		sPortal = oeffis_unref(sPortal);
		sPortalStarted = false;
		Report(terminal_state, terminal_reason,
			terminal_state == LinuxLibeiState::PERMISSION_DENIED ? EACCES : EPIPE);
	}
}

void DispatchEi()
{
	if (!sEi) return;
	struct pollfd pfd = { ei_get_fd(sEi), POLLIN | POLLHUP | POLLERR, 0 };
	if (pfd.fd < 0 || poll(&pfd, 1, 0) <= 0) return;
	bool transport_dead = (pfd.revents & (POLLHUP | POLLERR | POLLNVAL)) != 0;
	ei_dispatch(sEi);
	bool disconnected = false;
#ifdef AHK_LIBEI_HAS_PING
	std::array<PendingPing, 32> completed_pings = {};
	unsigned completed_ping_count = 0;
#endif
	for (ei_event *event = ei_get_event(sEi); event; event = ei_get_event(sEi))
	{
		ei_event_type type = ei_event_get_type(event);
		Trace("ei-event", 0, sStatus.last_sequence,
			ei_event_type_to_string(type), LinuxLibeiOutcome::NONE);
		switch (type)
		{
		case EI_EVENT_CONNECT:
			Trace("lifecycle", 0, sStatus.last_sequence, "connect",
				LinuxLibeiOutcome::NONE);
			Report(LinuxLibeiState::BINDING, "EIS connected; waiting for seat");
			break;
		case EI_EVENT_SEAT_ADDED:
		{
			ei_seat *seat = ei_event_get_seat(event);
#ifdef AHK_LIBEI_HAS_TEXT
			ei_seat_bind_capabilities(seat, EI_DEVICE_CAP_KEYBOARD,
				EI_DEVICE_CAP_POINTER, EI_DEVICE_CAP_BUTTON,
				EI_DEVICE_CAP_SCROLL, EI_DEVICE_CAP_TEXT, NULL);
#else
			ei_seat_bind_capabilities(seat, EI_DEVICE_CAP_KEYBOARD,
				EI_DEVICE_CAP_POINTER, EI_DEVICE_CAP_BUTTON,
				EI_DEVICE_CAP_SCROLL, NULL);
#endif
			Report(LinuxLibeiState::BINDING, "EIS seat capabilities bound");
			break;
		}
		case EI_EVENT_DEVICE_ADDED:
			AddDevice(ei_event_get_device(event));
			Trace("lifecycle", 0, sStatus.last_sequence, "device-added",
				LinuxLibeiOutcome::NONE);
			UpdateCapabilities();
			break;
		case EI_EVENT_DEVICE_RESUMED:
		{
			DeviceEntry *entry = AddDevice(ei_event_get_device(event));
			if (entry)
			{
				entry->resumed = true;
				if (++sNextSequence == 0) ++sNextSequence;
				entry->sequence = sNextSequence;
				sStatus.last_sequence = entry->sequence;
				ei_device_start_emulating(entry->device, entry->sequence);
				Trace("lifecycle", 0, entry->sequence, "device-resumed",
					LinuxLibeiOutcome::NONE);
			}
			UpdateCapabilities();
			break;
		}
		case EI_EVENT_DEVICE_PAUSED:
		{
			DeviceEntry *entry = FindDevice(ei_event_get_device(event));
			if (entry)
			{
				// EIS pause explicitly preserves device state. Keep held ownership
				// and modifier snapshot so the same device can receive matching
				// releases after resume; only removal implies neutralization.
				if (sCurrentTransaction)
					MarkFailed("EIS device paused during transaction");
				entry->resumed = false;
				Trace("lifecycle", 0, entry->sequence, "device-paused",
					LinuxLibeiOutcome::NONE);
			}
			UpdateCapabilities();
			break;
		}
		case EI_EVENT_DEVICE_REMOVED:
			Trace("lifecycle", 0, sStatus.last_sequence, "device-removed",
				LinuxLibeiOutcome::NONE);
			RemoveDevice(ei_event_get_device(event));
			UpdateCapabilities();
			break;
		case EI_EVENT_KEYBOARD_MODIFIERS:
		{
			DeviceEntry *entry = FindDevice(ei_event_get_device(event));
			if (entry)
			{
				entry->depressed = (xkb_mod_mask_t)
					ei_event_keyboard_get_xkb_mods_depressed(event);
				entry->latched = (xkb_mod_mask_t)
					ei_event_keyboard_get_xkb_mods_latched(event);
				entry->locked = (xkb_mod_mask_t)
					ei_event_keyboard_get_xkb_mods_locked(event);
				entry->group = (xkb_layout_index_t)
					ei_event_keyboard_get_xkb_group(event);
			}
			break;
		}
#ifdef AHK_LIBEI_HAS_PING
		case EI_EVENT_PONG:
		{
			struct ei_ping *ping = ei_event_pong_get_ping(event);
			uint64_t id = ping ? ei_ping_get_id(ping) : 0;
			for (auto &pending : sPings)
				if (pending.id == id && id)
				{
					if (completed_ping_count < completed_pings.size())
						completed_pings[completed_ping_count++] = pending;
					pending.id = 0;
					break;
				}
			break;
		}
#endif
		case EI_EVENT_DISCONNECT:
			disconnected = true;
			break;
		default: break; // future events are explicitly ignored then unref'd.
		}
		ei_event_unref(event);
	}
#ifdef AHK_LIBEI_HAS_PING
	if (!transport_dead && !disconnected)
		for (unsigned i = 0; i < completed_ping_count; ++i)
		{
			const PendingPing &pending = completed_pings[i];
			// An older synchronization result must never rewind a later FAILED or
			// submitted transaction's public status.
			if (pending.transaction >= sStatus.last_transaction_id)
			{
				sStatus.last_transaction_id = pending.transaction;
				sStatus.last_outcome = LinuxLibeiOutcome::EIS_PROCESSED;
				sStatus.last_success_us = LinuxInputEventMonotonicUs();
			}
			Trace("outcome", pending.transaction, pending.sequence, "pong",
				LinuxLibeiOutcome::EIS_PROCESSED,
				pending.send_level, pending.transport);
		}
	else
		for (unsigned i = 0; i < completed_ping_count; ++i)
			Trace("outcome", completed_pings[i].transaction,
				completed_pings[i].sequence, "pong-discarded-disconnect",
				LinuxLibeiOutcome::NONE, completed_pings[i].send_level,
				completed_pings[i].transport);
#endif
	if (transport_dead || disconnected)
	{
		bool portal_owned = sStatus.portal_session;
		sTerminal = true;
		DestroyEi(false);
		if (sPortal)
		{
			sPortal = oeffis_unref(sPortal);
			sPortalStarted = false;
		}
		Report(portal_owned ? LinuxLibeiState::REAUTH_REQUIRED
			: LinuxLibeiState::DISCONNECTED,
			portal_owned ? "EIS disconnected; reauthorization required"
				: "EIS disconnected");
	}
}

bool Start()
{
	if (sEi || sPortalStarted || sTerminal) return sEi || sPortalStarted;
	if (!sStatus.generation)
		sStatus.generation = LinuxInputBackendNextGeneration(AhkInputBackendKind::LIBEI);
	else
		++sStatus.generation;
	if (const char *socket = getenv("AHK_LIBEI_SOCKET"))
		if (*socket) return SetupEiSocket(socket);
	sStatus.portal_interface_version = PortalVersion();
	if (sStatus.portal_interface_version < 2)
	{
		sTerminal = true;
		Report(LinuxLibeiState::UNSUPPORTED,
			"RemoteDesktop portal v2/ConnectToEIS unavailable");
		return false;
	}
	sPortal = oeffis_new(nullptr);
	if (!sPortal)
	{
		sTerminal = true;
		Report(LinuxLibeiState::DISCONNECTED, "oeffis_new failed", ENOMEM);
		return false;
	}
	sPortalStarted = true;
	Report(LinuxLibeiState::PORTAL_CONNECTING,
		"waiting for RemoteDesktop consent and EIS fd");
	oeffis_create_session(sPortal, OEFFIS_DEVICE_KEYBOARD | OEFFIS_DEVICE_POINTER);
	return true;
}

DeviceEntry *DeviceFor(bool DeviceEntry::*capability)
{
	for (auto &entry : sDevices)
		if (entry.device && entry.resumed && entry.*capability)
			return &entry;
	return nullptr;
}

DeviceEntry *HeldKeyDevice(unsigned int code)
{
	if (code >= KEY_CNT) return nullptr;
	for (auto &entry : sDevices)
		if (entry.device && entry.resumed && entry.held_keys[code])
			return &entry;
	return nullptr;
}

DeviceEntry *HeldButtonDevice(unsigned int code)
{
	if (code >= KEY_CNT) return nullptr;
	for (auto &entry : sDevices)
		if (entry.device && entry.resumed && entry.held_buttons[code])
			return &entry;
	return nullptr;
}

DeviceEntry *WaitDevice(bool DeviceEntry::*capability, const char *name)
{
	if (DeviceEntry *entry = DeviceFor(capability)) return entry;
	unsigned timeout_ms = 1500;
	if (const char *value = getenv("AHK_LIBEI_DEVICE_TIMEOUT_MS"))
	{
		timeout_ms = (unsigned)strtoul(value, nullptr, 10);
		if (timeout_ms < 50) timeout_ms = 50;
		if (timeout_ms > 30000) timeout_ms = 30000;
	}
	uint64_t end = LinuxInputEventMonotonicUs() + (uint64_t)timeout_ms * 1000;
	while (!sTerminal && LinuxInputEventMonotonicUs() < end)
	{
		struct pollfd fds[2]; int count = 0;
		if (sPortal) fds[count++] = pollfd{oeffis_get_fd(sPortal), POLLIN, 0};
		if (sEi) fds[count++] = pollfd{ei_get_fd(sEi), POLLIN, 0};
		if (count) poll(fds, count, 10); else usleep(10000);
		LinuxLibeiDispatch();
		if (DeviceEntry *entry = DeviceFor(capability)) return entry;
	}
	char reason[192];
	snprintf(reason, sizeof(reason), "EIS %s capability unavailable after %u ms",
		name ? name : "requested", timeout_ms);
	SetReason(reason);
	return nullptr;
}

bool BeginImplicit(bool &implicit)
{
	implicit = sCurrentTransaction == 0;
	if (implicit) LinuxLibeiBeginTransaction(0, "implicit");
	return sCurrentTransaction != 0;
}

bool FailOperation(bool implicit, const char *reason)
{
	MarkFailed(reason);
	if (implicit) LinuxLibeiEndTransaction();
	return false;
}

void MarkFailed(const char *reason)
{
	if (sCurrentFailed) return; // Preserve the first failed phase in a batch.
	sCurrentFailed = true;
	sStatus.last_transaction_id = sCurrentTransaction;
	snprintf(sCurrentError, sizeof(sCurrentError), "%s", reason ? reason
		: "libei transaction failed");
	sStatus.last_outcome = LinuxLibeiOutcome::FAILED;
	SetReason(sCurrentError);
	Trace("failure", sCurrentTransaction, sCurrentSequence, sCurrentError,
		LinuxLibeiOutcome::FAILED);
}

void MarkSubmitted(const char *event, uint32_t sequence)
{
	sCurrentSubmitted = true;
	sCurrentSequence = sequence;
	sStatus.last_transaction_id = sCurrentTransaction;
	sStatus.last_sequence = sequence;
	if (!sCurrentFailed)
		sStatus.last_outcome = LinuxLibeiOutcome::SUBMITTED_TO_LIBEI;
	sStatus.last_success_us = LinuxInputEventMonotonicUs();
	Trace("submit", sCurrentTransaction, sequence, event,
		sCurrentFailed ? LinuxLibeiOutcome::FAILED
			: LinuxLibeiOutcome::SUBMITTED_TO_LIBEI);
}

bool SubmitFrame(DeviceEntry &entry, const char *event, bool implicit)
{
	ei_device_frame(entry.device, ei_now(sEi));
	if (sEiApiError)
	{
		MarkFailed(sEiApiErrorText);
		LinuxLibeiDispatch();
		if (implicit) LinuxLibeiEndTransaction();
		return false;
	}
	MarkSubmitted(event, entry.sequence);
	return true;
}

uint32_t ButtonCode(unsigned int button)
{
	switch (button)
	{
	case 1: return BTN_LEFT;
	case 2: return BTN_MIDDLE;
	case 3: return BTN_RIGHT;
	case 8: return BTN_SIDE;
	case 9: return BTN_EXTRA;
	default: return 0;
	}
}

bool FindUtf32(DeviceEntry &entry, uint32_t codepoint, unsigned int held_mods,
	unsigned int &evdev, bool &shift, bool &altgr)
{
	evdev = 0; shift = altgr = false;
	if (!entry.keymap || !codepoint) return false;
	xkb_mod_index_t shift_index = xkb_keymap_mod_get_index(entry.keymap,
		XKB_MOD_NAME_SHIFT);
	xkb_mod_index_t altgr_index = xkb_keymap_mod_get_index(entry.keymap, "Mod5");
	if (altgr_index == XKB_MOD_INVALID)
		altgr_index = xkb_keymap_mod_get_index(entry.keymap, "LevelThree");
	xkb_mod_mask_t shift_mask = shift_index == XKB_MOD_INVALID ? 0
		: (xkb_mod_mask_t)1u << shift_index;
	xkb_mod_mask_t altgr_mask = altgr_index == XKB_MOD_INVALID ? 0
		: (xkb_mod_mask_t)1u << altgr_index;
	xkb_mod_mask_t physical_current = entry.depressed | entry.latched | entry.locked;
	xkb_mod_mask_t selection_depressed = entry.depressed;
	// AHK-held modifiers are command modifiers around the character key, not
	// part of choosing which key/level represents that character. Remove them
	// from the lookup state while leaving external/locked modifiers intact.
	auto remove_held = [&](unsigned int ahk_mod, const char *xkb_name) {
		xkb_mod_index_t i = xkb_keymap_mod_get_index(entry.keymap, xkb_name);
		if ((held_mods & ahk_mod) && i != XKB_MOD_INVALID && i < 32)
			selection_depressed &= ~((xkb_mod_mask_t)1u << i);
		return i == XKB_MOD_INVALID || i >= 32 ? (xkb_mod_mask_t)0
			: (xkb_mod_mask_t)1u << i;
	};
	xkb_mod_mask_t held_shift_mask = remove_held(MOD_SHIFT, XKB_MOD_NAME_SHIFT);
	remove_held(MOD_CONTROL, XKB_MOD_NAME_CTRL);
	remove_held(MOD_ALT, XKB_MOD_NAME_ALT);
	remove_held(MOD_WIN, XKB_MOD_NAME_LOGO);
	// Server modifier feedback is asynchronous. Locally-held Shift is known to
	// be down even if its MODIFIERS event has not arrived yet.
	if (held_mods & MOD_SHIFT) physical_current |= held_shift_mask;
	struct Candidate { bool add_shift, add_altgr; } candidates[] = {
		{false, false}, {true, false}, {false, true}, {true, true}
	};
	for (const auto &candidate : candidates)
	{
		bool press_shift = candidate.add_shift && !(physical_current & shift_mask);
		bool press_altgr = candidate.add_altgr && !(physical_current & altgr_mask);
		if ((press_shift && (!shift_mask || !entry.shift_evdev))
			|| (press_altgr && (!altgr_mask || !entry.altgr_evdev)))
			continue;
		xkb_mod_mask_t depressed = selection_depressed
			| (candidate.add_shift ? shift_mask : 0)
			| (candidate.add_altgr ? altgr_mask : 0);
		xkb_state *state = xkb_state_new(entry.keymap);
		if (!state) return false;
		xkb_state_update_mask(state, depressed, entry.latched, entry.locked,
			0, 0, entry.group);
		for (xkb_keycode_t key = xkb_keymap_min_keycode(entry.keymap);
			key <= xkb_keymap_max_keycode(entry.keymap); ++key)
			if (xkb_state_key_get_utf32(state, key) == codepoint && key >= 8)
			{
				evdev = (unsigned int)key - 8;
				shift = press_shift; altgr = press_altgr;
				xkb_state_unref(state);
				return evdev < KEY_CNT;
			}
		xkb_state_unref(state);
	}
	return false;
}

#endif // HAVE_LIBEI

} // namespace

const char *LinuxLibeiStateName(LinuxLibeiState state)
{
	switch (state)
	{
	case LinuxLibeiState::NOT_BUILT: return "not-built";
	case LinuxLibeiState::DISABLED: return "disabled";
	case LinuxLibeiState::IDLE: return "idle";
	case LinuxLibeiState::PORTAL_CONNECTING: return "portal-connecting";
	case LinuxLibeiState::EIS_CONNECTING: return "eis-connecting";
	case LinuxLibeiState::BINDING: return "binding";
	case LinuxLibeiState::READY: return "ready";
	case LinuxLibeiState::PAUSED: return "paused";
	case LinuxLibeiState::DISCONNECTED: return "disconnected";
	case LinuxLibeiState::PERMISSION_DENIED: return "permission-denied";
	case LinuxLibeiState::REAUTH_REQUIRED: return "reauth-required";
	default: return "unsupported";
	}
}

const char *LinuxLibeiOutcomeName(LinuxLibeiOutcome outcome)
{
	switch (outcome)
	{
	case LinuxLibeiOutcome::SUBMITTED_TO_LIBEI: return "submitted-to-libei";
	case LinuxLibeiOutcome::EIS_PROCESSED: return "eis-processed";
	case LinuxLibeiOutcome::TARGET_DELIVERED_UNKNOWN: return "target-delivered-unknown";
	case LinuxLibeiOutcome::TARGET_CONSUMED_UNKNOWN: return "target-consumed-unknown";
	case LinuxLibeiOutcome::FAILED: return "failed";
	default: return "none";
	}
}

bool LinuxLibeiRequired()
{
	const char *value = getenv("AHK_LIBEI");
	return value && (!strcasecmp(value, "required") || !strcasecmp(value, "force"));
}

bool LinuxLibeiMayFallback()
{
	if (LinuxLibeiRequired()) return false;
	switch (sStatus.state)
	{
	case LinuxLibeiState::NOT_BUILT:
	case LinuxLibeiState::DISABLED:
	case LinuxLibeiState::IDLE:
	case LinuxLibeiState::UNSUPPORTED:
		return true;
	default:
		return false;
	}
}

bool LinuxLibeiShouldAttempt()
{
#ifdef HAVE_LIBEI
	const char *mode = getenv("AHK_LIBEI");
	if (EnvFalse(mode)) return false;
	if (sStatus.state == LinuxLibeiState::UNSUPPORTED && !LinuxLibeiRequired())
		return false; // Re-enable virtual-keyboard/uinput/paste auto fallback.
	if (getenv("AHK_LIBEI_SOCKET") || EnvTrue(mode)) return true;
	if (getenv("DISPLAY") && *getenv("DISPLAY")) return false;
	const char *session = getenv("XDG_SESSION_TYPE");
	return (session && !strcasecmp(session, "wayland")) || getenv("WAYLAND_DISPLAY");
#else
	return false;
#endif
}

bool LinuxLibeiEnsureReady(unsigned timeout_ms)
{
#ifdef HAVE_LIBEI
	if (!LinuxLibeiShouldAttempt())
	{
		if (sStatus.state == LinuxLibeiState::IDLE)
		{
			sStatus.state = LinuxLibeiState::DISABLED;
			SetReason("libei not selected for this session");
		}
		return false;
	}
	if (sStatus.state == LinuxLibeiState::READY)
	{
		LinuxLibeiDispatch();
		if (sEiApiError)
			LinuxLibeiDispatch();
		return sStatus.state == LinuxLibeiState::READY && !sEiApiError;
	}
	if (!Start() || sTerminal)
		return false;
	if (!timeout_ms)
	{
		const char *configured = getenv("AHK_LIBEI_CONSENT_TIMEOUT_MS");
		timeout_ms = configured ? (unsigned)strtoul(configured, nullptr, 10) : 30000;
		if (timeout_ms < 100) timeout_ms = 100;
		if (timeout_ms > 120000) timeout_ms = 120000;
	}
	uint64_t end = LinuxInputEventMonotonicUs() + (uint64_t)timeout_ms * 1000;
	while (!sTerminal && LinuxInputEventMonotonicUs() < end)
	{
		LinuxLibeiDispatch();
		if (sStatus.state == LinuxLibeiState::READY && !sEiApiError)
			return true;
		struct pollfd fds[2]; int count = 0;
		if (sPortal) fds[count++] = pollfd{oeffis_get_fd(sPortal), POLLIN, 0};
		if (sEi) fds[count++] = pollfd{ei_get_fd(sEi), POLLIN, 0};
		if (count) poll(fds, count, 10); else usleep(10000);
	}
	if (!sTerminal && sStatus.state != LinuxLibeiState::READY)
		Report(LinuxLibeiState::DISCONNECTED,
			"libei consent/handshake timed out", ETIMEDOUT);
	return sStatus.state == LinuxLibeiState::READY;
#else
	(void)timeout_ms;
	sStatus.state = LinuxLibeiState::NOT_BUILT;
	sStatus.last_errno = ENOSYS;
	SetReason("libei/liboeffis support not built");
	return false;
#endif
}

void LinuxLibeiDispatch()
{
#ifdef HAVE_LIBEI
	DispatchPortal();
	DispatchEi();
	if (sEiApiError && sEi)
	{
		bool portal_owned = sStatus.portal_session;
		sTerminal = true;
		char reason[256];
		snprintf(reason, sizeof(reason), "%s",
			sEiApiErrorText[0] ? sEiApiErrorText : "libei transport error");
		DestroyEi(false);
		if (sPortal)
		{
			sPortal = oeffis_unref(sPortal);
			sPortalStarted = false;
		}
		Report(portal_owned ? LinuxLibeiState::REAUTH_REQUIRED
			: LinuxLibeiState::DISCONNECTED, reason, EPIPE);
		sEiApiError = false;
	}
#endif
}

void LinuxLibeiShutdown()
{
#ifdef HAVE_LIBEI
	DestroyEi(true);
	if (sPortal) sPortal = oeffis_unref(sPortal);
	sPortalStarted = false;
	sStatus.portal_session = false;
	sTerminal = true;
	Report(LinuxLibeiState::DISCONNECTED, "libei shutdown");
#endif
}

void LinuxLibeiBeginTransaction(int send_level, const char *transport)
{
#ifdef HAVE_LIBEI
	if (sCurrentTransaction) return;
	if (++sNextTransaction == 0) ++sNextTransaction;
	sCurrentTransaction = sNextTransaction;
	sStatus.last_transaction_id = sCurrentTransaction;
	sStatus.last_outcome = LinuxLibeiOutcome::NONE;
	sCurrentSendLevel = send_level;
	snprintf(sCurrentTransport, sizeof(sCurrentTransport), "%s",
		transport ? transport : "event");
	sCurrentSubmitted = false;
	sCurrentFailed = false;
	sCurrentError[0] = 0;
	sCurrentSequence = 0;
	Trace("begin", sCurrentTransaction, sStatus.last_sequence,
		sCurrentTransport, LinuxLibeiOutcome::NONE);
#else
	(void)send_level; (void)transport;
#endif
}

void LinuxLibeiEndTransaction()
{
#ifdef HAVE_LIBEI
	if (!sCurrentTransaction) return;
#ifdef AHK_LIBEI_HAS_PING
	if (sCurrentSubmitted && !sCurrentFailed && sEi)
	{
		struct ei_ping *ping = ei_new_ping(sEi);
		if (ping)
		{
			uint64_t id = ei_ping_get_id(ping);
			PendingPing &pending = sPings[sPingHead++ % sPings.size()];
			pending = PendingPing{id, sCurrentTransaction, sCurrentSequence,
				sCurrentSendLevel, ""};
			snprintf(pending.transport, sizeof(pending.transport), "%s",
				sCurrentTransport);
			ei_ping(ping);
			ei_ping_unref(ping);
		}
	}
#endif
	Trace("end", sCurrentTransaction,
		sCurrentSequence ? sCurrentSequence : sStatus.last_sequence,
		sCurrentTransport, sCurrentFailed ? LinuxLibeiOutcome::FAILED
			: (sCurrentSubmitted ? LinuxLibeiOutcome::TARGET_DELIVERED_UNKNOWN
				: LinuxLibeiOutcome::NONE));
	sStatus.last_outcome = sCurrentFailed ? LinuxLibeiOutcome::FAILED
		: (sCurrentSubmitted ? LinuxLibeiOutcome::TARGET_DELIVERED_UNKNOWN
			: LinuxLibeiOutcome::NONE);
	if (sCurrentFailed) SetReason(sCurrentError);
	sCurrentTransaction = 0;
	sCurrentSubmitted = false;
	sCurrentFailed = false;
	sCurrentError[0] = 0;
	sCurrentSequence = 0;
#endif
}

bool LinuxLibeiTransactionSubmitted()
{
#ifdef HAVE_LIBEI
	return sCurrentSubmitted;
#else
	return false;
#endif
}

bool LinuxLibeiTransactionFailed()
{
#ifdef HAVE_LIBEI
	return sCurrentFailed;
#else
	return false;
#endif
}

const char *LinuxLibeiTransactionError()
{
#ifdef HAVE_LIBEI
	return sCurrentError;
#else
	return "libei/liboeffis support not built";
#endif
}

void LinuxLibeiFailTransaction(const char *reason)
{
#ifdef HAVE_LIBEI
	if (sCurrentTransaction) MarkFailed(reason);
#else
	(void)reason;
#endif
}

bool LinuxLibeiKeyEvent(unsigned int vk, bool down)
{
#ifdef HAVE_LIBEI
	if (!LinuxLibeiEnsureReady())
	{
		if (sCurrentTransaction) MarkFailed(sStatus.reason);
		return false;
	}
	bool batch_failed = sCurrentTransaction && sCurrentFailed;
	bool implicit; BeginImplicit(implicit);
	unsigned int code = LinuxWaylandKeycodeForVk(vk);
	if (!code || code >= KEY_CNT)
		return FailOperation(implicit,
			"key is not representable by EIS keyboard keycode");
	// Releases are routed back to the device which received the down, even if
	// a later phase in this batch failed or device enumeration order changed.
	DeviceEntry *entry = down ? nullptr : HeldKeyDevice(code);
	if (!down && !entry && sNeutralizedKeyUps[code])
	{
		sNeutralizedKeyUps[code] = 0;
		Trace("outcome", sCurrentTransaction, 0,
			"key-up-neutralized-by-eis", LinuxLibeiOutcome::NONE);
		if (implicit) LinuxLibeiEndTransaction();
		return true;
	}
	if (batch_failed && down) return false;
	if (!entry && batch_failed) return false;
	if (!entry) entry = WaitDevice(&DeviceEntry::keyboard, "keyboard");
	if (!entry)
		return FailOperation(implicit, "EIS keyboard capability unavailable");
	ei_device_keyboard_key(entry->device, code, down);
	if (sEiApiError)
	{
		MarkFailed(sEiApiErrorText);
		LinuxLibeiDispatch();
		if (implicit) LinuxLibeiEndTransaction();
		return true;
	}
	if (!SubmitFrame(*entry, down ? "key-down" : "key-up", implicit))
		return true;
	entry->held_keys[code] = down ? 1 : 0;
	if (down) sNeutralizedKeyUps[code] = 0;
	if (implicit) LinuxLibeiEndTransaction();
	return true;
#else
	(void)vk; (void)down; return false;
#endif
}

bool LinuxLibeiTapKey(unsigned int vk, int press_ms, int gap_ms)
{
#ifdef HAVE_LIBEI
	if (!LinuxLibeiEnsureReady())
	{
		if (sCurrentTransaction) MarkFailed(sStatus.reason);
		return false;
	}
	if (sCurrentTransaction && sCurrentFailed) return false;
	bool implicit; BeginImplicit(implicit);
	DeviceEntry *entry = WaitDevice(&DeviceEntry::keyboard, "keyboard");
	unsigned int code = LinuxWaylandKeycodeForVk(vk);
	if (!entry || !code || code >= KEY_CNT)
		return FailOperation(implicit, !entry
			? "EIS keyboard capability unavailable"
			: "key is not representable by EIS keyboard keycode");
	ei_device *original_device = entry->device;
	uint32_t original_sequence = entry->sequence;
	ei_device_keyboard_key(entry->device, code, true);
	if (sEiApiError)
	{
		MarkFailed(sEiApiErrorText);
		LinuxLibeiDispatch();
		if (implicit) LinuxLibeiEndTransaction();
		return true;
	}
	// EI key transitions must be separated by a frame. Queue the up
	// immediately when no PressDuration is requested, but never merge it into
	// the down frame.
	if (!SubmitFrame(*entry, "key-down", implicit))
		return true;
	entry->held_keys[code] = 1;
	if (press_ms > 0)
	{
		usleep((useconds_t)press_ms * 1000);
		LinuxLibeiDispatch();
		if (entry->device != original_device || !entry->resumed
			|| entry->sequence != original_sequence)
		{
			MarkFailed("EIS keyboard was paused/replaced during PressDuration");
			if (implicit) LinuxLibeiEndTransaction();
			return true; // down was submitted; never duplicate via fallback.
		}
	}
	ei_device_keyboard_key(entry->device, code, false);
	if (sEiApiError)
	{
		MarkFailed(sEiApiErrorText);
		LinuxLibeiDispatch();
		if (implicit) LinuxLibeiEndTransaction();
		return true;
	}
	if (!SubmitFrame(*entry, "key-up", implicit))
		return true;
	entry->held_keys[code] = 0;
	if (gap_ms > 0) usleep((useconds_t)gap_ms * 1000);
	if (implicit) LinuxLibeiEndTransaction();
	return true;
#else
	(void)vk; (void)press_ms; (void)gap_ms; return false;
#endif
}

bool LinuxLibeiButtonEvent(unsigned int button, bool down)
{
#ifdef HAVE_LIBEI
	if (!LinuxLibeiEnsureReady())
	{
		if (sCurrentTransaction) MarkFailed(sStatus.reason);
		return false;
	}
	bool batch_failed = sCurrentTransaction && sCurrentFailed;
	bool implicit; BeginImplicit(implicit);
	uint32_t code = ButtonCode(button);
	if (!code || code >= KEY_CNT)
		return FailOperation(implicit, "button is not representable by EIS");
	DeviceEntry *entry = down ? nullptr : HeldButtonDevice(code);
	if (!down && !entry && sNeutralizedButtonUps[code])
	{
		sNeutralizedButtonUps[code] = 0;
		Trace("outcome", sCurrentTransaction, 0,
			"button-up-neutralized-by-eis", LinuxLibeiOutcome::NONE);
		if (implicit) LinuxLibeiEndTransaction();
		return true;
	}
	if (batch_failed && down) return false;
	if (!entry && batch_failed) return false;
	if (!entry) entry = WaitDevice(&DeviceEntry::button, "button");
	if (!entry)
		return FailOperation(implicit, "EIS button capability unavailable");
	ei_device_button_button(entry->device, code, down);
	if (sEiApiError)
	{
		MarkFailed(sEiApiErrorText); LinuxLibeiDispatch();
		if (implicit) LinuxLibeiEndTransaction();
		return true;
	}
	if (!SubmitFrame(*entry, down ? "button-down" : "button-up", implicit))
		return true;
	entry->held_buttons[code] = down ? 1 : 0;
	if (down) sNeutralizedButtonUps[code] = 0;
	if (implicit) LinuxLibeiEndTransaction();
	return true;
#else
	(void)button; (void)down; return false;
#endif
}

bool LinuxLibeiTapButton(unsigned int button)
{
#ifdef HAVE_LIBEI
	if (!LinuxLibeiEnsureReady())
	{
		if (sCurrentTransaction) MarkFailed(sStatus.reason);
		return false;
	}
	if (sCurrentTransaction && sCurrentFailed) return false;
	bool implicit; BeginImplicit(implicit);
	DeviceEntry *entry = WaitDevice(&DeviceEntry::button, "button");
	uint32_t code = ButtonCode(button);
	if (!entry || !code || code >= KEY_CNT)
		return FailOperation(implicit, !entry
			? "EIS button capability unavailable"
			: "button is not representable by EIS");
	ei_device_button_button(entry->device, code, true);
	if (!sEiApiError && !SubmitFrame(*entry, "button-down", implicit))
		return true;
	entry->held_buttons[code] = sEiApiError ? 0 : 1;
	if (!sEiApiError)
		ei_device_button_button(entry->device, code, false);
	if (sEiApiError)
	{
		MarkFailed(sEiApiErrorText); LinuxLibeiDispatch();
		if (implicit) LinuxLibeiEndTransaction();
		return true;
	}
	if (!SubmitFrame(*entry, "button-up", implicit))
		return true;
	entry->held_buttons[code] = 0;
	if (implicit) LinuxLibeiEndTransaction();
	return true;
#else
	(void)button; return false;
#endif
}

bool LinuxLibeiScrollEvent(unsigned int button)
{
#ifdef HAVE_LIBEI
	if (!LinuxLibeiEnsureReady())
	{
		if (sCurrentTransaction) MarkFailed(sStatus.reason);
		return false;
	}
	if (sCurrentTransaction && sCurrentFailed) return false;
	bool implicit; BeginImplicit(implicit);
	DeviceEntry *entry = WaitDevice(&DeviceEntry::scroll, "scroll");
	if (!entry || button < 4 || button > 7)
		return FailOperation(implicit, !entry
			? "EIS scroll capability unavailable" : "invalid scroll axis");
	int32_t x = 0, y = 0;
	if (button == 4) y = -120;
	else if (button == 5) y = 120;
	else if (button == 6) x = -120;
	else x = 120;
	ei_device_scroll_discrete(entry->device, x, y);
	if (sEiApiError)
	{
		MarkFailed(sEiApiErrorText); LinuxLibeiDispatch();
		if (implicit) LinuxLibeiEndTransaction();
		return true;
	}
	if (!SubmitFrame(*entry, "scroll-discrete", implicit))
		return true;
	if (implicit) LinuxLibeiEndTransaction();
	return true;
#else
	(void)button; return false;
#endif
}

bool LinuxLibeiPointerMotion(double dx, double dy)
{
#ifdef HAVE_LIBEI
	if (!LinuxLibeiEnsureReady())
	{
		if (sCurrentTransaction) MarkFailed(sStatus.reason);
		return false;
	}
	if (sCurrentTransaction && sCurrentFailed) return false;
	bool implicit; BeginImplicit(implicit);
	DeviceEntry *entry = WaitDevice(&DeviceEntry::pointer, "pointer");
	if (!entry)
		return FailOperation(implicit, "EIS pointer capability unavailable");
	ei_device_pointer_motion(entry->device, dx, dy);
	if (sEiApiError)
	{
		MarkFailed(sEiApiErrorText); LinuxLibeiDispatch();
		if (implicit) LinuxLibeiEndTransaction();
		return true;
	}
	if (!SubmitFrame(*entry, "pointer-motion", implicit))
		return true;
	if (implicit) LinuxLibeiEndTransaction();
	return true;
#else
	(void)dx; (void)dy; return false;
#endif
}

bool LinuxLibeiSendUtf32(uint32_t codepoint, unsigned int held_mods,
	int press_ms, int gap_ms)
{
#ifdef HAVE_LIBEI
	if (!LinuxLibeiEnsureReady())
	{
		if (sCurrentTransaction) MarkFailed(sStatus.reason);
		return false;
	}
	if (sCurrentTransaction && sCurrentFailed) return false;
	bool implicit; BeginImplicit(implicit);
#ifdef AHK_LIBEI_HAS_TEXT
	if (!held_mods)
	{
	if (DeviceEntry *text = DeviceFor(&DeviceEntry::text))
	{
		char utf8[5] = {0};
		if (codepoint < 0x80) utf8[0] = (char)codepoint;
		else if (codepoint < 0x800)
		{ utf8[0] = (char)(0xC0 | codepoint >> 6); utf8[1] = (char)(0x80 | (codepoint & 63)); }
		else if (codepoint < 0x10000)
		{ utf8[0] = (char)(0xE0 | codepoint >> 12); utf8[1] = (char)(0x80 | ((codepoint >> 6) & 63)); utf8[2] = (char)(0x80 | (codepoint & 63)); }
		else
		{ utf8[0] = (char)(0xF0 | codepoint >> 18); utf8[1] = (char)(0x80 | ((codepoint >> 12) & 63)); utf8[2] = (char)(0x80 | ((codepoint >> 6) & 63)); utf8[3] = (char)(0x80 | (codepoint & 63)); }
		ei_device_text_utf8(text->device, utf8);
		if (sEiApiError)
		{
			MarkFailed(sEiApiErrorText); LinuxLibeiDispatch();
			if (implicit) LinuxLibeiEndTransaction();
			return true;
		}
		if (!SubmitFrame(*text, "text-utf8", implicit))
			return true;
		if (implicit) LinuxLibeiEndTransaction();
		return true;
	}
	}
#endif
	DeviceEntry *keyboard = nullptr;
	unsigned int code = 0; bool shift = false, altgr = false;
	bool have_keyboard = WaitDevice(&DeviceEntry::keyboard, "keyboard") != nullptr;
	for (auto &candidate : sDevices)
		if (candidate.device && candidate.resumed && candidate.keyboard)
		{
			have_keyboard = true;
			if (FindUtf32(candidate, codepoint, held_mods, code, shift, altgr))
			{ keyboard = &candidate; break; }
		}
	if (!keyboard)
		return FailOperation(implicit, !have_keyboard
			? "EIS keyboard/TEXT capability unavailable"
			: "character is not representable by any resumed EIS keymap and TEXT is unavailable");
	ei_device *original_device = keyboard->device;
	uint32_t original_sequence = keyboard->sequence;
	if (shift) ei_device_keyboard_key(keyboard->device, keyboard->shift_evdev, true);
	if (!sEiApiError && altgr)
		ei_device_keyboard_key(keyboard->device, keyboard->altgr_evdev, true);
	if (!sEiApiError) ei_device_keyboard_key(keyboard->device, code, true);
	if (sEiApiError)
	{
		MarkFailed(sEiApiErrorText); LinuxLibeiDispatch();
		if (implicit) LinuxLibeiEndTransaction();
		return true;
	}
	if (!SubmitFrame(*keyboard, "keymap-text-down", implicit))
		return true;
	if (press_ms > 0)
	{
		usleep((useconds_t)press_ms * 1000);
		LinuxLibeiDispatch();
		if (keyboard->device != original_device || !keyboard->resumed
			|| keyboard->sequence != original_sequence)
		{
			MarkFailed("EIS text keyboard was paused/replaced during PressDuration");
			if (implicit) LinuxLibeiEndTransaction();
			return true;
		}
	}
	ei_device_keyboard_key(keyboard->device, code, false);
	if (!sEiApiError && altgr)
		ei_device_keyboard_key(keyboard->device, keyboard->altgr_evdev, false);
	if (!sEiApiError && shift)
		ei_device_keyboard_key(keyboard->device, keyboard->shift_evdev, false);
	if (sEiApiError)
	{
		MarkFailed(sEiApiErrorText); LinuxLibeiDispatch();
		if (implicit) LinuxLibeiEndTransaction();
		return true;
	}
	if (!SubmitFrame(*keyboard, "keymap-text-up", implicit))
		return true;
	if (gap_ms > 0) usleep((useconds_t)gap_ms * 1000);
	if (implicit) LinuxLibeiEndTransaction();
	return true;
#else
	(void)codepoint; (void)held_mods; (void)press_ms; (void)gap_ms;
	return false;
#endif
}

const LinuxLibeiStatus &LinuxLibeiGetStatus()
{
	// Status queries are non-blocking. The portal interface probe is performed
	// only by Start(), immediately before an explicitly requested session.
	return sStatus;
}
