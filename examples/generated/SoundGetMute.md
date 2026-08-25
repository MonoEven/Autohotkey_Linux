# SoundGetMute

- Linux status: `IMPL` (P2)
- Example kind: `verified`
- Environment profile: `host-tools`
- Verified source: [tests/doccheck/assert_sound_etc.ahk:30](../../tests/doccheck/assert_sound_etc.ahk#L30)
- Profile command: `bash tests/doccheck/run_check.sh --xvfb "$BIN"`
- Linux adaptation: Uses pactl/amixer; requires the external tool; limited to master devices

Retrieves a mute setting of a sound device.

## Syntax

````text
Setting := SoundGetMute(Component, Device)
````

## Linux-verified example excerpt

This excerpt is executed by the profile command above; surrounding setup and assertions remain in the linked source.

````ahk

; --- SoundGet*/Set* without a mixer tool -> OSError -------------------------
ExpectOSError("sgm", () => SoundGetMute())
ExpectOSError("sgv", () => SoundGetVolume())
ExpectOSError("sgn", () => SoundGetName())
ExpectOSError("ssm", () => SoundSetMute(1))
````

## Upstream reference example

Source: [docs-v2/docs/lib/SoundGetMute.htm](../../docs-v2/docs/lib/SoundGetMute.htm)

````ahk
master_mute := SoundGetMute()
if master_mute
    MsgBox "The default playback device is muted."
else
    MsgBox "The default playback device is not muted."
````
