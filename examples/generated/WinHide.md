# WinHide

- Linux status: `IMPL` (P1)
- Example kind: `verified`
- Environment profile: `x11`
- Verified source: [tests/doccheck/assert_win.ahk:140](../../tests/doccheck/assert_win.ahk#L140)
- Profile command: `bash tests/doccheck/run_check.sh --xvfb "$BIN"`

Hides the specified window.

## Syntax

````text
WinHide WinTitle, WinText, ExcludeTitle, ExcludeText
````

## Linux-verified example excerpt

This excerpt is executed by the profile command above; surrounding setup and assertions remain in the linked source.

````ahk

; --- WinHide / WinShow + DetectHiddenWindows. ---
WinHide("DocCheck Beta")
Log("hide_notexist=" (WinExist("DocCheck Beta") = "" ? 1 : 0))
DetectHiddenWindows(1)
Log("hide_dhw=" (WinExist("DocCheck Beta") != "" ? 1 : 0))
````

## Upstream reference example

Source: [docs-v2/docs/lib/WinHide.htm](../../docs-v2/docs/lib/WinHide.htm)

````ahk
Run "notepad.exe"
WinWait "Untitled - Notepad"
Sleep 500
WinHide ; Use the window found by WinWait.
Sleep 1000
WinShow ; Use the window found by WinWait.
````
