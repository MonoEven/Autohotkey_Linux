#include "../../stdafx.h"
#include "../../application.h"
#include "../../globaldata.h"
#include "../../hotkey.h"
#include "../../script.h"
#include "input_pipeline.h"
#include "input_semantics.h"
#include <linux/input.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace {

enum class PipelineMode { ACTIVE, MIRROR, LEGACY };

PipelineMode Mode()
{
	static PipelineMode mode = []() {
		const char *v = getenv("AHK_INPUT_PIPELINE");
		if (v && !strcmp(v, "legacy")) return PipelineMode::LEGACY;
		if (v && !strcmp(v, "mirror")) return PipelineMode::MIRROR;
		return PipelineMode::ACTIVE;
	}();
	return mode;
}

struct SeatState
{
	bool used;
	AhkInputSourceDomain domain;
	uint64_t authority_generation;
	uint32_t seat_id;
	uint64_t reducer_generation;
	bool physical_known;
	unsigned char logical_vk[256 / 8];
	unsigned char physical_vk[256 / 8];
	unsigned char logical_evdev[KEY_CNT / 8];
	unsigned char physical_evdev[KEY_CNT / 8];
	uint64_t keyup_owner[KEY_CNT];
	unsigned char keyup_suppress[KEY_CNT];
};

SeatState sSeats[16] = {};
uint64_t sAcceptanceSeq = 0;

struct RecentIdentity
{
	AhkInputSourceDomain domain;
	uint64_t authority_generation;
	uint64_t backend_sequence;
	bool release;
};
RecentIdentity sRecent[64] = {};
unsigned sRecentHead = 0;

void SetBit(unsigned char *bits, unsigned int cap, unsigned int id, bool down)
{
	if (id >= cap) return;
	if (down) bits[id / 8] |= (unsigned char)(1u << (id & 7));
	else bits[id / 8] &= (unsigned char)~(1u << (id & 7));
}

bool GetBit(const unsigned char *bits, unsigned int cap, unsigned int id)
{
	return id < cap && (bits[id / 8] & (1u << (id & 7))) != 0;
}

void RecomputeGenericModifiers(unsigned char *bits)
{
	SetBit(bits, 256, 0x10, GetBit(bits, 256, 0xA0) || GetBit(bits, 256, 0xA1));
	SetBit(bits, 256, 0x11, GetBit(bits, 256, 0xA2) || GetBit(bits, 256, 0xA3));
	SetBit(bits, 256, 0x12, GetBit(bits, 256, 0xA4) || GetBit(bits, 256, 0xA5));
}

void ApplyModifierSnapshot(unsigned char *bits, unsigned int snapshot,
	unsigned int event_code)
{
	if (event_code != KEY_LEFTSHIFT && event_code != KEY_RIGHTSHIFT)
		SetBit(bits, 256, 0x10, (snapshot & MOD_SHIFT) != 0);
	if (event_code != KEY_LEFTCTRL && event_code != KEY_RIGHTCTRL)
		SetBit(bits, 256, 0x11, (snapshot & MOD_CONTROL) != 0);
	if (event_code != KEY_LEFTALT && event_code != KEY_RIGHTALT)
		SetBit(bits, 256, 0x12, (snapshot & MOD_ALT) != 0);
	if (event_code != KEY_LEFTMETA && event_code != KEY_RIGHTMETA)
	{
		SetBit(bits, 256, 0x5B, (snapshot & MOD_WIN) != 0);
		SetBit(bits, 256, 0x5C, false);
	}
}

void SetModifierFromEvdev(unsigned char *bits, unsigned int code, bool down)
{
	switch (code)
	{
	case KEY_LEFTSHIFT: SetBit(bits, 256, 0xA0, down); break;
	case KEY_RIGHTSHIFT: SetBit(bits, 256, 0xA1, down); break;
	case KEY_LEFTCTRL: SetBit(bits, 256, 0xA2, down); break;
	case KEY_RIGHTCTRL: SetBit(bits, 256, 0xA3, down); break;
	case KEY_LEFTALT: SetBit(bits, 256, 0xA4, down); break;
	case KEY_RIGHTALT: SetBit(bits, 256, 0xA5, down); break;
	case KEY_LEFTMETA: SetBit(bits, 256, 0x5B, down); break;
	case KEY_RIGHTMETA: SetBit(bits, 256, 0x5C, down); break;
	}
	RecomputeGenericModifiers(bits);
}

