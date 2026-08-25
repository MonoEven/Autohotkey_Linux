# WinClose

- Linux status: `IMPL` (P1)
- Example kind: `verified`
- Environment profile: `x11`
- Verified source: [tests/doccheck/assert_win.ahk:160](../../tests/doccheck/assert_win.ahk#L160)
- Profile command: `bash tests/doccheck/run_check.sh --xvfb "$BIN"`

Closes the specified  window.

## Syntax

````text
WinClose WinTitle, WinText, SecondsToWait, ExcludeTitle, ExcludeText
````

## Linux-verified example excerpt

This excerpt is executed by the profile command above; surrounding setup and assertions remain in the linked source.

````ahk

; --- WinClose + WinWaitClose. ---
WinClose("DocCheck Beta")
Log("winwaitclose=" (WinWaitClose("DocCheck Beta",, 5) = 1 ? 1 : 0))
Log("close_gone=" (WinExist("DocCheck Beta") = "" ? 1 : 0))
````

## Upstream reference example

Source: [docs-v2/docs/lib/WinClose.htm](../../docs-v2/docs/lib/WinClose.htm)

````ahk
if WinExist("Untitled - Notepad")
    WinClose ; Use the window found by WinExist.
else
    WinClose "Calculator"
````
