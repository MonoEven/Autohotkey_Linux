// Client side of the ahk-inputd broker protocol (check_detail0824 §1-C / M4-C).
//
// Wire protocol v1 and v2 live in ../inputd/inputd_proto.h.  The client
// connects to the broker socket and negotiates v2 first (magic "AHK2"):
// the broker grants capabilities, binds this connection's per-process script
// nonce to a client_id, and stamps every event with an authoritative
// provenance envelope (check0901 P0-3).  A v1-only broker closes the v2
// magic, and the client transparently reconnects with the legacy v1 HELLO.
//
// Rules are computed from the Hotkey table (EVDEV-assigned hotkeys,
// want_suppress cleared for tilde passthrough); EVENT frames dispatch into
// the same evdev matcher used by the in-process lane.  A lost broker
// connection simply deactivates the client so the lane can fall back to its
// own device scan.
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
bool sBackendDegraded = false;
unsigned char sRx[65536];
size_t sRxUsed = 0;

/* protocol v2 connection state (check0901 P0-3) */
bool sV2 = false;
unsigned long long sClientId = 0;
unsigned char sAuthorityId[16] = { 0 };
unsigned long long sGeneration = 0;
unsigned int sCapsGranted = 0;
unsigned int sCapsDenied = 0;
unsigned long long sClientSeq = 0;
unsigned char sNonce[16] = { 0 };
bool sNonceReady = false;
/* M2 local monotonic connection generation + authoritative broker snapshot. */
unsigned long long sConnectionGeneration = 0;
unsigned long long sLocalHealthSeq = 0;
unsigned long long sBrokerHealthSeq = 0;
unsigned long long sLastSuccessUs = 0;
int sLastErrno = 0;
AhkDeviceCoverage sCoverage = {0, 0, 0, 0};
AhkPermissionState sPermission = AhkPermissionState::UNKNOWN;
bool sReplayAvailable = false;
bool sRegistrationsReconciled = false;
bool sHeldStateReconciled = false;

void ReportHealth(AhkBackendState aState, const char *aReason)
{
	LinuxInputBackendReportHealth(AhkInputBackendKind::EVDEV, aState,
		sConnectionGeneration, ++sLocalHealthSeq, sLastSuccessUs, sLastErrno,
		aReason, sCoverage, sPermission, sReplayAvailable,
		sRegistrationsReconciled, sHeldStateReconciled);
}

void Disconnect(const char *aReason = "broker disconnected")
{
	bool had_connection = sFd >= 0 || sActive;
	if (had_connection)
	{
		sReplayAvailable = false;
		sRegistrationsReconciled = false;
		sHeldStateReconciled = false;
		ReportHealth(AhkBackendState::DISCONNECTED, aReason);
	}
	if (sFd >= 0)
		close(sFd);
	sFd = -1;
	sActive = false;
	sConnectAttempted = false;
	sRxUsed = 0;
	sBackendDegraded = false; // next connection re-probes health.
	sV2 = false;
	sClientId = 0;
	/* Preserve last authority generation/id for post-disconnect diagnostics;
	 * the next HELLO replaces it. */
	sCapsGranted = 0;
	sCapsDenied = 0;
	sClientSeq = 0;
}

