# WinGetPID

- Linux status: `IMPL` (P1)
- Example kind: `verified`
- Environment profile: `x11`
- Verified source: [tests/doccheck/assert_win.ahk:44](../../tests/doccheck/assert_win.ahk#L44)
- Profile command: `bash tests/doccheck/run_check.sh --xvfb "$BIN"`

Returns the Process ID (PID) of the specified window.

## Syntax

````text
PID := WinGetPID(WinTitle, WinText, ExcludeTitle, ExcludeText)
````

## Linux-verified example excerpt

This excerpt is executed by the profile command above; surrounding setup and assertions remain in the linked source.

````ahk
Log("exe_count=" (WinGetCount("DocCheck ahk_exe xwin_helper") = 3))
Log("exe_missing=" (WinGetCount("ahk_exe no_such_proc_xyz") = 0))
wpid := WinGetPID("DocCheck Alpha")
Log("pid_criteria=" (WinExist("ahk_pid " wpid) != "" ? 1 : 0))
Log("id_criteria=" (WinGetTitle("ahk_id " id_alpha) = "DocCheck Alpha"))
````

## Upstream reference example

Source: [docs-v2/docs/lib/WinGetPID.htm](../../docs-v2/docs/lib/WinGetPID.htm)

The upstream page has no standalone code block; use the Linux-verified excerpt above.