modLR_type ModifiersLR(const unsigned char *bits)
{
	modLR_type lr = 0;
	if (GetBit(bits, 256, 0xA2)) lr |= MOD_LCONTROL;
	if (GetBit(bits, 256, 0xA3)) lr |= MOD_RCONTROL;
	if (GetBit(bits, 256, 0xA0)) lr |= MOD_LSHIFT;
	if (GetBit(bits, 256, 0xA1)) lr |= MOD_RSHIFT;
	if (GetBit(bits, 256, 0xA4)) lr |= MOD_LALT;
	if (GetBit(bits, 256, 0xA5)) lr |= MOD_RALT;
	if (GetBit(bits, 256, 0x5B)) lr |= MOD_LWIN;
	if (GetBit(bits, 256, 0x5C)) lr |= MOD_RWIN;
	return lr;
}

unsigned GenericFromLR(modLR_type lr)
{
	unsigned m = 0;
	if (lr & (MOD_LCONTROL | MOD_RCONTROL)) m |= MOD_CONTROL;
	if (lr & (MOD_LSHIFT | MOD_RSHIFT)) m |= MOD_SHIFT;
	if (lr & (MOD_LALT | MOD_RALT)) m |= MOD_ALT;
	if (lr & (MOD_LWIN | MOD_RWIN)) m |= MOD_WIN;
	return m;
}

void ApplyLRSnapshot(unsigned char *bits, modLR_type lr)
{
	SetBit(bits, 256, 0xA2, (lr & MOD_LCONTROL) != 0);
	SetBit(bits, 256, 0xA3, (lr & MOD_RCONTROL) != 0);
	SetBit(bits, 256, 0xA0, (lr & MOD_LSHIFT) != 0);
	SetBit(bits, 256, 0xA1, (lr & MOD_RSHIFT) != 0);
	SetBit(bits, 256, 0xA4, (lr & MOD_LALT) != 0);
	SetBit(bits, 256, 0xA5, (lr & MOD_RALT) != 0);
	SetBit(bits, 256, 0x5B, (lr & MOD_LWIN) != 0);
	SetBit(bits, 256, 0x5C, (lr & MOD_RWIN) != 0);
	RecomputeGenericModifiers(bits);
}

unsigned Modifiers(const unsigned char *bits)
{
	unsigned m = 0;
	if (GetBit(bits, 256, 0x10) || GetBit(bits, 256, 0xA0) || GetBit(bits, 256, 0xA1)) m |= MOD_SHIFT;
	if (GetBit(bits, 256, 0x11) || GetBit(bits, 256, 0xA2) || GetBit(bits, 256, 0xA3)) m |= MOD_CONTROL;
	if (GetBit(bits, 256, 0x12) || GetBit(bits, 256, 0xA4) || GetBit(bits, 256, 0xA5)) m |= MOD_ALT;
	if (GetBit(bits, 256, 0x5B) || GetBit(bits, 256, 0x5C)) m |= MOD_WIN;
	return m;
}

SeatState &Seat(const AhkInputContext &ctx)
{
	SeatState *free_slot = nullptr;
	for (SeatState &s : sSeats)
	{
		if (s.used && s.domain == ctx.domain && s.seat_id == ctx.seat_id)
		{
			if (s.authority_generation != ctx.authority_generation)
			{
				memset(s.logical_vk, 0, sizeof(s.logical_vk));
				memset(s.physical_vk, 0, sizeof(s.physical_vk));
				memset(s.logical_evdev, 0, sizeof(s.logical_evdev));
				memset(s.physical_evdev, 0, sizeof(s.physical_evdev));
				s.authority_generation = ctx.authority_generation;
				++s.reducer_generation;
			}
			return s;
		}
		if (!s.used && !free_slot) free_slot = &s;
	}
	SeatState &s = free_slot ? *free_slot : sSeats[(unsigned)ctx.domain % _countof(sSeats)];
	memset(&s, 0, sizeof(s));
	s.used = true;
	s.domain = ctx.domain;
	s.authority_generation = ctx.authority_generation;
	s.seat_id = ctx.seat_id;
	s.reducer_generation = 1;
	return s;
}

