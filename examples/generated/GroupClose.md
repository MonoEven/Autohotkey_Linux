# GroupClose

- Linux status: `IMPL` (P1)
- Example kind: `verified`
- Environment profile: `x11`
- Verified source: [tests/doccheck/assert_win.ahk:192](../../tests/doccheck/assert_win.ahk#L192)
- Profile command: `bash tests/doccheck/run_check.sh --xvfb "$BIN"`

Closes the active window if it was just activated by GroupActivate or GroupDeactivate. It then activates the next window in the series. It can also close all windows in a group.

## Syntax

````text
GroupClose GroupName , Mode
````

## Linux-verified example excerpt

This excerpt is executed by the profile command above; surrounding setup and assertions remain in the linked source.

````ahk
id_active := WinActive("ahk_class DocCheckClass")
Log("group_active_found=" (id_active != "" ? 1 : 0))
GroupClose("g1")
Log("group_close=" (WinWaitClose("ahk_id " id_active,, 5) = 1 ? 1 : 0))

; --- WinRedraw / WinMoveTop / WinMoveBottom / WinSetEnabled: no error. ---
````

## Upstream reference example

Source: [docs-v2/docs/lib/GroupClose.htm](../../docs-v2/docs/lib/GroupClose.htm)

````ahk
GroupClose "MyGroup", "R"
````
