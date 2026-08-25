# WinSetAlwaysOnTop

- Linux status: `IMPL` (P3)
- Example kind: `verified`
- Environment profile: `x11`
- Verified source: [tests/doccheck/assert_win.ahk:105](../../tests/doccheck/assert_win.ahk#L105)
- Profile command: `bash tests/doccheck/run_check.sh --xvfb "$BIN"`
- Linux adaptation: Virtual shadow + _NET_WM_STATE_ABOVE sent to the WM

Makes the specified window stay on top of all other windows (except other always-on-top windows).

## Syntax

````text
WinSetAlwaysOnTop NewSetting, WinTitle, WinText, ExcludeTitle, ExcludeText
````

## Linux-verified example excerpt

This excerpt is executed by the profile command above; surrounding setup and assertions remain in the linked source.

````ahk

; --- WinSetAlwaysOnTop + WinGetExStyle (WS_EX_TOPMOST = 0x8). ---
WinSetAlwaysOnTop(1, "DocCheck Alpha")
Log("topmost_on=" ((WinGetExStyle("DocCheck Alpha") & 0x8) ? 1 : 0))
WinSetAlwaysOnTop(0, "DocCheck Alpha")
Log("topmost_off=" ((WinGetExStyle("DocCheck Alpha") & 0x8) ? 0 : 1))
````

## Upstream reference example

Source: [docs-v2/docs/lib/WinSetAlwaysOnTop.htm](../../docs-v2/docs/lib/WinSetAlwaysOnTop.htm)

````ahk
WinSetAlwaysOnTop -1, "Calculator"
````