bool IsDuplicate(const AhkInputContext &ctx, bool release)
{
	if (!ctx.backend_sequence)
		return false;
	for (const RecentIdentity &r : sRecent)
		if (r.backend_sequence == ctx.backend_sequence
			&& r.authority_generation == ctx.authority_generation
			&& r.domain == ctx.domain && r.release == release)
			return true;
	sRecent[sRecentHead++ % _countof(sRecent)] = RecentIdentity{
		ctx.domain, ctx.authority_generation, ctx.backend_sequence, release};
	return false;
}

const char *DomainName(AhkInputSourceDomain d)
{
	switch (d)
	{
	case AhkInputSourceDomain::X11_GRAB: return "x11_grab";
	case AhkInputSourceDomain::X11_RAW: return "x11_raw";
	case AhkInputSourceDomain::EVDEV_LOCAL: return "evdev_local";
	case AhkInputSourceDomain::INPUTD: return "inputd";
	case AhkInputSourceDomain::PORTAL: return "portal";
	case AhkInputSourceDomain::GNOME_SHELL: return "gnome_shell";
	case AhkInputSourceDomain::IME: return "ime";
	default: return "unknown";
	}
}

const char *ActionName(AhkInputDecisionAction a)
{
	switch (a)
	{
	case AhkInputDecisionAction::TRIGGER_PASS: return "trigger_pass";
	case AhkInputDecisionAction::TRIGGER_SUPPRESS: return "trigger_suppress";
	case AhkInputDecisionAction::SUPPRESS_ORIGINAL: return "suppress_original";
	case AhkInputDecisionAction::REPLACE_TRANSACTION: return "replace_transaction";
	case AhkInputDecisionAction::DUPLICATE_IGNORED: return "duplicate_ignored";
	case AhkInputDecisionAction::NO_MATCH: return "no_match";
	case AhkInputDecisionAction::CONFLICT: return "conflict";
	default: return "pass_original";
	}
}

const char *ReasonName(AhkInputDecisionReason r)
{
	switch (r)
	{
	case AhkInputDecisionReason::ORDINARY_HOTKEY: return "ordinary_hotkey";
	case AhkInputDecisionReason::KEYUP_OWNERSHIP: return "keyup_ownership";
	case AhkInputDecisionReason::LEVEL_FILTERED: return "level_filtered";
	case AhkInputDecisionReason::BACKEND_LIMIT: return "backend_limit";
	case AhkInputDecisionReason::LEGACY_COMBO: return "legacy_combo";
	case AhkInputDecisionReason::DUPLICATE_IDENTITY: return "duplicate_identity";
	case AhkInputDecisionReason::MIRROR_MATCH: return "mirror_match";
	case AhkInputDecisionReason::MIRROR_MISMATCH: return "mirror_mismatch";
	default: return "none";
	}
}

