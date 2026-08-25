# SoundBeep

- Linux status: `IMPL` (P2)
- Example kind: `verified`
- Environment profile: `x11`
- Verified source: [tests/doccheck/assert_misc_cov.ahk:260](../../tests/doccheck/assert_misc_cov.ahk#L260)
- Profile command: `bash tests/doccheck/run_check.sh --xvfb "$BIN"`
- Linux adaptation: Terminal/X11 bell; Frequency is ignored (no pitch control)

Emits a tone from the PC speaker.

## Syntax

````text
SoundBeep Frequency, Duration
````

## Linux-verified example excerpt

This excerpt is executed by the profile command above; surrounding setup and assertions remain in the linked source.

````ahk

; --- SoundBeep (bell; returns 0) -----------------------------------------
Check("soundbeep", () => SoundBeep(523, 5))

; --- Gui control instances (v2 has no global "GuiControl" identifier; the
; control classes are Gui.Control/Gui.Text, covered here and in assert_gui) -
````

## Upstream reference example

Source: [docs-v2/docs/lib/SoundBeep.htm](../../docs-v2/docs/lib/SoundBeep.htm)

````ahk
SoundBeep
````
