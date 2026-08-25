# SoundSetVolume

- Linux status: `IMPL` (P2)
- Example kind: `verified`
- Environment profile: `host-tools`
- Verified source: [tests/doccheck/assert_sound_etc.ahk:34](../../tests/doccheck/assert_sound_etc.ahk#L34)
- Profile command: `bash tests/doccheck/run_check.sh --xvfb "$BIN"`
- Linux adaptation: Uses pactl/amixer; requires the external tool; limited to master devices

Changes a volume setting of a sound device.

## Syntax

````text
SoundSetVolume NewSetting , Component, Device
````

## Linux-verified example excerpt

This excerpt is executed by the profile command above; surrounding setup and assertions remain in the linked source.

````ahk
ExpectOSError("sgn", () => SoundGetName())
ExpectOSError("ssm", () => SoundSetMute(1))
ExpectOSError("ssv", () => SoundSetVolume(50))

; --- CaretGetPos without a focused GTK window -------------------------------
x := y := "?"
````

## Upstream reference example

Source: [docs-v2/docs/lib/SoundSetVolume.htm](../../docs-v2/docs/lib/SoundSetVolume.htm)

````ahk
SoundSetVolume "50"
````
