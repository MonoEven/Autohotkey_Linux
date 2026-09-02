// Consented libei sender backend (check_detail0901 M6a).
#pragma once

#include <cstdint>

// These states describe the user-session injection lane, independently from
// whichever backend owns global hotkey capture.
enum class LinuxLibeiState
{
	NOT_BUILT,
	DISABLED,
	IDLE,
	PORTAL_CONNECTING,
	EIS_CONNECTING,
	BINDING,
	READY,
	PAUSED,
	DISCONNECTED,
	PERMISSION_DENIED,
	REAUTH_REQUIRED,
	UNSUPPORTED,
};

enum class LinuxLibeiOutcome
{
	NONE,
	SUBMITTED_TO_LIBEI,
	EIS_PROCESSED,
	TARGET_DELIVERED_UNKNOWN,
	TARGET_CONSUMED_UNKNOWN,
	FAILED,
};

struct LinuxLibeiStatus
{
	bool build_enabled;
	const char *libei_version;
	const char *liboeffis_version;
	const char *libportal_version;
	unsigned portal_interface_version;
	LinuxLibeiState state;
	uint64_t generation;
	uint64_t health_seq;
	uint64_t last_success_us;
	int last_errno;
	const char *reason;
	bool portal_session;
	bool eis_connected;
	bool keyboard;
	bool pointer;
	bool button;
	bool scroll;
	bool text;
	bool keymap;
	uint64_t keymap_generation;
	uint64_t last_transaction_id;
	uint32_t last_sequence;
	LinuxLibeiOutcome last_outcome;
};

bool LinuxLibeiShouldAttempt();
bool LinuxLibeiRequired();
bool LinuxLibeiMayFallback();
bool LinuxLibeiEnsureReady(unsigned aTimeoutMs = 0);
void LinuxLibeiDispatch();
void LinuxLibeiShutdown();

void LinuxLibeiBeginTransaction(int aSendLevel, const char *aTransport);
void LinuxLibeiEndTransaction();
bool LinuxLibeiTransactionSubmitted();
bool LinuxLibeiTransactionFailed();
const char *LinuxLibeiTransactionError();
void LinuxLibeiFailTransaction(const char *aReason);

bool LinuxLibeiKeyEvent(unsigned int aVk, bool aDown);
bool LinuxLibeiTapKey(unsigned int aVk, int aPressMs, int aGapMs);
bool LinuxLibeiButtonEvent(unsigned int aXButton, bool aDown);
bool LinuxLibeiTapButton(unsigned int aXButton);
bool LinuxLibeiScrollEvent(unsigned int aXButton);
bool LinuxLibeiPointerMotion(double aDx, double aDy);
bool LinuxLibeiSendUtf32(uint32_t aCodepoint, unsigned int aHeldMods = 0,
	int aPressMs = 0, int aGapMs = 0);

const LinuxLibeiStatus &LinuxLibeiGetStatus();
const char *LinuxLibeiStateName(LinuxLibeiState aState);
const char *LinuxLibeiOutcomeName(LinuxLibeiOutcome aOutcome);