void EnsureNonce()
{
	// Per-process random script nonce (check_detail0901 §3.2): never a PID.
	if (sNonceReady)
		return;
	sNonceReady = true;
	FILE *ur = fopen("/dev/urandom", "rb");
	if (ur)
	{
		if (fread(sNonce, 1, sizeof(sNonce), ur) != sizeof(sNonce))
			memset(sNonce, 0, sizeof(sNonce));
		fclose(ur);
	}
	unsigned long long mix = (unsigned long long)time(nullptr)
		^ ((unsigned long long)getpid() << 32) ^ 0xd1b54a32d192ed03ULL;
	if (!sNonce[0] && !sNonce[15])
		for (int i = 0; i < 16; ++i)
			sNonce[i] = (unsigned char)(mix >> ((i % 8) * 8));
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

/* ---- little-endian wire helpers (mirror inputd.c) ------------------------- */

void st_le16(unsigned char *p, unsigned int v)
{
	p[0] = (unsigned char)(v & 0xff);
	p[1] = (unsigned char)((v >> 8) & 0xff);
}

unsigned int ld_le16(const unsigned char *p)
{
	return (unsigned int)p[0] | ((unsigned int)p[1] << 8);
}

void st_le32(unsigned char *p, unsigned int v)
{
	p[0] = (unsigned char)(v & 0xff);
	p[1] = (unsigned char)((v >> 8) & 0xff);
	p[2] = (unsigned char)((v >> 16) & 0xff);
	p[3] = (unsigned char)((v >> 24) & 0xff);
}

unsigned int ld_le32(const unsigned char *p)
{
	return (unsigned int)p[0]
		| ((unsigned int)p[1] << 8)
		| ((unsigned int)p[2] << 16)
		| ((unsigned int)p[3] << 24);
}

unsigned long long ld_le64(const unsigned char *p)
{
	return (unsigned long long)ld_le32(p)
		| ((unsigned long long)ld_le32(p + 4) << 32);
}

/* ---- v1 HELLO (fallback) -------------------------------------------------- */

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

/* ---- v2 HELLO (check0901 P0-3) --------------------------------------------- */

bool ReadFrameV2(int aFd, unsigned int &aType, unsigned char *aPayload
	, size_t aPayloadCap, size_t &aPayloadLen)
{
	unsigned char head[4 + INPUTD_V2_HEADER_LEN];
	if (!ReadExactly(aFd, head, sizeof(head)))
		return false;
	if (ld_le32(head) != INPUTD_V2_MAGIC
		|| ld_le16(head + 4) != INPUTD_V2_PROTO_VERSION
		|| ld_le16(head + 6) != INPUTD_V2_HEADER_LEN)
		return false;
	unsigned int mlen = ld_le32(head + 8);
	if (mlen < INPUTD_V2_HEADER_LEN)
		return false;
	size_t plen = (size_t)mlen - INPUTD_V2_HEADER_LEN;
	if (plen > aPayloadCap)
		return false;
	if (plen && !ReadExactly(aFd, aPayload, plen))
		return false;
	aType = ld_le16(head + 12);
	aPayloadLen = plen;
	return true;
}

bool SendHelloV2()
{
	EnsureNonce();
	unsigned char payload[INPUTD_V2_HELLO_PAYLOAD_LEN];
	unsigned char *p = payload;
	st_le16(p, INPUTD_V2_PROTO_VERSION); p += 2;      /* min_proto */
	st_le16(p, INPUTD_V2_PROTO_VERSION); p += 2;      /* max_proto */
	memcpy(p, sNonce, 16); p += 16;
	st_le32(p, INPUTD_V2_CAP_OBSERVE | INPUTD_V2_CAP_SUPPRESS); p += 4;
	st_le16(p, 1); p += 2;                            /* event_schema */
	st_le16(p, 1u << INPUTD_V2_PAYLOAD_KEY); p += 2;  /* payload_kinds */
	st_le16(p, INPUTD_MAX_RULES); p += 2;             /* max_rules */
	unsigned char frame[4 + INPUTD_V2_HEADER_LEN + sizeof(payload)];
	unsigned char *f = frame;
	st_le32(f, INPUTD_V2_MAGIC); f += 4;
	st_le16(f, INPUTD_V2_PROTO_VERSION); f += 2;
	st_le16(f, INPUTD_V2_HEADER_LEN); f += 2;
	st_le32(f, INPUTD_V2_HEADER_LEN + (unsigned int)sizeof(payload)); f += 4;
	st_le16(f, INPUTD_V2_HELLO); f += 2;
	st_le16(f, 0); f += 2;                            /* flags */
	unsigned long long client_seq = ++sClientSeq;
	for (int i = 0; i < 8; ++i) { *f++ = 0; }         /* request_id = 0 */
	for (int i = 0; i < 8; ++i) *f++ = (unsigned char)(client_seq >> (8 * i));
	memcpy(f, payload, sizeof(payload));
	if (!WriteAll(sFd, frame, sizeof(frame)))
		return false;
	unsigned char ack[512];
	size_t ack_len = 0;
	unsigned int type = 0;
	if (!ReadFrameV2(sFd, type, ack, sizeof(ack), ack_len))
		return false;
	if (type == INPUTD_V2_ERROR)
		return false; // fall back to v1
	if (type != INPUTD_V2_HELLO_ACK || ack_len != INPUTD_V2_HELLO_ACK_PAYLOAD_LEN)
		return false;
	sClientId = ld_le64(ack + 2);
	memcpy(sAuthorityId, ack + 10, 16);
	sGeneration = ld_le64(ack + 26);
	sCapsGranted = ld_le32(ack + 34);
	sCapsDenied = ld_le32(ack + 38);
	return true;
}

bool SendSubscribeFrame()
{
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
		// check_detail0901 §7.2 rule 3: suppression is owner/root-only.  A
		// client without the SUPPRESS grant subscribes observe-only; the
		// broker would reject suppress rules with CAPABILITY_DENIED.
		if (sV2 && !(sCapsGranted & INPUTD_V2_CAP_SUPPRESS))
			sup = 0;
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

	if (sV2)
	{
		// v2 SUBSCRIBE: frame header + count + rules (same rule layout).
		size_t payload_len = 4 + (size_t)count * 5;
		unsigned char *frame = (unsigned char *)malloc(4 + INPUTD_V2_HEADER_LEN + payload_len);
		if (!frame)
			return false;
		unsigned char *f = frame;
		st_le32(f, INPUTD_V2_MAGIC); f += 4;
		st_le16(f, INPUTD_V2_PROTO_VERSION); f += 2;
		st_le16(f, INPUTD_V2_HEADER_LEN); f += 2;
		st_le32(f, INPUTD_V2_HEADER_LEN + (unsigned int)payload_len); f += 4;
		st_le16(f, INPUTD_V2_SUBSCRIBE); f += 2;
		st_le16(f, 0); f += 2;
		for (int i = 0; i < 8; ++i) *f++ = 0; /* request_id */
		unsigned long long client_seq = ++sClientSeq;
		for (int i = 0; i < 8; ++i) *f++ = (unsigned char)(client_seq >> (8 * i));
		st_le32(f, (unsigned int)count); f += 4;
		for (int r = 0; r < count; ++r)
		{
			st_le32(f, rules[r].code); f += 4;
			*f++ = rules[r].suppress;
		}
		bool ok = WriteAll(sFd, frame, 4 + INPUTD_V2_HEADER_LEN + payload_len);
		free(frame);
		return ok;
	}

	unsigned int payload_len = 1 + 4 + (unsigned int)count * 5;
	unsigned char *frame = (unsigned char *)malloc(4 + payload_len);
	if (!frame)
		return false;
	memcpy(frame, &payload_len, 4);
	frame[4] = INPUTD_C2S_SUBSCRIBE;
	memcpy(frame + 5, &count, 4);
	for (int r = 0; r < count; ++r)
	{
		memcpy(frame + 9 + (size_t)r * 5, &rules[r].code, 4);
		frame[9 + (size_t)r * 5 + 4] = rules[r].suppress;
	}
	bool ok = WriteAll(sFd, frame, 4 + payload_len);
	free(frame);
	return ok;
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
	EnsureNonce();
	ReportHealth(AhkBackendState::PROBING, "probing broker socket");

	char paths[2][256] = {{0}};
	int path_count = SocketPaths(paths, 2);
	for (int path_index = 0; path_index < path_count; ++path_index)
	{
		auto open_socket = [&](int &fd) {
			fd = socket(AF_UNIX, SOCK_STREAM, 0);
			if (fd < 0)
				return false;
			struct sockaddr_un addr;
			memset(&addr, 0, sizeof(addr));
			addr.sun_family = AF_UNIX;
			size_t path_len = strlen(paths[path_index]);
			if (path_len >= sizeof(addr.sun_path))
			{
				close(fd);
				fd = -1;
				return false;
			}
			memcpy(addr.sun_path, paths[path_index], path_len + 1);
			if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) != 0)
			{
				close(fd);
				fd = -1;
				return false;
			}
			struct timeval tv = { 0, 500 * 1000 };
			setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
			return true;
		};

		int fd = -1;
		if (!open_socket(fd))
			continue;
		sFd = fd;
		// v2 first (check0901 P0-3); a v1-only broker closes the v2 magic,
		// so reconnect fresh and fall back to the legacy v1 HELLO.
		if (SendHelloV2())
		{
			sV2 = true;
			sActive = true;
			break;
		}
		close(fd);
		sFd = -1;
		if (open_socket(fd))
		{
			sFd = fd;
			if (SendHello())
			{
				sV2 = false;
				sGeneration = 0;
				sBrokerHealthSeq = 0;
				memset(sAuthorityId, 0, sizeof(sAuthorityId));
				sActive = true;
				break;
			}
			close(fd);
			sFd = -1;
		}
	}
	if (sFd < 0)
	{
		ReportHealth(AhkBackendState::RETRY_WAIT, "broker socket unavailable");
		return false;
	}
	int flags = fcntl(sFd, F_GETFL, 0);
	fcntl(sFd, F_SETFL, flags | O_NONBLOCK);
	sActive = true;
	sConnectionGeneration = LinuxInputBackendNextGeneration(AhkInputBackendKind::EVDEV);
	sLocalHealthSeq = 0;
	sBrokerHealthSeq = 0;
	sCoverage = AhkDeviceCoverage{0, 0, 0, 0};
	sPermission = sV2 && (sCapsGranted & INPUTD_V2_CAP_OBSERVE)
		? AhkPermissionState::GRANTED : AhkPermissionState::UNKNOWN;
	sReplayAvailable = false;
	sRegistrationsReconciled = false;
	sHeldStateReconciled = false;
	ReportHealth(sV2 ? AhkBackendState::BINDING : AhkBackendState::AVAILABLE,
		sV2 ? "hello acknowledged; waiting for health and registration reconciliation"
			: "v1 broker connected; authoritative health telemetry unavailable");
	return true;
}

