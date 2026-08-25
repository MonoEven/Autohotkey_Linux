# WinGetText

- Linux status: `IMPL` (P1)
- Example kind: `verified`
- Environment profile: `x11`
- Verified source: [tests/doccheck/assert_win.ahk:79](../../tests/doccheck/assert_win.ahk#L79)
- Profile command: `bash tests/doccheck/run_check.sh --xvfb "$BIN"`

Retrieves the text from the specified window.

## Syntax

````text
Text := WinGetText(WinTitle, WinText, ExcludeTitle, ExcludeText)
````

## Linux-verified example excerpt

This excerpt is executed by the profile command above; surrounding setup and assertions remain in the linked source.

````ahk

; --- WinGetText / WinGetControls / WinGetControlsHwnd (no X11 controls). ---
Log("gettext=" (WinGetText("DocCheck Alpha") = ""))
Log("getcontrols=" (WinGetControls("DocCheck Alpha").Length = 0))
Log("getcontrolshwnd=" (WinGetControlsHwnd("DocCheck Alpha").Length = 0))
````

## Upstream reference example

Source: [docs-v2/docs/lib/WinGetText.htm](../../docs-v2/docs/lib/WinGetText.htm)

````ahk
Run "calc.exe"
WinWait "Calculator"
MsgBox "The text is:`n" WinGetText() ; Use the window found by WinWait.
````
