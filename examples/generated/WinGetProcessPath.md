# WinGetProcessPath

- Linux status: `IMPL` (P1)
- Example kind: `verified`
- Environment profile: `x11`
- Verified source: [tests/doccheck/assert_win.ahk:53](../../tests/doccheck/assert_win.ahk#L53)
- Profile command: `bash tests/doccheck/run_check.sh --xvfb "$BIN"`

Returns the full path and name of the process that owns the specified window.

## Syntax

````text
ProcessPath := WinGetProcessPath(WinTitle, WinText, ExcludeTitle, ExcludeText)
````

## Linux-verified example excerpt

This excerpt is executed by the profile command above; surrounding setup and assertions remain in the linked source.

````ahk
Log("getpid_gt0=" (WinGetPID("DocCheck Alpha") > 0 ? 1 : 0))
Log("getprocname=" (WinGetProcessName("DocCheck Alpha") = "xwin_helper"))
Log("getprocpath=" (SubStr(WinGetProcessPath("DocCheck Alpha"), -11) = "xwin_helper"))
Log("gettitle_after_rename=" (WinGetTitle("DocCheck Beta") = "DocCheck Beta"))
try {
    WinGetTitle("No Such Window Ever")
````

## Upstream reference example

Source: [docs-v2/docs/lib/WinGetProcessPath.htm](../../docs-v2/docs/lib/WinGetProcessPath.htm)

The upstream page has no standalone code block; use the Linux-verified excerpt above.
