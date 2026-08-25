# WinActive

- Linux status: `IMPL` (P1)
- Example kind: `verified`
- Environment profile: `x11`
- Verified source: [tests/doccheck/assert_win.ahk:154](../../tests/doccheck/assert_win.ahk#L154)
- Profile command: `bash tests/doccheck/run_check.sh --xvfb "$BIN"`

Checks if the specified window is active and returns its unique ID (HWND).

## Syntax

````text
HWND := WinActive(WinTitle, WinText, ExcludeTitle, ExcludeText)
````

## Linux-verified example excerpt

This excerpt is executed by the profile command above; surrounding setup and assertions remain in the linked source.

````ahk
WinActivate("DocCheck Alpha")
Sleep(100)
Log("winactive=" (WinActive("DocCheck Alpha") != "" ? 1 : 0))
Log("winnotactive=" (WinActive("DocCheck Beta") = "" ? 1 : 0))
Log("winwaitactive=" (WinWaitActive("DocCheck Alpha",, 3) != 0 ? 1 : 0))
Log("winwaitnotactive=" (WinWaitActive("DocCheck Beta",, 0.3) = 0 ? 1 : 0))
````

## Upstream reference example

Source: [docs-v2/docs/lib/WinActive.htm](../../docs-v2/docs/lib/WinActive.htm)

````ahk
if WinActive("ahk_class Notepad") or WinActive("ahk_class" ClassName)
    WinClose ; Use the window found by WinActive.
````