void Trace(const char *stage, const AhkInputAcceptance &a,
	const AhkInputDecision *d = nullptr, const char *outcome = nullptr,
	int equivalent = -1)
{
	const char *path = getenv("AHK_INPUT_PIPELINE_TRACE");
	if (!path || !*path) return;
	FILE *f = fopen(path, "a");
	if (!f) return;
	fprintf(f,
		"{\"schema\":1,\"stage\":\"%s\",\"acceptance_seq\":%llu,"
		"\"backend_seq\":%llu,\"authority_generation\":%llu,"
		"\"domain\":\"%s\",\"origin\":\"%s\",\"source\":\"%s\","
		"\"vk\":%u,\"sc\":%u,\"evdev_code\":%u,\"release\":%s,"
		"\"repeat\":%s,\"send_level\":%d,\"mods\":%u,\"mods_lr\":%u,"
		"\"physical_mods\":%u,\"physical_mods_lr\":%u,\"transaction_id\":%llu,"
		"\"parent_transaction_id\":%llu,\"duplicate\":%s",
		stage, (unsigned long long)a.state.acceptance_seq,
		(unsigned long long)a.context.backend_sequence,
		(unsigned long long)a.context.authority_generation,
		DomainName(a.context.domain), LinuxInputOriginName(a.event.origin),
		LinuxInputSourceName(a.event.source), (unsigned)a.event.vk,
		(unsigned)a.event.sc, a.event.evdev_code,
		a.event.is_release ? "true" : "false",
		a.event.is_repeat ? "true" : "false", (int)a.event.send_level,
		a.state.modifiers, (unsigned)a.state.modifiers_lr,
		a.state.physical_modifiers, (unsigned)a.state.physical_modifiers_lr,
		(unsigned long long)a.context.transaction_id,
		(unsigned long long)a.context.parent_transaction_id,
		a.duplicate ? "true" : "false");
	if (d)
		fprintf(f, ",\"action\":\"%s\",\"reason\":\"%s\","
			"\"registration_id\":%llu,\"can_suppress\":%s",
			ActionName(d->action), ReasonName(d->reason),
			(unsigned long long)d->registration_id,
			d->backend_can_suppress ? "true" : "false");
	if (outcome) fprintf(f, ",\"outcome\":\"%s\"", outcome);
	if (equivalent >= 0) fprintf(f, ",\"equivalent\":%s", equivalent ? "true" : "false");
	fprintf(f, "}\n");
	fclose(f);
}

void TrackHotkey(Hotkey *hk)
{
	g_script.mPriorHotkeyName = g_script.mThisHotkeyName;
	g_script.mPriorHotkeyStartTime = g_script.mThisHotkeyStartTime;
	g_script.mThisHotkeyName = hk->mName;
	g_script.mThisHotkeyStartTime = GetTickCount();
}

} // namespace

bool LinuxInputPipelineActive() { return Mode() == PipelineMode::ACTIVE; }
bool LinuxInputPipelineMirrorOnly() { return Mode() == PipelineMode::MIRROR; }

AhkInputAcceptance LinuxInputPipelineAccept(const AhkInputEvent &aEvent,
	const AhkInputContext &aContext)
{
	AhkInputAcceptance out = {};
	out.event = aEvent;
	out.context = aContext;
	out.state.acceptance_seq = ++sAcceptanceSeq;
	out.duplicate = IsDuplicate(aContext, aEvent.is_release);
	SeatState &seat = Seat(aContext);
	bool down = !aEvent.is_release;
	bool was_down = aEvent.evdev_code
		? GetBit(seat.logical_evdev, KEY_CNT, aEvent.evdev_code)
		: GetBit(seat.logical_vk, 256, (unsigned)aEvent.vk);
	out.event.is_repeat = aEvent.is_repeat || (down && was_down);
	bool mutate_state = !out.duplicate
		&& !(out.event.is_release && out.event.is_repeat);
	if (mutate_state)
	{
		if (aContext.modifier_snapshot_valid)
		{
			ApplyLRSnapshot(seat.logical_vk, aContext.modifier_lr_snapshot);
			SetBit(seat.logical_vk, 256, 0x10,
				(aContext.modifier_snapshot & MOD_SHIFT) != 0);
			SetBit(seat.logical_vk, 256, 0x11,
				(aContext.modifier_snapshot & MOD_CONTROL) != 0);
			SetBit(seat.logical_vk, 256, 0x12,
				(aContext.modifier_snapshot & MOD_ALT) != 0);
			SetBit(seat.logical_vk, 256, 0x5B,
				(aContext.modifier_snapshot & MOD_WIN) != 0);
		}
		if (aEvent.evdev_code)
			SetBit(seat.logical_evdev, KEY_CNT, aEvent.evdev_code, down);
		SetModifierFromEvdev(seat.logical_vk, aEvent.evdev_code, down);
		if (aEvent.vk)
			SetBit(seat.logical_vk, 256, (unsigned)aEvent.vk, down);
		RecomputeGenericModifiers(seat.logical_vk);
		if (aContext.modifier_snapshot_valid)
		{
			ApplyLRSnapshot(seat.logical_vk, aContext.modifier_lr_snapshot);
			ApplyModifierSnapshot(seat.logical_vk, aContext.modifier_snapshot,
				aEvent.evdev_code);
		}
		if (aContext.state_authoritative)
		{
			seat.physical_known = true;
			if (aContext.modifier_snapshot_valid)
			{
				ApplyLRSnapshot(seat.physical_vk, aContext.modifier_lr_snapshot);
				ApplyModifierSnapshot(seat.physical_vk,
					aContext.modifier_snapshot, aEvent.evdev_code);
			}
			if (aEvent.evdev_code)
				SetBit(seat.physical_evdev, KEY_CNT, aEvent.evdev_code, down);
			SetModifierFromEvdev(seat.physical_vk, aEvent.evdev_code, down);
			if (aEvent.vk)
				SetBit(seat.physical_vk, 256, (unsigned)aEvent.vk, down);
			RecomputeGenericModifiers(seat.physical_vk);
			if (aContext.modifier_snapshot_valid)
			{
				ApplyLRSnapshot(seat.physical_vk, aContext.modifier_lr_snapshot);
				ApplyModifierSnapshot(seat.physical_vk,
					aContext.modifier_snapshot, aEvent.evdev_code);
			}
		}
		++seat.reducer_generation;
	}
	out.state.reducer_generation = seat.reducer_generation;
	out.state.modifiers = Modifiers(seat.logical_vk);
	out.state.modifiers_lr = ModifiersLR(seat.logical_vk);
	out.state.physical_modifiers = Modifiers(seat.physical_vk);
	out.state.physical_modifiers_lr = ModifiersLR(seat.physical_vk);
	out.state.event_was_down = was_down;
	out.state.event_was_repeat = out.event.is_repeat;
	Trace("capture", out);
	Trace("reduce", out);
	LinuxInputEventTrace(out.event); // legacy normalized trace remains supported.
	return out;
}

