# WinGetProcessName

- Linux status: `IMPL` (P1)
- Example kind: `verified`
- Environment profile: `x11`
- Verified source: [tests/doccheck/assert_win.ahk:52](../../tests/doccheck/assert_win.ahk#L52)
- Profile command: `bash tests/doccheck/run_check.sh --xvfb "$BIN"`

Returns the name of the process that owns the specified window.

## Syntax

````text
ProcessName := WinGetProcessName(WinTitle, WinText, ExcludeTitle, ExcludeText)
````

## Linux-verified example excerpt

This excerpt is executed by the profile command above; surrounding setup and assertions remain in the linked source.

````ahk
Log("getclass=" (WinGetClass("DocCheck Alpha") = "DocCheckClass"))
Log("getpid_gt0=" (WinGetPID("DocCheck Alpha") > 0 ? 1 : 0))
Log("getprocname=" (WinGetProcessName("DocCheck Alpha") = "xwin_helper"))
Log("getprocpath=" (SubStr(WinGetProcessPath("DocCheck Alpha"), -11) = "xwin_helper"))
Log("gettitle_after_rename=" (WinGetTitle("DocCheck Beta") = "DocCheck Beta"))
try {
````

## Upstream reference example

Source: [docs-v2/docs/lib/WinGetProcessName.htm](../../docs-v2/docs/lib/WinGetProcessName.htm)

The upstream page has no standalone code block; use the Linux-verified excerpt above.
