# SoundGetVolume

- Linux status: `IMPL` (P2)
- Example kind: `verified`
- Environment profile: `host-tools`
- Verified source: [tests/doccheck/assert_sound_etc.ahk:31](../../tests/doccheck/assert_sound_etc.ahk#L31)
- Profile command: `bash tests/doccheck/run_check.sh --xvfb "$BIN"`
- Linux adaptation: Uses pactl/amixer; requires the external tool; limited to master devices

Retrieves a volume setting of a sound device.

## Syntax

````text
Setting := SoundGetVolume(Component, Device)
````

## Linux-verified example excerpt

This excerpt is executed by the profile command above; surrounding setup and assertions remain in the linked source.

````ahk
; --- SoundGet*/Set* without a mixer tool -> OSError -------------------------
ExpectOSError("sgm", () => SoundGetMute())
ExpectOSError("sgv", () => SoundGetVolume())
ExpectOSError("sgn", () => SoundGetName())
ExpectOSError("ssm", () => SoundSetMute(1))
ExpectOSError("ssv", () => SoundSetVolume(50))
````

## Upstream reference example

Source: [docs-v2/docs/lib/SoundGetVolume.htm](../../docs-v2/docs/lib/SoundGetVolume.htm)

````ahk
master_volume := SoundGetVolume()
MsgBox "Master volume is " master_volume " percent."
````
