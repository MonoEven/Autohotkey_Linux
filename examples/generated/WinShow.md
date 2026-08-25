# WinShow

- Linux status: `IMPL` (P1)
- Example kind: `verified`
- Environment profile: `x11`
- Verified source: [tests/doccheck/assert_win.ahk:146](../../tests/doccheck/assert_win.ahk#L146)
- Profile command: `bash tests/doccheck/run_check.sh --xvfb "$BIN"`

Unhides the specified window.

## Syntax

````text
WinShow WinTitle, WinText, ExcludeTitle, ExcludeText
````

## Linux-verified example excerpt

This excerpt is executed by the profile command above; surrounding setup and assertions remain in the linked source.

````ahk
; Docs: a hidden window is only detectable with DetectHiddenWindows On, so
; WinShow on the hidden window requires it too.
WinShow("DocCheck Beta")
Sleep(100)
DetectHiddenWindows(0)
Log("show_again=" (WinExist("DocCheck Beta") != "" ? 1 : 0))
````

## Upstream reference example

Source: [docs-v2/docs/lib/WinShow.htm](../../docs-v2/docs/lib/WinShow.htm)

````ahk
Run "notepad.exe"
WinWait "Untitled - Notepad"
Sleep 500
WinHide ; Use the window found by WinWait.
Sleep 1000
WinShow ; Use the window found by WinWait.
````
