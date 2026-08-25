# WinRedraw

- Linux status: `IMPL` (P1)
- Example kind: `verified`
- Environment profile: `x11`
- Verified source: [tests/doccheck/assert_win.ahk:198](../../tests/doccheck/assert_win.ahk#L198)
- Profile command: `bash tests/doccheck/run_check.sh --xvfb "$BIN"`

Redraws the specified window.

## Syntax

````text
WinRedraw WinTitle, WinText, ExcludeTitle, ExcludeText
````

## Linux-verified example excerpt

This excerpt is executed by the profile command above; surrounding setup and assertions remain in the linked source.

````ahk
id_survivor := WinExist("ahk_class DocCheckClass")
Log("group_survivor=" (id_survivor != "" ? 1 : 0))
WinRedraw("ahk_id " id_survivor)
WinMoveTop("ahk_id " id_survivor)
WinMoveBottom("ahk_id " id_survivor)
WinSetEnabled(0, "ahk_id " id_survivor)
````

## Upstream reference example

Source: [docs-v2/docs/lib/WinRedraw.htm](../../docs-v2/docs/lib/WinRedraw.htm)

The upstream page has no standalone code block; use the Linux-verified excerpt above.
