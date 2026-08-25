# WinKill

- Linux status: `IMPL` (P1)
- Example kind: `verified`
- Environment profile: `x11`
- Verified source: [tests/doccheck/assert_win.ahk:167](../../tests/doccheck/assert_win.ahk#L167)
- Profile command: `bash tests/doccheck/run_check.sh --xvfb "$BIN"`

Forces the specified window to close.

## Syntax

````text
WinKill WinTitle, WinText, SecondsToWait, ExcludeTitle, ExcludeText
````

## Linux-verified example excerpt

This excerpt is executed by the profile command above; surrounding setup and assertions remain in the linked source.

````ahk
Run('out/xwin_helper -title "Kill Me Now" -class KillClass -x 30 -y 30 -w 150 -h 120')
WinWait("Kill Me Now",, 5)
WinKill("Kill Me Now")
Log("winkill=" (WinWaitClose("Kill Me Now",, 5) = 1 ? 1 : 0))

; --- WinMinimizeAll / WinMinimizeAllUndo. ---
````

## Upstream reference example

Source: [docs-v2/docs/lib/WinKill.htm](../../docs-v2/docs/lib/WinKill.htm)

````ahk
if WinExist("Untitled - Notepad")
    WinKill ; Use the window found by WinExist.
else
    WinKill "Calculator"
````
