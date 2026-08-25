# WinMoveTop

- Linux status: `IMPL` (P1)
- Example kind: `verified`
- Environment profile: `x11`
- Verified source: [tests/doccheck/assert_win.ahk:199](../../tests/doccheck/assert_win.ahk#L199)
- Profile command: `bash tests/doccheck/run_check.sh --xvfb "$BIN"`

Brings the specified window to the top of the stack without explicitly activating it.

## Syntax

````text
WinMoveTop WinTitle, WinText, ExcludeTitle, ExcludeText
````

## Linux-verified example excerpt

This excerpt is executed by the profile command above; surrounding setup and assertions remain in the linked source.

````ahk
Log("group_survivor=" (id_survivor != "" ? 1 : 0))
WinRedraw("ahk_id " id_survivor)
WinMoveTop("ahk_id " id_survivor)
WinMoveBottom("ahk_id " id_survivor)
WinSetEnabled(0, "ahk_id " id_survivor)
WinSetEnabled(1, "ahk_id " id_survivor)
````

## Upstream reference example

Source: [docs-v2/docs/lib/WinMoveTop.htm](../../docs-v2/docs/lib/WinMoveTop.htm)

The upstream page has no standalone code block; use the Linux-verified excerpt above.
