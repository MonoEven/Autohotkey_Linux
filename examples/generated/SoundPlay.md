# SoundPlay

- Linux status: `IMPL` (P2)
- Example kind: `verified`
- Environment profile: `headless`
- Verified source: [tests/doccheck/assert_sys.ahk:344](../../tests/doccheck/assert_sys.ahk#L344)
- Profile command: `bash tests/doccheck/run_check.sh "$BIN"`
- Linux adaptation: Uses paplay/aplay; requires the external tool and a sound server

Plays a sound, video, or other supported file type.

## Syntax

````text
SoundPlay Filename , Wait
````

## Linux-verified example excerpt

This excerpt is executed by the profile command above; surrounding setup and assertions remain in the linked source.

````ahk
    MsgBox "DriveRetract_err=OSError"
try
    SoundPlay("/nonexistent.wav")
catch OSError
    MsgBox "SoundPlay_err=OSError"
````

## Upstream reference example

Source: [docs-v2/docs/lib/SoundPlay.htm](../../docs-v2/docs/lib/SoundPlay.htm)

````ahk
SoundPlay A_WinDir "\Media\ding.wav"
````
