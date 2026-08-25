# WinWaitActive

- Linux status: `IMPL` (P1)
- Example kind: `verified`
- Environment profile: `x11`
- Verified source: [tests/doccheck/assert_win.ahk:156](../../tests/doccheck/assert_win.ahk#L156)
- Profile command: `bash tests/doccheck/run_check.sh --xvfb "$BIN"`

Waits until the specified window is active or not active.

## Syntax

````text
HWND := WinWaitActive(WinTitle, WinText, Timeout, ExcludeTitle, ExcludeText) Boolean := WinWaitNotActive(WinTitle, WinText, Timeout, ExcludeTitle, ExcludeText)
````

## Linux-verified example excerpt

This excerpt is executed by the profile command above; surrounding setup and assertions remain in the linked source.

````ahk
Log("winactive=" (WinActive("DocCheck Alpha") != "" ? 1 : 0))
Log("winnotactive=" (WinActive("DocCheck Beta") = "" ? 1 : 0))
Log("winwaitactive=" (WinWaitActive("DocCheck Alpha",, 3) != 0 ? 1 : 0))
Log("winwaitnotactive=" (WinWaitActive("DocCheck Beta",, 0.3) = 0 ? 1 : 0))

; --- WinClose + WinWaitClose. ---
````

## Upstream reference example

Source: [docs-v2/docs/lib/WinWaitActive.htm](../../docs-v2/docs/lib/WinWaitActive.htm)

````ahk
Run "notepad.exe"
if WinWaitActive("Untitled - Notepad", , 2)
    WinMinimize ; Use the window found by WinWaitActive.
else
    MsgBox "WinWaitActive timed out."
````