static int CountBits(modLR_type value)
{
	int count = 0;
	for (unsigned v = (unsigned)value; v; v >>= 1) count += (int)(v & 1);
	return count;
}

static bool MatchModifiers(Hotkey *hk, const AhkInputStateSnapshot &state)
{
	modLR_type required_lr = hk->mModifiersLR;
	unsigned required = (unsigned)hk->mModifiers | GenericFromLR(required_lr);
	if ((state.modifiers & required) != required)
		return false;
	if ((state.modifiers_lr & required_lr) != required_lr)
		return false;
	if (!hk->mAllowExtraModifiers)
	{
		if (state.modifiers_lr & ~hk->mModifiersConsolidatedLR)
			return false;
		if (state.modifiers & ~required)
			return false;
	}
	return true;
}

static bool IsPassthrough(Hotkey *hk, HotkeyVariant *vp)
{
	return (hk->mNoSuppress & (NO_SUPPRESS_PREFIX
		| AT_LEAST_ONE_VARIANT_HAS_TILDE | AT_LEAST_ONE_COMBO_HAS_TILDE))
		|| (vp->mNoSuppress & (NO_SUPPRESS_PREFIX
			| AT_LEAST_ONE_VARIANT_HAS_TILDE));
}

static bool FindBestHotkey(const AhkInputAcceptance &a,
	AhkInputBackendKind backend, bool release, AhkInputMatch &match,
	bool &level_filtered)
{
	memset(&match, 0, sizeof(match));
	int best = 0x7fffffff;
	for (int i = 0; i < Hotkey::sHotkeyCount; ++i)
	{
		Hotkey *hk = Hotkey::shk[i];
		if (!hk || hk->mModifierVK || hk->mModifierSC
			|| hk->IsCompletelyDisabled()
			|| !LinuxInputBackendHotkeyAssigned(hk, backend)
			|| (bool)hk->mKeyUp != release)
			continue;
		bool key_match = hk->mVK
			? (unsigned int)hk->mVK == (unsigned int)a.event.vk
			: hk->mSC && hk->mSC == a.event.sc;
		if (!key_match || !MatchModifiers(hk, a.state)
			|| !AhkBackendHotkeyEnabled(hk))
			continue;
		HotkeyVariant *vp = hk->FindVariant();
		if (!vp || !vp->mEnabled || !hk->PerformIsAllowed(*vp))
			continue;
		if (a.event.send_level >= 0
			&& !AhkSyntheticMayTrigger(AhkConsumerKind::HOTKEY,
				AhkSendTransportClass::EVENT, a.event.send_level,
				(int)vp->mInputLevel))
		{
			level_filtered = true;
			continue;
		}
		int score = (hk->mAllowExtraModifiers ? 8 : 0)
			+ CountBits(hk->mModifiersConsolidatedLR);
		if (score < best)
		{
			best = score;
			match = AhkInputMatch{hk, vp, (uint64_t)i + 1};
		}
	}
	return match.hotkey != nullptr;
}

