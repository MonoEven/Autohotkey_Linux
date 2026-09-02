#include "../../stdafx.h"
#include "../../application.h"
#include "../../globaldata.h"
#include "../../hotkey.h"
#include "../../script.h"
#include "input_pipeline.h"
#include "input_semantics.h"
#include "core_keymodel_linux.h"
#include "core_wayland_linux.h"
#include "../inputd/inputd_proto.h"
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
	unsigned char prefix_down[KEY_CNT];
	unsigned char prefix_used[KEY_CNT];
	unsigned char prefix_passthrough[KEY_CNT];
	unsigned int prefix_mods[KEY_CNT];
	modLR_type prefix_mods_lr[KEY_CNT];
	unsigned char combo_suppressed[KEY_CNT];
	uint64_t combo_owner[KEY_CNT];
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

const char *ConsumerName(AhkInputConsumerKind c)
{
	return c == AhkInputConsumerKind::INPUTHOOK ? "inputhook" : "hotstring";
}

const char *ConsumerActionName(AhkInputConsumerAction a)
{
	switch (a)
	{
	case AhkInputConsumerAction::COLLECTED: return "collected";
	case AhkInputConsumerAction::CALLBACK_QUEUED: return "callback_queued";
	case AhkInputConsumerAction::TRIGGERED: return "triggered";
	case AhkInputConsumerAction::REPLACED: return "replaced";
	case AhkInputConsumerAction::SUPPRESSED: return "suppressed";
	case AhkInputConsumerAction::PASSED: return "passed";
	default: return "ignored";
	}
}

const char *ConsumerReasonName(AhkInputConsumerReason r)
{
	switch (r)
	{
	case AhkInputConsumerReason::LEVEL_FILTERED: return "level_filtered";
	case AhkInputConsumerReason::TRANSPORT_FILTERED: return "transport_filtered";
	case AhkInputConsumerReason::KEY_CALLBACK: return "key_callback";
	case AhkInputConsumerReason::CHAR_BUFFERED: return "char_buffered";
	case AhkInputConsumerReason::END_KEY: return "end_key";
	case AhkInputConsumerReason::HOTSTRING_BUFFERED: return "hotstring_buffered";
	case AhkInputConsumerReason::HOTSTRING_MATCHED: return "hotstring_matched";
	case AhkInputConsumerReason::LEVEL_ZERO_EXCLUDED: return "level_zero_excluded";
	case AhkInputConsumerReason::SELECTED_GRAB_OWNS_EVENT: return "selected_grab_owns_event";
	default: return "none";
	}
}

