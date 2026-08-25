# WinSetExStyle

- Linux status: `IMPL` (P3)
- Example kind: `verified`
- Environment profile: `x11`
- Verified source: [tests/doccheck/assert_win.ahk:136](../../tests/doccheck/assert_win.ahk#L136)
- Profile command: `bash tests/doccheck/run_check.sh --xvfb "$BIN"`
- Linux adaptation: Window style is a virtual shadow + EWMH; exact for self-set windows only

## Linux-verified example excerpt

This excerpt is executed by the profile command above; surrounding setup and assertions remain in the linked source.

````ahk
WinSetStyle("0x1234", "DocCheck Alpha")
Log("style_set=" (WinGetStyle("DocCheck Alpha") = 0x1234 ? 1 : 0))
WinSetExStyle("+0x100", "DocCheck Alpha")
Log("exstyle_add=" (WinGetExStyle("DocCheck Alpha") & 0x100 ? 1 : 0))

; --- WinHide / WinShow + DetectHiddenWindows. ---
````