bool LinuxInputPipelineMatchSingleHotkey(const AhkInputAcceptance &a,
	AhkInputBackendKind backend, AhkInputMatch &match,
	AhkInputDecision &decision, bool can_suppress)
{
	memset(&match, 0, sizeof(match));
	decision = AhkInputDecision{a.state.acceptance_seq, 0,
		AhkInputDecisionAction::NO_MATCH, AhkInputDecisionReason::NONE,
		can_suppress};
	if (a.duplicate || (a.event.is_release && a.event.is_repeat))
	{
		decision.action = AhkInputDecisionAction::DUPLICATE_IGNORED;
		decision.reason = AhkInputDecisionReason::DUPLICATE_IDENTITY;
		Trace("match", a, &decision);
		return false;
	}
	SeatState &seat = Seat(a.context);
	unsigned int code = a.event.evdev_code;
	bool level_filtered = false;
	bool found = !(a.event.is_release && a.event.is_repeat)
		&& FindBestHotkey(a, backend, a.event.is_release, match,
			level_filtered);
	uint64_t stored_owner = code < KEY_CNT ? seat.keyup_owner[code] : 0;
	bool stored_suppress = code < KEY_CNT && seat.keyup_suppress[code];
	if (a.event.is_release && stored_owner && found
		&& match.registration_id != stored_owner)
	{
		memset(&match, 0, sizeof(match));
		found = false;
	}

	if (!a.event.is_release && !a.event.is_repeat && code < KEY_CNT)
	{
		AhkInputMatch up_match;
		bool up_filtered = false;
		if (FindBestHotkey(a, backend, true, up_match, up_filtered))
		{
			bool owner_suppress = backend == AhkInputBackendKind::X11
				|| (can_suppress && !IsPassthrough(up_match.hotkey,
					up_match.variant));
			seat.keyup_owner[code] = up_match.registration_id;
			seat.keyup_suppress[code] = owner_suppress ? 1 : 0;
			stored_suppress = owner_suppress;
			if (!found)
			{
				decision.registration_id = up_match.registration_id;
				decision.action = owner_suppress
					? AhkInputDecisionAction::SUPPRESS_ORIGINAL
					: AhkInputDecisionAction::PASS_ORIGINAL;
				decision.reason = AhkInputDecisionReason::KEYUP_OWNERSHIP;
				Trace("match", a, &decision);
				return false;
			}
		}
		level_filtered = level_filtered || up_filtered;
	}

	if (found)
	{
		decision.registration_id = match.registration_id;
		bool suppress = can_suppress && !IsPassthrough(match.hotkey,
			match.variant);
		if (a.event.is_release && stored_suppress)
			suppress = true;
		if (!a.event.is_release && stored_suppress)
			suppress = true;
		decision.action = suppress ? AhkInputDecisionAction::TRIGGER_SUPPRESS
			: AhkInputDecisionAction::TRIGGER_PASS;
		decision.reason = AhkInputDecisionReason::ORDINARY_HOTKEY;
		if (a.event.is_release && code < KEY_CNT)
		{
			seat.keyup_owner[code] = 0;
			seat.keyup_suppress[code] = 0;
		}
		Trace("match", a, &decision);
		return true;
	}

	if (a.event.is_release && code < KEY_CNT && stored_suppress)
	{
		decision.registration_id = seat.keyup_owner[code];
		decision.action = AhkInputDecisionAction::SUPPRESS_ORIGINAL;
		decision.reason = AhkInputDecisionReason::KEYUP_OWNERSHIP;
		seat.keyup_owner[code] = 0;
		seat.keyup_suppress[code] = 0;
	}
	else
		decision.reason = level_filtered ? AhkInputDecisionReason::LEVEL_FILTERED
			: AhkInputDecisionReason::NONE;
	Trace("match", a, &decision);
	return false;
}

