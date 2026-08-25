// Client side of the ahk-inputd broker protocol (check_detail0824 §1-C / M4-C).
//
// Wire protocol v1 lives in ../inputd/inputd_proto.h.  The client connects to
// the broker socket, HELLOs, pushes SUBSCRIBE rules computed from the Hotkey
// table (EVDEV-assigned hotkeys, want_suppress cleared for tilde passthrough)
// and then dispatches EVENT frames into the same evdev matcher used by the
// in-process lane.  A lost broker connection simply deactivates the client so
// the lane can fall back to its own device scan.
#include "../../stdafx.h"
#include "../../hotkey.h"
#include "core_inputd_client_linux.h"
#include "input_backend.h"
#include "core_keymodel_linux.h"
#include "core_wayland_linux.h"
#include "core_capture_linux.h" // LinuxCaptureUsesRaw
#include "core_win_linux.h"     // LinuxX11Display
#include "../inputd/inputd_proto.h"
#include <X11/Xlib.h>
#include <linux/input.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <poll.h>
#include <fcntl.h>
#include <unistd.h>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>

namespace {

int sFd = -1;
bool sActive = false;
bool sConnectAttempted = false;
unsigned char sRx[4096];
size_t sRxUsed = 0;

void Disconnect()
{
	if (sFd >= 0)
		close(sFd);
	sFd = -1;
	sActive = false;
	sConnectAttempted = false;
	sRxUsed = 0;
}

int SocketPaths(char aPaths[][256], int aMax)
{
	if (aMax < 1)
		return 0;
	const char *env = getenv("AHK_INPUTD_SOCKET");
	if (env && *env)
	{
		snprintf(aPaths[0], 256, "%s", env);
		return 1; // Explicit configuration never falls through to another daemon.
	}
	int count = 0;
	auto append = [&](const char *path) {
		if (!path || !*path || count >= aMax)
			return;
		for (int i = 0; i < count; ++i)
			if (!strcmp(aPaths[i], path))
				return;
		snprintf(aPaths[count++], 256, "%s", path);
	};
	char path[256];
	const char *rt = getenv("XDG_RUNTIME_DIR");
	if (rt && *rt)
	{
		snprintf(path, sizeof(path), "%s/%s", rt, INPUTD_DEFAULT_SOCKET_NAME);
		append(path); // A manually-started per-user broker remains first.
	}
	append("/run/ahk-inputd.sock"); // Packaged systemd socket activation.
	// Never auto-connect to /tmp: another user can win the pathname race there.
	// Legacy/manual brokers remain available through explicit AHK_INPUTD_SOCKET.
	return count;
}

bool ReadExactly(int aFd, void *aBuf, size_t aN)
{
	size_t got = 0;
	while (got < aN)
	{
		ssize_t r = read(aFd, (char *)aBuf + got, aN - got);
		if (r == 0)
			return false;
		if (r < 0)
		{
			if (errno == EINTR)
				continue;
			return false;
		}
		got += (size_t)r;
	}
	return true;
}

bool WriteAll(int aFd, const void *aBuf, size_t aN)
{
	size_t sent = 0;
	while (sent < aN)
	{
		ssize_t r = write(aFd, (const char *)aBuf + sent, aN - sent);
		if (r < 0)
		{
			if (errno == EINTR)
				continue;
			return false;
		}
		sent += (size_t)r;
	}
	return true;
}

bool SendHello()
{
	unsigned char frame[4 + 1 + 4];
	unsigned int len = 5;
	memcpy(frame, &len, 4);
	frame[4] = INPUTD_C2S_HELLO;
	unsigned int ver = INPUTD_PROTO_VERSION;
	memcpy(frame + 5, &ver, 4);
	if (!WriteAll(sFd, frame, sizeof(frame)))
		return false;
	unsigned char ack[6];
	if (!ReadExactly(sFd, ack, sizeof(ack)) || ack[0] != INPUTD_S2C_ACK)
		return false;
	return ack[1] == 1 && ack[2] == INPUTD_PROTO_VERSION;
}

} // namespace

bool LinuxInputdClientConnect()
{
	if (sActive)
		return true;
	if (sConnectAttempted)
		return false; // Only one connect attempt per process (avoid retry churn).
	sConnectAttempted = true;
	if (getenv("AHK_INPUTD_DISABLE"))
		return false;

	char paths[2][256] = {{0}};
	int path_count = SocketPaths(paths, 2);
	for (int path_index = 0; path_index < path_count; ++path_index)
	{
		int fd = socket(AF_UNIX, SOCK_STREAM, 0);
		if (fd < 0)
			continue;
		struct sockaddr_un addr;
		memset(&addr, 0, sizeof(addr));
		addr.sun_family = AF_UNIX;
		size_t path_len = strlen(paths[path_index]);
		if (path_len >= sizeof(addr.sun_path))
		{
			close(fd);
			continue;
		}
		memcpy(addr.sun_path, paths[path_index], path_len + 1);
		if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) != 0)
		{
			close(fd);
			continue;
		}
		struct timeval tv = { 0, 500 * 1000 };
		setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
		sFd = fd;
		if (SendHello())
			break;
		close(fd);
		sFd = -1;
	}
	if (sFd < 0)
		return false;
	int flags = fcntl(sFd, F_GETFL, 0);
	fcntl(sFd, F_SETFL, flags | O_NONBLOCK);
	sActive = true;
	return true;
}

bool LinuxInputdClientActive()
{
	return sActive;
}

