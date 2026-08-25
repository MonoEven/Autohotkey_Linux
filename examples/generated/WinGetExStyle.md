# WinGetExStyle

- Linux status: `IMPL` (P3)
- Example kind: `verified`
- Environment profile: `x11`
- Verified source: [tests/doccheck/assert_win.ahk:106](../../tests/doccheck/assert_win.ahk#L106)
- Profile command: `bash tests/doccheck/run_check.sh --xvfb "$BIN"`
- Linux adaptation: Window style is a virtual shadow + EWMH; exact for self-set windows only

## Linux-verified example excerpt

This excerpt is executed by the profile command above; surrounding setup and assertions remain in the linked source.

````ahk
; --- WinSetAlwaysOnTop + WinGetExStyle (WS_EX_TOPMOST = 0x8). ---
WinSetAlwaysOnTop(1, "DocCheck Alpha")
Log("topmost_on=" ((WinGetExStyle("DocCheck Alpha") & 0x8) ? 1 : 0))
WinSetAlwaysOnTop(0, "DocCheck Alpha")
Log("topmost_off=" ((WinGetExStyle("DocCheck Alpha") & 0x8) ? 0 : 1))
````