void LinuxInputPipelineTraceLegacyComparison(const AhkInputAcceptance &a,
	const AhkInputMatch *new_match, Hotkey *legacy_hk, HotkeyVariant *legacy_vp)
{
	bool equivalent = (!new_match || !new_match->hotkey)
		? !legacy_hk : (new_match->hotkey == legacy_hk
			&& new_match->variant == legacy_vp);
	AhkInputDecision d = {a.state.acceptance_seq,
		new_match ? new_match->registration_id : 0,
		equivalent ? AhkInputDecisionAction::PASS_ORIGINAL
			: AhkInputDecisionAction::CONFLICT,
		equivalent ? AhkInputDecisionReason::MIRROR_MATCH
			: AhkInputDecisionReason::MIRROR_MISMATCH, false};
	Trace("mirror", a, &d, nullptr, equivalent ? 1 : 0);
}

void LinuxInputPipelineTraceDecision(const AhkInputAcceptance &a,
	const AhkInputDecision &d)
{
	Trace("decision", a, &d);
}

void LinuxInputPipelineDispatch(const AhkInputAcceptance &a,
	const AhkInputMatch &match, AhkInputDecision &decision)
{
	if (!match.hotkey || !match.variant) return;
	Trace("dispatch", a, &decision, "begin");
	SendLevelType saved_send_level = g->SendLevel;
	++g_nThreads;
	++g;
	InitNewThread(match.variant->mPriority, false, false);
	g->SendLevel = match.variant->mInputLevel;
	TrackHotkey(match.hotkey);
	match.hotkey->PerformInNewThreadMadeByCaller(*match.variant);
	ResumeUnderlyingThread();
	g->SendLevel = saved_send_level;
	Trace("dispatch", a, &decision, "complete");
}

void LinuxInputPipelineTraceOutcome(const AhkInputAcceptance &a,
	const AhkInputDecision &d, const char *outcome)
{
	Trace("outcome", a, &d, outcome ? outcome : "unknown");
}

bool LinuxInputPipelineHasState(AhkInputSourceDomain domain,
	uint64_t authority_generation, uint32_t seat_id)
{
	for (const SeatState &s : sSeats)
		if (s.used && s.domain == domain && s.seat_id == seat_id
			&& s.authority_generation == authority_generation
			&& s.physical_known)
			return true;
	return false;
}

bool LinuxInputPipelineVkDown(AhkInputSourceDomain domain,
	uint64_t authority_generation, uint32_t seat_id, unsigned int vk)
{
	for (const SeatState &s : sSeats)
		if (s.used && s.domain == domain && s.seat_id == seat_id
			&& s.authority_generation == authority_generation)
			return GetBit(s.physical_vk, 256, vk);
	return false;
}

uint64_t LinuxInputPipelineReducerGeneration(AhkInputSourceDomain domain,
	uint64_t authority_generation, uint32_t seat_id)
{
	for (const SeatState &s : sSeats)
		if (s.used && s.domain == domain && s.seat_id == seat_id
			&& s.authority_generation == authority_generation)
			return s.reducer_generation;
	return 0;
}

const char *LinuxInputPipelineModeName()
{
	switch (Mode())
	{
	case PipelineMode::LEGACY: return "legacy";
	case PipelineMode::MIRROR: return "mirror";
	default: return "active";
	}
}

const char *LinuxInputPipelineStateSourceName(AhkInputSourceDomain domain)
{
	return DomainName(domain);
}

void LinuxInputPipelineResetDomain(AhkInputSourceDomain domain,
	uint64_t authority_generation)
{
	for (SeatState &s : sSeats)
		if (s.used && s.domain == domain
			&& (!authority_generation || s.authority_generation == authority_generation))
			memset(&s, 0, sizeof(s));
}
