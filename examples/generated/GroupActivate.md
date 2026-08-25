# GroupActivate

- Linux status: `IMPL` (P1)
- Example kind: `verified`
- Environment profile: `x11`
- Verified source: [tests/doccheck/assert_win.ahk:185](../../tests/doccheck/assert_win.ahk#L185)
- Profile command: `bash tests/doccheck/run_check.sh --xvfb "$BIN"`

Activates the next window in a window group that was defined with GroupAdd.

## Syntax

````text
HWND := GroupActivate(GroupName , Mode)
````

## Linux-verified example excerpt

This excerpt is executed by the profile command above; surrounding setup and assertions remain in the linked source.

````ahk
; --- GroupAdd / GroupActivate (docs: HWND of the activated window). ---
GroupAdd("g1", "ahk_class DocCheckClass")
ga1 := GroupActivate("g1")
Log("group_activate1=" (ga1 != 0 ? 1 : 0))
ga2 := GroupActivate("g1")
Log("group_cycle=" (ga2 != 0 && ga2 != ga1 ? 1 : 0))
````

## Upstream reference example

Source: [docs-v2/docs/lib/GroupActivate.htm](../../docs-v2/docs/lib/GroupActivate.htm)

````ahk
GroupActivate "MyGroup", "R"
````
