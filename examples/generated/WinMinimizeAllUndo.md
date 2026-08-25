# WinMinimizeAllUndo

- Linux status: `IMPL` (P1)
- Example kind: `verified`
- Environment profile: `x11`
- Verified source: [tests/doccheck/assert_win.ahk:174](../../tests/doccheck/assert_win.ahk#L174)
- Profile command: `bash tests/doccheck/run_check.sh --xvfb "$BIN"`

## Linux-verified example excerpt

This excerpt is executed by the profile command above; surrounding setup and assertions remain in the linked source.

````ahk
Sleep(50)
Log("minall=" (WinGetMinMax("DocCheck Alpha") = -1 ? 1 : 0))
WinMinimizeAllUndo()
Sleep(50)
Log("minall_undo=" (WinGetMinMax("DocCheck Alpha") = 0 ? 1 : 0))
````
