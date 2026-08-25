# SoundSetMute

- Linux status: `IMPL` (P2)
- Example kind: `verified`
- Environment profile: `host-tools`
- Verified source: [tests/doccheck/assert_sound_etc.ahk:33](../../tests/doccheck/assert_sound_etc.ahk#L33)
- Profile command: `bash tests/doccheck/run_check.sh --xvfb "$BIN"`
- Linux adaptation: Uses pactl/amixer; requires the external tool; limited to master devices

Changes a mute setting of a sound device.

## Syntax

````text
SoundSetMute NewSetting , Component, Device
````

## Linux-verified example excerpt

This excerpt is executed by the profile command above; surrounding setup and assertions remain in the linked source.

````ahk
ExpectOSError("sgv", () => SoundGetVolume())
ExpectOSError("sgn", () => SoundGetName())
ExpectOSError("ssm", () => SoundSetMute(1))
ExpectOSError("ssv", () => SoundSetVolume(50))

; --- CaretGetPos without a focused GTK window -------------------------------
````

## Upstream reference example

Source: [docs-v2/docs/lib/SoundSetMute.htm](../../docs-v2/docs/lib/SoundSetMute.htm)

````ahk
SoundSetMute true
````
