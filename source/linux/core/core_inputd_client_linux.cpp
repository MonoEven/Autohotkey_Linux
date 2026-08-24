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

bool SocketPath(char *aBuf, size_t aSize)
{
	const char *env = getenv("AHK_INPUTD_SOCKET");
	if (env && *env)
	{
		snprintf(aBuf, aSize, "%s", env);
		return true;
	}
	const char *rt = getenv("XDG_RUNTIME_DIR");
	if (rt && *rt)
	{
		snprintf(aBuf, aSize, "%s/%s", rt, INPUTD_DEFAULT_SOCKET_NAME);
		return true;
	}
	snprintf(aBuf, aSize, "/tmp/%s", INPUTD_DEFAULT_SOCKET_NAME);
	return true;
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

	char path[256];
	SocketPath(path, sizeof(path));
	int fd = socket(AF_UNIX, SOCK_STREAM, 0);
	if (fd < 0)
		return false;
	struct sockaddr_un addr;
	memset(&addr, 0, sizeof(addr));
	addr.sun_family = AF_UNIX;
	snprintf(addr.sun_path, sizeof(addr.sun_path), "%s", path);
	if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) != 0)
	{
		close(fd);
		return false;
	}
	struct timeval tv = { 2, 0 };
	setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
	sFd = fd;
	if (!SendHello())
	{
		close(fd);
		sFd = -1;
		return false;
	}
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
	if (sFd >= 0)
		close(sFd);
	sFd = -1;
	sActive = false;
	sConnectAttempted = false; // allow a later reconnect
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
		sActive = false;
		close(sFd);
		sFd = -1;
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
	for (;;)
	{
		unsigned char hdr;
		ssize_t got = read(sFd, &hdr, 1);
		if (got == 0)
		{
			sActive = false; // broker closed: fall back to device lane.
			close(sFd);
			sFd = -1;
			return;
		}
		if (got < 0)
		{
			if (errno == EINTR)
				continue;
			if (errno == EAGAIN || errno == EWOULDBLOCK)
				break; // drained
			sActive = false;
			close(sFd);
			sFd = -1;
			return;
		}
		switch (hdr)
		{
		case INPUTD_S2C_EVENT:
		{
			unsigned char rest[13];
			if (!ReadExactly(sFd, rest, sizeof(rest)))
			{
				sActive = false;
				close(sFd);
				sFd = -1;
				return;
			}
			unsigned int code;
			long long ts;
			memcpy(&code, rest, 4);
			memcpy(&ts, rest + 5, 8);
			if (aFn)
				aFn(code, (int)rest[4], ts, aUser);
			break;
		}
		case INPUTD_S2C_ACK:
		{
			unsigned char rest[5];
			if (!ReadExactly(sFd, rest, sizeof(rest)))
			{
				sActive = false;
				close(sFd);
				sFd = -1;
				return;
			}
			break;
		}
		case INPUTD_S2C_PONG:
			break;
		default:
			sActive = false; // protocol desync: disconnect.
			close(sFd);
			sFd = -1;
			return;
		}
	}
}