bool LinuxInputdClientActive()
{
	return sActive;
}

uint64_t LinuxInputdClientConnectionGeneration()
{
	return sConnectionGeneration;
}

uint64_t LinuxInputdClientAuthorityGeneration()
{
	return sGeneration;
}

uint64_t LinuxInputdClientBrokerHealthSeq()
{
	return sBrokerHealthSeq;
}

unsigned LinuxInputdClientCapsGranted()
{
	return sCapsGranted;
}

void LinuxInputdClientShutdown()
{
	Disconnect();
}

void LinuxInputdClientUpdateRules()
{
	if (!sActive)
		return;
	sRegistrationsReconciled = false;
	ReportHealth(AhkBackendState::RESUBSCRIBING, "sending desired registration set");
	if (!SendSubscribeFrame())
		Disconnect("registration write failed");
	else if (!sV2)
	{
		sRegistrationsReconciled = true;
		ReportHealth(AhkBackendState::AVAILABLE,
			"v1 registration sent; ACK/health guarantees unavailable");
	}
}

/* ---- v2 dispatch ----------------------------------------------------------- */

AhkBackendState WireHealthState(unsigned char aState)
{
	switch (aState)
	{
	case INPUTD_V2_HEALTH_PROBING: return AhkBackendState::PROBING;
	case INPUTD_V2_HEALTH_AVAILABLE: return AhkBackendState::AVAILABLE;
	case INPUTD_V2_HEALTH_BINDING: return AhkBackendState::BINDING;
	case INPUTD_V2_HEALTH_HEALTHY: return AhkBackendState::HEALTHY;
	case INPUTD_V2_HEALTH_DEGRADED: return AhkBackendState::DEGRADED;
	case INPUTD_V2_HEALTH_DISCONNECTED: return AhkBackendState::DISCONNECTED;
	case INPUTD_V2_HEALTH_RETRY_WAIT: return AhkBackendState::RETRY_WAIT;
	case INPUTD_V2_HEALTH_RESUBSCRIBING: return AhkBackendState::RESUBSCRIBING;
	case INPUTD_V2_HEALTH_RECONCILING_STATE: return AhkBackendState::RECONCILING_STATE;
	case INPUTD_V2_HEALTH_PERMISSION_DENIED: return AhkBackendState::PERMISSION_DENIED;
	case INPUTD_V2_HEALTH_UNSUPPORTED: return AhkBackendState::UNSUPPORTED;
	case INPUTD_V2_HEALTH_REAUTH_REQUIRED: return AhkBackendState::REAUTH_REQUIRED;
	case INPUTD_V2_HEALTH_SHUTDOWN: return AhkBackendState::SHUTDOWN;
	default: return AhkBackendState::UNINITIALIZED;
	}
}

