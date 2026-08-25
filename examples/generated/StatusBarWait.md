# StatusBarWait

- Linux status: `IMPL` (P1)
- Example kind: `verified`
- Environment profile: `x11`
- Verified source: [tests/doccheck/assert_monitor.ahk:71](../../tests/doccheck/assert_monitor.ahk#L71)
- Profile command: `bash tests/doccheck/run_check.sh --xvfb "$BIN"`

Waits until a window's status bar contains the specified string.

## Syntax

````text
Boolean := StatusBarWait(BarText, Timeout, Part#, WinTitle, WinText, Interval, ExcludeTitle, ExcludeText)
````

## Linux-verified example excerpt

This excerpt is executed by the profile command above; surrounding setup and assertions remain in the linked source.

````ahk
ControlSetText("Ready", "msctls_statusbar321", "MonWin")
Log("sb_set=" (StatusBarGetText(1, "MonWin") = "Ready" ? 1 : 0))
Log("sb_wait=" (StatusBarWait("Ready", 2, 1, "MonWin") = 1 ? 1 : 0))
Log("sb_wait_timeout=" (StatusBarWait("Nope", 0.3, 1, "MonWin") = 0 ? 1 : 0))
try
    StatusBarGetText(1, "NoSuchWindowPlease")
````

## Upstream reference example

Source: [docs-v2/docs/lib/StatusBarWait.htm](../../docs-v2/docs/lib/StatusBarWait.htm)

````ahk
if WinExist("Search Results") ; Sets the Last Found window to simplify the below.
{
    WinActivate
    Send "{tab 2}!o*.txt{enter}"  ; In the Search window, enter the pattern to search for.
    Sleep 400  ; Give the status bar time to change to "Searching".
    if StatusBarWait("found", 3
````
