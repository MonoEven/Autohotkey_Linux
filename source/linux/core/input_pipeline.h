#pragma once

#include "input_event.h"
#include "input_backend.h"
#include <stdint.h>

constexpr unsigned AHK_INPUT_PIPELINE_VERSION = 1;

// M5a normalized input pipeline (check0901 P1-1 / check_detail0901 §4).
// InputEvent is fact, InputDecision is policy output, and injection remains a
// separate transaction.  Backend adapters only normalize and execute results.

enum class AhkInputSourceDomain : uint8_t
{
	UNKNOWN = 0,
	X11_GRAB = 1,
	X11_RAW = 2,
	EVDEV_LOCAL = 3,
	INPUTD = 4,
	PORTAL = 5,
	GNOME_SHELL = 6,
	IME = 7,
};

enum class AhkProvenanceConfidence : uint8_t
{
	UNKNOWN = 0,
	TIME_CORRELATED = 1,
	DEVICE_DERIVED = 2,
	AUTHORITATIVE = 3,
};

struct AhkInputContext
{
	AhkInputBackendKind backend;
	AhkInputSourceDomain domain;
	AhkProvenanceConfidence provenance;
	uint64_t authority_generation;
	uint64_t backend_sequence;
	uint64_t producer_client_id;
	uint64_t transaction_id;
	uint64_t parent_transaction_id;
	uint32_t seat_id;
	unsigned int modifier_snapshot; // adapter-observed MOD_* bits before event
	modLR_type modifier_lr_snapshot;
	bool modifier_snapshot_valid;
	bool state_authoritative;
};

struct AhkInputStateSnapshot
{
	uint64_t reducer_generation;
	uint64_t acceptance_seq;
	unsigned int modifiers; // logical Windows MOD_* bits after this event.
	modLR_type modifiers_lr;
	unsigned int physical_modifiers;
	modLR_type physical_modifiers_lr;
	bool event_was_down;
	bool event_was_repeat;
};

struct AhkInputAcceptance
{
	AhkInputEvent event;
	AhkInputContext context;
	AhkInputStateSnapshot state;
	bool duplicate;
};

struct Hotkey;
struct HotkeyVariant;

enum class AhkInputDecisionAction : uint8_t
{
	PASS_ORIGINAL,
	TRIGGER_PASS,
	TRIGGER_SUPPRESS,
	SUPPRESS_ORIGINAL,
	REPLACE_TRANSACTION,
	DUPLICATE_IGNORED,
	NO_MATCH,
	CONFLICT,
};

enum class AhkInputDecisionReason : uint8_t
{
	NONE,
	ORDINARY_HOTKEY,
	KEYUP_OWNERSHIP,
	LEVEL_FILTERED,
	BACKEND_LIMIT,
	LEGACY_COMBO,
	DUPLICATE_IDENTITY,
	MIRROR_MATCH,
	MIRROR_MISMATCH,
};

struct AhkInputMatch
{
	Hotkey *hotkey;
	HotkeyVariant *variant;
	uint64_t registration_id;
};

struct AhkInputDecision
{
	uint64_t acceptance_seq;
	uint64_t registration_id;
	AhkInputDecisionAction action;
	AhkInputDecisionReason reason;
	bool backend_can_suppress;
};

// Default active. AHK_INPUT_PIPELINE=legacy runs the old EVDEV matcher while
// mirroring into the new pipeline; =mirror keeps legacy dispatch with full
// comparison trace; =active (or unset) makes the pipeline authoritative.
bool LinuxInputPipelineActive();
bool LinuxInputPipelineMirrorOnly();

AhkInputAcceptance LinuxInputPipelineAccept(const AhkInputEvent &aEvent,
	const AhkInputContext &aContext);

bool LinuxInputPipelineMatchSingleHotkey(const AhkInputAcceptance &aAccepted,
	AhkInputBackendKind aBackend, AhkInputMatch &aMatch,
	AhkInputDecision &aDecision, bool aBackendCanSuppress);

void LinuxInputPipelineTraceLegacyComparison(const AhkInputAcceptance &aAccepted,
	const AhkInputMatch *aNewMatch, Hotkey *aLegacyHotkey,
	HotkeyVariant *aLegacyVariant);
void LinuxInputPipelineTraceDecision(const AhkInputAcceptance &aAccepted,
	const AhkInputDecision &aDecision);
void LinuxInputPipelineDispatch(const AhkInputAcceptance &aAccepted,
	const AhkInputMatch &aMatch, AhkInputDecision &aDecision);
void LinuxInputPipelineTraceOutcome(const AhkInputAcceptance &aAccepted,
	const AhkInputDecision &aDecision, const char *aOutcome);

bool LinuxInputPipelineHasState(AhkInputSourceDomain aDomain,
	uint64_t aAuthorityGeneration, uint32_t aSeatId);
bool LinuxInputPipelineVkDown(AhkInputSourceDomain aDomain,
	uint64_t aAuthorityGeneration, uint32_t aSeatId, unsigned int aVk);
uint64_t LinuxInputPipelineReducerGeneration(AhkInputSourceDomain aDomain,
	uint64_t aAuthorityGeneration, uint32_t aSeatId);
const char *LinuxInputPipelineModeName();
const char *LinuxInputPipelineStateSourceName(AhkInputSourceDomain aDomain);
void LinuxInputPipelineResetDomain(AhkInputSourceDomain aDomain,
	uint64_t aAuthorityGeneration = 0);
