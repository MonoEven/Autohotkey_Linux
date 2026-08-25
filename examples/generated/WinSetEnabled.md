# WinSetEnabled

- Linux status: `IMPL` (P1)
- Example kind: `verified`
- Environment profile: `x11`
- Verified source: [tests/doccheck/assert_win.ahk:201](../../tests/doccheck/assert_win.ahk#L201)
- Profile command: `bash tests/doccheck/run_check.sh --xvfb "$BIN"`

Enables or disables the specified window.

## Syntax

````text
WinSetEnabled NewSetting , WinTitle, WinText, ExcludeTitle, ExcludeText
````

## Linux-verified example excerpt

This excerpt is executed by the profile command above; surrounding setup and assertions remain in the linked source.

````ahk
WinMoveTop("ahk_id " id_survivor)
WinMoveBottom("ahk_id " id_survivor)
WinSetEnabled(0, "ahk_id " id_survivor)
WinSetEnabled(1, "ahk_id " id_survivor)
Log("misc_ok=1")
````

## Upstream reference example

Source: [docs-v2/docs/lib/WinSetEnabled.htm](../../docs-v2/docs/lib/WinSetEnabled.htm)

The upstream page has no standalone code block; use the Linux-verified excerpt above.