const char *ReasonName(AhkInputDecisionReason r)
{
	switch (r)
	{
	case AhkInputDecisionReason::ORDINARY_HOTKEY: return "ordinary_hotkey";
	case AhkInputDecisionReason::KEYUP_OWNERSHIP: return "keyup_ownership";
	case AhkInputDecisionReason::COMBO_PREFIX_HELD: return "combo_prefix_held";
	case AhkInputDecisionReason::COMBO_SUFFIX: return "combo_suffix";
	case AhkInputDecisionReason::COMBO_RELEASE: return "combo_release";
	case AhkInputDecisionReason::COMBO_STANDALONE: return "combo_standalone";
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

void TraceConsumer(const char *stage, const AhkInputAcceptance &a,
	const AhkInputConsumerDecision &d, const char *outcome)
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
		"\"repeat\":%s,\"send_level\":%d,\"transaction_id\":%llu,"
		"\"parent_transaction_id\":%llu,\"consumer\":\"%s\","
		"\"consumer_action\":\"%s\",\"consumer_reason\":\"%s\","
		"\"registration_id\":%llu,\"suppress_original\":%s",
		stage, (unsigned long long)a.state.acceptance_seq,
		(unsigned long long)a.context.backend_sequence,
		(unsigned long long)a.context.authority_generation,
		DomainName(a.context.domain), LinuxInputOriginName(a.event.origin),
		LinuxInputSourceName(a.event.source), (unsigned)a.event.vk,
		(unsigned)a.event.sc, a.event.evdev_code,
		a.event.is_release ? "true" : "false",
		a.event.is_repeat ? "true" : "false", (int)a.event.send_level,
		(unsigned long long)a.context.transaction_id,
		(unsigned long long)a.context.parent_transaction_id,
		ConsumerName(d.consumer), ConsumerActionName(d.action),
		ConsumerReasonName(d.reason), (unsigned long long)d.registration_id,
		d.suppress_original ? "true" : "false");
	if (outcome) fprintf(f, ",\"outcome\":\"%s\"", outcome);
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
		HotkeyVariant *vp = nullptr;
		for (HotkeyVariant *candidate = hk->mFirstVariant; candidate;
			candidate = candidate->mNextVariant)
			if (AhkBackendVariantFireable(hk, candidate)
				&& hk->PerformIsAllowed(*candidate))
			{ vp = candidate; break; }
		if (!vp) continue;
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

static unsigned int ComboKeyCode(Hotkey *hk, bool prefix)
{
	if (!hk) return 0;
	vk_type vk = prefix ? hk->mModifierVK : hk->mVK;
	sc_type sc = prefix ? hk->mModifierSC : hk->mSC;
	if (sc) return LinuxEvdevCodeForScanCode(sc);
	return vk ? LinuxWaylandKeycodeForVk(vk) : 0;
}

static bool ComboVariant(Hotkey *hk, HotkeyVariant *&vp)
{
	vp = nullptr;
	if (!hk || !AhkBackendHotkeyEnabled(hk)) return false;
	for (HotkeyVariant *candidate = hk->mFirstVariant; candidate;
		candidate = candidate->mNextVariant)
		if (AhkBackendVariantFireable(hk, candidate)
			&& hk->PerformIsAllowed(*candidate))
		{ vp = candidate; return true; }
	return false;
}

static bool NativePrefix(unsigned int code)
{
	switch (code)
	{
	case KEY_LEFTSHIFT: case KEY_RIGHTSHIFT: case KEY_LEFTCTRL: case KEY_RIGHTCTRL:
	case KEY_LEFTALT: case KEY_RIGHTALT: case KEY_LEFTMETA: case KEY_RIGHTMETA:
	case KEY_CAPSLOCK: case KEY_NUMLOCK: case KEY_SCROLLLOCK:
		return true;
	default:
		return false;
	}
}

static bool PrefixProperties(unsigned int code, AhkInputBackendKind backend,
	bool &passthrough)
{
	passthrough = NativePrefix(code);
	bool found = false;
	for (int i = 0; i < Hotkey::sHotkeyCount; ++i)
	{
		Hotkey *hk = Hotkey::shk[i];
		if (!hk || (!hk->mModifierVK && !hk->mModifierSC)
			|| !LinuxInputBackendHotkeyAssigned(hk, backend)
			|| ComboKeyCode(hk, true) != code)
			continue;
		HotkeyVariant *vp = nullptr;
		if (!ComboVariant(hk, vp)) continue;
		found = true;
		if (hk->mNoSuppress & AT_LEAST_ONE_COMBO_HAS_TILDE)
			passthrough = true;
	}
	return found;
}

static void AppendStandalone(const AhkInputAcceptance &a,
	AhkInputBackendKind backend, unsigned int mods, modLR_type mods_lr,
	int phase, AhkInputComboResult &result)
{
	AhkInputAcceptance view = a;
	view.state.modifiers = mods;
	view.state.modifiers_lr = mods_lr;
	bool phases[2] = {phase != 1, phase != 0}; // down, up
	for (int p = 0; p < 2 && result.match_count < _countof(result.matches); ++p)
	{
		if (!phases[p]) continue;
		AhkInputMatch match;
		bool filtered = false;
		if (FindBestHotkey(view, backend, p == 1, match, filtered))
			result.matches[result.match_count++] = match;
	}
}

bool LinuxInputPipelineProcessCombo(const AhkInputAcceptance &a,
	AhkInputBackendKind backend, bool can_suppress,
	AhkInputComboResult &result, bool dispatch)
{
	memset(&result, 0, sizeof(result));
	result.decision = AhkInputDecision{a.state.acceptance_seq, 0,
		AhkInputDecisionAction::NO_MATCH, AhkInputDecisionReason::NONE,
		can_suppress};
	unsigned int code = a.event.evdev_code;
	if (!code || code >= KEY_CNT || a.duplicate)
		return false;
	SeatState &seat = Seat(a.context);
	bool down = !a.event.is_release;

	if (!down && seat.prefix_down[code])
	{
		bool passthrough = seat.prefix_passthrough[code] != 0;
		if (passthrough)
			AppendStandalone(a, backend, seat.prefix_mods[code],
				seat.prefix_mods_lr[code], 1, result);
		else if (!seat.prefix_used[code])
			AppendStandalone(a, backend, seat.prefix_mods[code],
				seat.prefix_mods_lr[code], -1, result);
		seat.prefix_down[code] = 0;
		seat.prefix_used[code] = 0;
		result.handled = true;
		result.suppress_original = can_suppress && !passthrough;
		result.decision.registration_id = result.match_count
			? result.matches[0].registration_id : 0;
		result.decision.action = result.suppress_original
			? AhkInputDecisionAction::SUPPRESS_ORIGINAL
			: AhkInputDecisionAction::PASS_ORIGINAL;
		result.decision.reason = result.match_count
			? AhkInputDecisionReason::COMBO_STANDALONE
			: AhkInputDecisionReason::COMBO_RELEASE;
	}
	else
	{
		bool prefix_passthrough = false;
		bool is_prefix = PrefixProperties(code, backend, prefix_passthrough);
		if (is_prefix)
		{
			if (down && !a.event.is_repeat)
			{
				seat.prefix_down[code] = 1;
				seat.prefix_used[code] = 0;
				seat.prefix_passthrough[code] = prefix_passthrough ? 1 : 0;
				seat.prefix_mods[code] = a.state.modifiers;
				seat.prefix_mods_lr[code] = a.state.modifiers_lr;
				if (prefix_passthrough)
					AppendStandalone(a, backend, a.state.modifiers,
						a.state.modifiers_lr, 0, result);
			}
			else if (!down)
				seat.prefix_down[code] = 0;
			result.handled = true;
			result.suppress_original = can_suppress && !prefix_passthrough;
			result.decision.registration_id = result.match_count
				? result.matches[0].registration_id : 0;
			result.decision.action = result.suppress_original
				? AhkInputDecisionAction::SUPPRESS_ORIGINAL
				: AhkInputDecisionAction::PASS_ORIGINAL;
			result.decision.reason = AhkInputDecisionReason::COMBO_PREFIX_HELD;
		}
		else
		{
			bool release_suppressed = !down && seat.combo_suppressed[code];
			uint64_t stored_owner = code < KEY_CNT ? seat.combo_owner[code] : 0;
			Hotkey *combo = nullptr;
			HotkeyVariant *variant = nullptr;
			uint64_t combo_id = 0;
			for (int i = 0; i < Hotkey::sHotkeyCount; ++i)
			{
				Hotkey *hk = Hotkey::shk[i];
				if (!hk || (!hk->mModifierVK && !hk->mModifierSC)
					|| !LinuxInputBackendHotkeyAssigned(hk, backend)
					|| ComboKeyCode(hk, false) != code)
					continue;
				unsigned int prefix = ComboKeyCode(hk, true);
				if (!prefix || prefix >= KEY_CNT || !seat.prefix_down[prefix])
					continue;
				unsigned int required = (unsigned)hk->mModifiers
					| GenericFromLR(hk->mModifiersLR);
				if ((a.state.modifiers & required) != required
					|| (a.state.modifiers_lr & hk->mModifiersLR) != hk->mModifiersLR)
					continue; // custom combos accept unrelated extra modifiers.
				HotkeyVariant *vp = nullptr;
				if (!ComboVariant(hk, vp)) continue;
				if (a.event.send_level >= 0
					&& !AhkSyntheticMayTrigger(AhkConsumerKind::HOTKEY,
						AhkSendTransportClass::EVENT, a.event.send_level,
						(int)vp->mInputLevel))
					continue;
				uint64_t id = (uint64_t)i + 1;
				if (!down && stored_owner && id != stored_owner)
					continue;
				if ((bool)hk->mKeyUp != a.event.is_release)
				{
					if (!(down && hk->mKeyUp)) continue;
				}
				combo = hk; variant = vp; combo_id = id;
				seat.prefix_used[prefix] = 1;
				break;
			}
			if (combo && down && combo->mKeyUp)
			{
				bool pass = IsPassthrough(combo, variant);
				seat.combo_owner[code] = combo_id;
				seat.combo_suppressed[code] = can_suppress && !pass ? 1 : 0;
				result.handled = true;
				result.suppress_original = seat.combo_suppressed[code] != 0;
				result.decision.registration_id = combo_id;
				result.decision.action = result.suppress_original
					? AhkInputDecisionAction::SUPPRESS_ORIGINAL
					: AhkInputDecisionAction::PASS_ORIGINAL;
				result.decision.reason = AhkInputDecisionReason::KEYUP_OWNERSHIP;
			}
			else if (combo && (bool)combo->mKeyUp == a.event.is_release)
			{
				bool pass = IsPassthrough(combo, variant);
				result.matches[result.match_count++] = AhkInputMatch{
					combo, variant, combo_id};
				result.handled = true;
				result.suppress_original = release_suppressed
					|| (can_suppress && !pass);
				result.decision.registration_id = combo_id;
				result.decision.action = result.suppress_original
					? AhkInputDecisionAction::TRIGGER_SUPPRESS
					: AhkInputDecisionAction::TRIGGER_PASS;
				result.decision.reason = AhkInputDecisionReason::COMBO_SUFFIX;
				if (down && result.suppress_original)
				{
					seat.combo_owner[code] = combo_id;
					seat.combo_suppressed[code] = 1;
				}
				if (!down) { seat.combo_owner[code] = 0; seat.combo_suppressed[code] = 0; }
			}
			else if (release_suppressed)
			{
				seat.combo_owner[code] = 0; seat.combo_suppressed[code] = 0;
				result.handled = true;
				result.suppress_original = true;
				result.decision.registration_id = stored_owner;
				result.decision.action = AhkInputDecisionAction::SUPPRESS_ORIGINAL;
				result.decision.reason = AhkInputDecisionReason::COMBO_RELEASE;
			}
			else if (down && !a.event.is_repeat)
				for (unsigned int p = 0; p < KEY_CNT; ++p)
					if (seat.prefix_down[p] && p != code) seat.prefix_used[p] = 1;
		}
	}

	if (!result.handled) return false;
	LinuxInputPipelineTraceDecision(a, result.decision);
	if (dispatch)
		for (unsigned int i = 0; i < result.match_count; ++i)
			LinuxInputPipelineDispatch(a, result.matches[i], result.decision);
	LinuxInputPipelineTraceOutcome(a, result.decision,
		result.match_count
			? (dispatch ? "combo_callback_dispatched" : "combo_shadow_matched")
			: (result.suppress_original ? "combo_suppressed" : "combo_passed"));
	return true;
}

void LinuxInputPipelineTraceComboComparison(const AhkInputAcceptance &a,
	const AhkInputComboResult &new_result, int legacy_result)
{
	bool legacy_handled = legacy_result >= 0;
	bool legacy_suppress = legacy_result > 0;
	bool equivalent = new_result.handled == legacy_handled
		&& (!legacy_handled || new_result.suppress_original == legacy_suppress);
	AhkInputDecision d = new_result.decision;
	d.action = equivalent ? AhkInputDecisionAction::PASS_ORIGINAL
		: AhkInputDecisionAction::CONFLICT;
	d.reason = equivalent ? AhkInputDecisionReason::MIRROR_MATCH
		: AhkInputDecisionReason::MIRROR_MISMATCH;
	Trace("combo_mirror", a, &d, nullptr, equivalent ? 1 : 0);
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

void LinuxInputPipelineTraceConsumerDecision(const AhkInputAcceptance &a,
	const AhkInputConsumerDecision &d)
{
	TraceConsumer("consumer_decision", a, d, nullptr);
}

void LinuxInputPipelineTraceConsumerOutcome(const AhkInputAcceptance &a,
	const AhkInputConsumerDecision &d, const char *outcome)
{
	TraceConsumer("consumer_outcome", a, d, outcome ? outcome : "unknown");
}

void LinuxInputPipelineTraceBrokerDecision(uint64_t authority_generation,
	uint64_t source_event_seq, uint64_t source_txn, uint32_t code,
	uint8_t action, uint8_t reason, uint64_t winner,
	uint64_t replacement_txn, int priority)
{
	const char *path = getenv("AHK_INPUT_PIPELINE_TRACE");
	if (!path || !*path) return;
	const char *action_name = action == INPUTD_V2_DECISION_SUPPRESS ? "suppress"
		: (action == INPUTD_V2_DECISION_REMAP ? "remap"
			: (action == INPUTD_V2_DECISION_REPLACEMENT_FAILED
				? "replacement_failed" : "replay"));
	FILE *f = fopen(path, "a");
	if (!f) return;
	fprintf(f,
		"{\"schema\":1,\"stage\":\"broker_decision\","
		"\"acceptance_seq\":0,\"backend_seq\":%llu,"
		"\"authority_generation\":%llu,\"domain\":\"inputd\","
		"\"source_transaction_id\":%llu,\"evdev_code\":%u,"
		"\"broker_action\":\"%s\",\"broker_action_id\":%u,"
		"\"broker_reason_id\":%u,\"winner_registration_id\":%llu,"
		"\"replacement_transaction_id\":%llu,\"priority\":%d}\n",
		(unsigned long long)source_event_seq,
		(unsigned long long)authority_generation,
		(unsigned long long)source_txn, code, action_name, (unsigned)action,
		(unsigned)reason, (unsigned long long)winner,
		(unsigned long long)replacement_txn, priority);
	fclose(f);
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
