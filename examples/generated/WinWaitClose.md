# WinWaitClose

- Linux status: `IMPL` (P1)
- Example kind: `verified`
- Environment profile: `x11`
- Verified source: [tests/doccheck/assert_win.ahk:161](../../tests/doccheck/assert_win.ahk#L161)
- Profile command: `bash tests/doccheck/run_check.sh --xvfb "$BIN"`

Waits until no matching windows can be found.

## Syntax

````text
Boolean := WinWaitClose(WinTitle, WinText, Timeout, ExcludeTitle, ExcludeText)
````

## Linux-verified example excerpt

This excerpt is executed by the profile command above; surrounding setup and assertions remain in the linked source.

````ahk
; --- WinClose + WinWaitClose. ---
WinClose("DocCheck Beta")
Log("winwaitclose=" (WinWaitClose("DocCheck Beta",, 5) = 1 ? 1 : 0))
Log("close_gone=" (WinExist("DocCheck Beta") = "" ? 1 : 0))

; --- WinKill. ---
````

## Upstream reference example

Source: [docs-v2/docs/lib/WinWaitClose.htm](../../docs-v2/docs/lib/WinWaitClose.htm)

````ahk
Run "notepad.exe"
WinWait "Untitled - Notepad"
WinWaitClose ; Use the window found by WinWait.
MsgBox "Notepad is now closed."
````
