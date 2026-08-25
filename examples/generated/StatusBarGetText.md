# StatusBarGetText

- Linux status: `IMPL` (P1)
- Example kind: `verified`
- Environment profile: `x11`
- Verified source: [tests/doccheck/assert_monitor.ahk:67](../../tests/doccheck/assert_monitor.ahk#L67)
- Profile command: `bash tests/doccheck/run_check.sh --xvfb "$BIN"`

Retrieves the text from a standard status bar control.

## Syntax

````text
Text := StatusBarGetText(Part#, WinTitle, WinText, ExcludeTitle, ExcludeText)
````

## Linux-verified example excerpt

This excerpt is executed by the profile command above; surrounding setup and assertions remain in the linked source.

````ahk
; msctls_statusbar32 common control; Part 1 is the text; Part > 1 = "" since
; X11 bars have a single part; wait matches per TitleMatchMode). ---
Log("sb_text=" (StatusBarGetText(1, "MonWin") = "StatusBar1" ? 1 : 0))
Log("sb_part2=" (StatusBarGetText(2, "MonWin") = "" ? 1 : 0))
ControlSetText("Ready", "msctls_statusbar321", "MonWin")
Log("sb_set=" (StatusBarGetText(1, "MonWin") = "Ready" ? 1 : 0))
````

## Upstream reference example

Source: [docs-v2/docs/lib/StatusBarGetText.htm](../../docs-v2/docs/lib/StatusBarGetText.htm)

````ahk
RetrievedText := StatusBarGetText(1, "Search Results")
if InStr(RetrievedText, "found")
    MsgBox "Search results have been found."
````
