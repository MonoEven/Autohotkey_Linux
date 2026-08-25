# SoundGetName

- Linux status: `IMPL` (P2)
- Example kind: `verified`
- Environment profile: `host-tools`
- Verified source: [tests/doccheck/assert_sound_etc.ahk:32](../../tests/doccheck/assert_sound_etc.ahk#L32)
- Profile command: `bash tests/doccheck/run_check.sh --xvfb "$BIN"`
- Linux adaptation: Uses pactl/amixer; requires the external tool; limited to master devices

Retrieves the name of a sound device or component.

## Syntax

````text
Name := SoundGetName(Component, Device)
````

## Linux-verified example excerpt

This excerpt is executed by the profile command above; surrounding setup and assertions remain in the linked source.

````ahk
ExpectOSError("sgm", () => SoundGetMute())
ExpectOSError("sgv", () => SoundGetVolume())
ExpectOSError("sgn", () => SoundGetName())
ExpectOSError("ssm", () => SoundSetMute(1))
ExpectOSError("ssv", () => SoundSetVolume(50))
````

## Upstream reference example

Source: [docs-v2/docs/lib/SoundGetName.htm](../../docs-v2/docs/lib/SoundGetName.htm)

````ahk
default_device := SoundGetName()
MsgBox "The default playback device is " default_device
````