AhkPermissionState WirePermission(unsigned char aState)
{
	switch (aState)
	{
	case INPUTD_V2_PERMISSION_GRANTED: return AhkPermissionState::GRANTED;
	case INPUTD_V2_PERMISSION_DENIED: return AhkPermissionState::DENIED;
	case INPUTD_V2_PERMISSION_REAUTH_REQUIRED: return AhkPermissionState::REAUTH_REQUIRED;
	default: return AhkPermissionState::UNKNOWN;
	}
}

void DispatchFrameV2(LinuxInputdEventFn aFn, void *aUser)
{
	unsigned int type = ld_le16(sRx + 4 + 8);
	size_t payload_len = (size_t)ld_le32(sRx + 4 + 4) - INPUTD_V2_HEADER_LEN;
	const unsigned char *payload = sRx + 4 + INPUTD_V2_HEADER_LEN;
	switch (type)
	{
	case INPUTD_V2_EVENT:
	{
		if (payload_len != INPUTD_V2_ENVELOPE_LEN + INPUTD_V2_KEY_PAYLOAD_LEN)
		{
			// Unknown envelope shape (future schema): skip the frame, keep
			// the lane alive.  Schema negotiation lives in HELLO.
			fprintf(stderr, "AHK warning: ahk-inputd EVENT envelope of "
				"unexpected size %zu ignored\n", payload_len);
			break;
		}
		const unsigned char *env = payload;
		const unsigned char *key = payload + INPUTD_V2_ENVELOPE_LEN;
		LinuxInputdEvent ev;
		ev.code = ld_le32(key);
		ev.value = (int)key[13];
		ev.tsUs = (long long)ld_le64(env + 32);
		ev.sendLevel = (int)(short)ld_le16(env + 52);
		ev.source = env[48];
		ev.confidence = env[50];
		ev.deviceId = (unsigned int)ld_le64(env + 40);
		ev.producerClientId = ld_le64(env + 58);
		ev.transactionId = ld_le64(env + 66);
		ev.parentTransactionId = ld_le64(env + 74);
		ev.v2 = true;
		if (aFn)
			aFn(ev, aUser);
		break;
	}
	case INPUTD_V2_ERROR:
	{
		if (payload_len >= 4 + 2)
		{
			unsigned int code = ld_le32(payload);
			size_t dlen = ld_le16(payload + 4);
			if (dlen > payload_len - 6)
				dlen = payload_len - 6;
			fprintf(stderr, "AHK warning: ahk-inputd protocol error %u: %.*s\n",
				code, (int)dlen, (const char *)payload + 6);
		}
		break;
	}
	case INPUTD_V2_BACKEND_HEALTH:
	{
		if (payload_len < INPUTD_V2_HEALTH_BASE_LEN)
			break;
		uint64_t authority_generation = ld_le64(payload + 4);
		uint64_t broker_seq = ld_le64(payload + 12);
		// A health snapshot for an old authority/sequence is stale and must not
		// mutate the current connection generation.
		if (authority_generation != sGeneration || broker_seq <= sBrokerHealthSeq)
			break;
		sBrokerHealthSeq = broker_seq;
		sLastSuccessUs = ld_le64(payload + 20);
		sLastErrno = (int)ld_le32(payload + 28);
		sCoverage.device_count = ld_le32(payload + 32);
		sCoverage.grabbed_count = ld_le32(payload + 36);
		sCoverage.registration_count = ld_le32(payload + 40);
		sCoverage.active_transaction_count = ld_le32(payload + 44);
		sPermission = WirePermission(payload[1]);
		unsigned int health_flags = ld_le16(payload + 2);
		sReplayAvailable = (health_flags & INPUTD_V2_HEALTH_FLAG_REPLAY_AVAILABLE) != 0;
		sRegistrationsReconciled = (health_flags
			& INPUTD_V2_HEALTH_FLAG_REGISTRATIONS_RECONCILED) != 0;
		sHeldStateReconciled = (health_flags
			& INPUTD_V2_HEALTH_FLAG_HELD_STATE_RECONCILED) != 0;
		size_t reason_len = ld_le16(payload + 48);
		if (reason_len > payload_len - INPUTD_V2_HEALTH_BASE_LEN)
			reason_len = payload_len - INPUTD_V2_HEALTH_BASE_LEN;
		char reason[192];
		if (reason_len >= sizeof(reason)) reason_len = sizeof(reason) - 1;
		memcpy(reason, payload + INPUTD_V2_HEALTH_BASE_LEN, reason_len);
		reason[reason_len] = '\0';
		AhkBackendState state = WireHealthState(payload[0]);
		if (state == AhkBackendState::HEALTHY && !sRegistrationsReconciled)
			state = AhkBackendState::RECONCILING_STATE;
		ReportHealth(state, reason);
		break;
	}
	case INPUTD_V2_BACKEND_DEGRADED:
	{
		// check0901 P0-1: the broker released all grabs (replay lane
		// failed).  Observe/listen-only from here on; surface it once.
		if (!sBackendDegraded)
		{
			sBackendDegraded = true;
			fprintf(stderr, "AHK warning: ahk-inputd degraded: replay "
				"lane failed, grabs released (listen-only)\n");
		}
		sReplayAvailable = false;
		ReportHealth(AhkBackendState::DEGRADED,
			"replay lane failed; grabs released (listen-only)");
		break;
	}
	case INPUTD_V2_SUBSCRIBE_ACK:
	case INPUTD_V2_UNSUBSCRIBE_ACK:
		if (payload_len >= 1 && payload[0])
		{
			sRegistrationsReconciled = true;
			ReportHealth(AhkBackendState::RECONCILING_STATE,
				"registration ACK received; waiting for health confirmation");
		}
		break;
	case INPUTD_V2_HELLO_ACK:
	case INPUTD_V2_PONG:
	case INPUTD_V2_DEVICE_ADDED:
	case INPUTD_V2_DEVICE_REMOVED:
		break; // bookkeeping frames
	default:
		Disconnect("protocol desync"); // caller may reconnect.
		return;
	}
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
			Disconnect("receive buffer overflow");
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
			if (sV2)
			{
				// magic(4) + version(2) + header_len(2) + message_len(4) + ...
				if (sRxUsed < 4)
					break;
				if (ld_le32(sRx) != INPUTD_V2_MAGIC
					|| sRxUsed < 4 + INPUTD_V2_HEADER_LEN)
				{
					if (sRxUsed < 4 + INPUTD_V2_HEADER_LEN)
						break; // stream fragment: keep it for the next dispatch.
					Disconnect("v2 magic mismatch");
					return;
				}
				unsigned int mlen = ld_le32(sRx + 8);
				if (ld_le16(sRx + 4) != INPUTD_V2_PROTO_VERSION
					|| ld_le16(sRx + 6) != INPUTD_V2_HEADER_LEN
					|| mlen < INPUTD_V2_HEADER_LEN
					|| mlen > sizeof(sRx) - 4)
				{
					Disconnect("invalid v2 frame header");
					return;
				}
				frame_size = 4 + (size_t)mlen;
			}
			else
			{
				switch (sRx[0])
				{
				case INPUTD_S2C_EVENT: frame_size = 14; break;
				case INPUTD_S2C_ACK: frame_size = 6; break;
				case INPUTD_S2C_PONG: frame_size = 1; break;
				case INPUTD_S2C_BACKEND_DEGRADED: frame_size = 1; break;
				default:
					Disconnect("v1 protocol desync"); // caller may reconnect.
					return;
				}
			}
			if (sRxUsed < frame_size)
				break; // stream fragment: keep it for the next dispatch.
			if (sV2)
				DispatchFrameV2(aFn, aUser);
			else if (sRx[0] == INPUTD_S2C_BACKEND_DEGRADED)
			{
				// check0901 P0-1: the broker released all grabs (replay lane
				// failed).  Observe/listen-only from here on; surface it once.
				if (!sBackendDegraded)
				{
					sBackendDegraded = true;
					fprintf(stderr, "AHK warning: ahk-inputd degraded: replay "
						"lane failed, grabs released (listen-only)\n");
				}
				sReplayAvailable = false;
				ReportHealth(AhkBackendState::DEGRADED,
					"v1 broker reported replay degradation");
			}
			else if (sRx[0] == INPUTD_S2C_EVENT)
			{
				LinuxInputdEvent ev;
				ev.code = 0;
				memcpy(&ev.code, sRx + 1, 4);
				ev.value = (int)sRx[5];
				ev.tsUs = 0;
				memcpy(&ev.tsUs, sRx + 6, 8);
				ev.sendLevel = -1;
				ev.source = INPUTD_V2_SOURCE_UNKNOWN;
				ev.confidence = INPUTD_V2_CONF_UNKNOWN;
				ev.deviceId = 0;
				ev.producerClientId = 0;
				ev.transactionId = 0;
				ev.parentTransactionId = 0;
				ev.v2 = false;
				if (aFn)
					aFn(ev, aUser);
			}
			if (!sActive)
				return; // DispatchFrameV2 may have disconnected us.
			sRxUsed -= frame_size;
			if (sRxUsed)
				memmove(sRx, sRx + frame_size, sRxUsed);
		}
		if (peer_closed)
		{
			Disconnect("broker socket closed"); // caller starts reconnect window.
			return;
		}
		if (got < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
			return; // drained for now.
	}
}
