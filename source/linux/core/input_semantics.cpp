/* input_semantics.cpp -- see input_semantics.h. */
#include "input_semantics.h"

bool AhkSyntheticMayTrigger(AhkConsumerKind aConsumer
	, AhkSendTransportClass aTransport
	, int aSendLevel
	, int aInputLevel
	, const char **aOutReason)
{
	if (aOutReason)
		*aOutReason = "pass";
	// Physical or non-AHK input is never filtered by level policy.
	if (aSendLevel < 0)
		return true;
	switch (aConsumer)
	{
	case AhkConsumerKind::HOTKEY:
	case AhkConsumerKind::HOTSTRING:
		// Windows rule (hotkey.cpp::HotInputLevelAllowsFiring): fire only
		// when send_level > input_level; equal levels do NOT fire.
		if (aSendLevel <= aInputLevel)
		{
			if (aOutReason)
				*aOutReason = "synthetic send_level<=input_level";
			return false;
		}
		return true;
	case AhkConsumerKind::INPUTHOOK:
		// SendInput unloads the script's own hook; SendPlay never reaches
		// hooks.  Only SendEvent-class synthetic input is collected.
		if (aTransport == AhkSendTransportClass::INPUT
			|| aTransport == AhkSendTransportClass::PLAY)
		{
			if (aOutReason)
				*aOutReason = "sendinput/sendplay never reach InputHook";
			return false;
		}
		if (aSendLevel < aInputLevel)
		{
			if (aOutReason)
				*aOutReason = "synthetic send_level<min_send_level";
			return false;
		}
		return true;
	}
	return true;
}
