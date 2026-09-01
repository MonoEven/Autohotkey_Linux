/* input_semantics.h -- unified synthetic-input level policy for Linux.
 *
 * audit check0901 P0-2 / check_detail0901 §2: every consumer (hotkey,
 * hotstring, InputHook) shares ONE classification entry point, but each
 * consumer keeps its own Windows-golden policy:
 *
 *   - Hotkey/Hotstring: a synthetic event triggers only when
 *     send_level > input_level (official HotInputLevelAllowsFiring:
 *     "InputLevelFromInfo(extra) <= inputLevel -> ignore").  Physical or
 *     non-AHK input (send_level < 0) is never filtered by level.
 *   - InputHook: SendEvent-class synthetic input is collected when
 *     send_level >= MinSendLevel; SendInput and SendPlay are always ignored
 *     (Windows unloads its own hook during SendInput, and SendPlay uses the
 *     journal which never reaches hooks).  Physical/non-AHK input is never
 *     filtered by MinSendLevel.
 *
 * A transport class only classifies the event for policy; it is not a fourth
 * public Send mode (check_detail0901 §6.1).
 */
#ifndef AHK_LINUX_INPUT_SEMANTICS_H
#define AHK_LINUX_INPUT_SEMANTICS_H

enum class AhkConsumerKind
{
	HOTKEY,
	HOTSTRING,
	INPUTHOOK
};

enum class AhkSendTransportClass
{
	EVENT, // SendEvent, or {Text}/SendText under Event mode: level-gated.
	INPUT, // SendInput (incl. default Send under Input mode): hook-unloaded.
	PLAY   // SendPlay: journal delivery; never reaches AHK hooks.
};

// Returns true when the event may reach the consumer.  When aOutReason is
// non-null it receives a static diagnostic string explaining the decision.
bool AhkSyntheticMayTrigger(AhkConsumerKind aConsumer
	, AhkSendTransportClass aTransport
	, int aSendLevel
	, int aInputLevel
	, const char **aOutReason = nullptr);

#endif /* AHK_LINUX_INPUT_SEMANTICS_H */
