# WinGetClass

- Linux status: `IMPL` (P1)
- Example kind: `verified`
- Environment profile: `x11`
- Verified source: [tests/doccheck/assert_win.ahk:50](../../tests/doccheck/assert_win.ahk#L50)
- Profile command: `bash tests/doccheck/run_check.sh --xvfb "$BIN"`

Retrieves the specified window's class name.

## Syntax

````text
ClassName := WinGetClass(WinTitle, WinText, ExcludeTitle, ExcludeText)
````

## Linux-verified example excerpt

This excerpt is executed by the profile command above; surrounding setup and assertions remain in the linked source.

````ahk
; --- WinGetTitle / WinGetClass / WinGetPID / WinGetProcessName / WinGetProcessPath. ---
Log("gettitle=" (WinGetTitle("DocCheck Alpha") = "DocCheck Alpha"))
Log("getclass=" (WinGetClass("DocCheck Alpha") = "DocCheckClass"))
Log("getpid_gt0=" (WinGetPID("DocCheck Alpha") > 0 ? 1 : 0))
Log("getprocname=" (WinGetProcessName("DocCheck Alpha") = "xwin_helper"))
Log("getprocpath=" (SubStr(WinGetProcessPath("DocCheck Alpha"), -11) = "xwin_helper"))
````

## Upstream reference example

Source: [docs-v2/docs/lib/WinGetClass.htm](../../docs-v2/docs/lib/WinGetClass.htm)

````ahk
MsgBox "The active window's class is " WinGetClass("A")
````