void LinuxInputdClientShutdown()
{
	Disconnect();
}

void LinuxInputdClientUpdateRules()
{
	if (!sActive)
		return;
	struct { unsigned int code; unsigned char suppress; } rules[INPUTD_MAX_RULES];
	int count = 0;
	auto add_rule = [&](unsigned int aCode, unsigned char aSuppress) {
		if (!aCode)
			return;
		for (int r = 0; r < count; ++r)
			if (rules[r].code == aCode)
			{
				if (aSuppress)
					rules[r].suppress = 1;
				return;
			}
		if (count < INPUTD_MAX_RULES)
		{
			rules[count].code = aCode;
			rules[count].suppress = aSuppress;
			++count;
		}
	};
	// EVDEV-assigned hotkeys with the want_suppress flag derived from tilde.
	for (int i = 0; i < Hotkey::sHotkeyCount && count < INPUTD_MAX_RULES; ++i)
	{
		Hotkey *hk = Hotkey::shk[i];
		if (!hk || hk->IsCompletelyDisabled())
			continue;
		if (!LinuxInputBackendHotkeyAssigned(hk, AhkInputBackendKind::EVDEV))
			continue;
		bool passthrough = (hk->mNoSuppress & (AT_LEAST_ONE_VARIANT_HAS_TILDE
			| AT_LEAST_ONE_COMBO_HAS_TILDE)) != 0;
		unsigned char sup = passthrough ? 0 : 1;
		if (hk->mSC)
			add_rule(LinuxEvdevCodeForScanCode(hk->mSC), sup);
		else if (hk->mVK)
			add_rule(LinuxWaylandKeycodeForVk(hk->mVK), sup);
		if (hk->mModifierSC)
			add_rule(LinuxEvdevCodeForScanCode(hk->mModifierSC), sup);
		else if (hk->mModifierVK)
			add_rule(LinuxWaylandKeycodeForVk(hk->mModifierVK), sup);
	}
	// Character-stream needs (Hotstring / visible InputHook): subscribe every
	// key which the X11 layout can produce text from, without suppression (the
	// M2-R backspace-replacement model relies on the original trigger reaching
	// the target first).  Pure Wayland has no X layout source here, so broker
	// char_stream stays limited to sessions with a working X11 display.
	if (LinuxCaptureUsesRaw())
	{
		Display *d = LinuxX11Display();
		if (d)
		{
			LinuxKeyModelX11Refresh(d);
			for (unsigned int code = 1; code < KEY_MAX && count < INPUTD_MAX_RULES; ++code)
			{
				sc_type sc = LinuxScanCodeForEvdev(code);
				if (!sc)
					continue;
				KeyCode xk = LinuxX11KeycodeForScanCode(sc);
				if (xk && LinuxKeyModelX11KeyProducesText(d, xk))
					add_rule(code, 0);
			}
		}
	}
	unsigned int payload_len = 1 + 4 + (unsigned int)count * 5;
	unsigned char *frame = (unsigned char *)malloc(4 + payload_len);
	if (!frame)
		return;
	memcpy(frame, &payload_len, 4);
	frame[4] = INPUTD_C2S_SUBSCRIBE;
	memcpy(frame + 5, &count, 4);
	for (int r = 0; r < count; ++r)
	{
		memcpy(frame + 9 + (size_t)r * 5, &rules[r].code, 4);
		frame[9 + (size_t)r * 5 + 4] = rules[r].suppress;
	}
	if (!WriteAll(sFd, frame, 4 + payload_len))
	{
		free(frame);
		Disconnect();
		return;
	}
	free(frame);
}

void LinuxInputdClientDispatch(LinuxInputdEventFn aFn, void *aUser)
{
	if (!sActive || sFd < 0)
		return;
	struct pollfd pfd = { sFd, POLLIN, 0 };
	int pr = poll(&pfd, 1, 0);
	if (pr <= 0)
		return;
	bool peer_closed = false;
	for (;;)
	{
		if (sRxUsed == sizeof(sRx))
		{
			Disconnect();
			return;
		}
		ssize_t got = read(sFd, sRx + sRxUsed, sizeof(sRx) - sRxUsed);
		if (got > 0)
			sRxUsed += (size_t)got;
		else if (got == 0)
			peer_closed = true;
		else if (errno == EINTR)
			continue;
		else if (errno != EAGAIN && errno != EWOULDBLOCK)
			peer_closed = true;

		while (sRxUsed)
		{
			size_t frame_size;
			switch (sRx[0])
			{
			case INPUTD_S2C_EVENT: frame_size = 14; break;
			case INPUTD_S2C_ACK: frame_size = 6; break;
			case INPUTD_S2C_PONG: frame_size = 1; break;
			default:
				Disconnect(); // protocol desync; caller may reconnect.
				return;
			}
			if (sRxUsed < frame_size)
				break; // stream fragment: keep it for the next dispatch.
			if (sRx[0] == INPUTD_S2C_EVENT)
			{
				unsigned int code;
				long long ts;
				memcpy(&code, sRx + 1, 4);
				memcpy(&ts, sRx + 6, 8);
				if (aFn)
					aFn(code, (int)sRx[5], ts, aUser);
			}
			sRxUsed -= frame_size;
			if (sRxUsed)
				memmove(sRx, sRx + frame_size, sRxUsed);
		}
		if (peer_closed)
		{
			Disconnect(); // caller starts a bounded reconnect window.
			return;
		}
		if (got < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
			return; // drained for now.
	}
}
