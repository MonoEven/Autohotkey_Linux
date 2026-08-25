# GroupAdd

- Linux status: `IMPL` (P1)
- Example kind: `verified`
- Environment profile: `x11`
- Verified source: [tests/doccheck/assert_win.ahk:184](../../tests/doccheck/assert_win.ahk#L184)
- Profile command: `bash tests/doccheck/run_check.sh --xvfb "$BIN"`

## Additional verified environments

- `x11`: [tests/doccheck/assert_misc_cov.ahk:242](../../tests/doccheck/assert_misc_cov.ahk#L242)

Adds a window specification to a window group, creating the group if necessary.

## Syntax

````text
GroupAdd GroupName , WinTitle, WinText, ExcludeTitle, ExcludeText
````

## Linux-verified example excerpt

This excerpt is executed by the profile command above; surrounding setup and assertions remain in the linked source.

````ahk

; --- GroupAdd / GroupActivate (docs: HWND of the activated window). ---
GroupAdd("g1", "ahk_class DocCheckClass")
ga1 := GroupActivate("g1")
Log("group_activate1=" (ga1 != 0 ? 1 : 0))
ga2 := GroupActivate("g1")
````

## Upstream reference example

Source: [docs-v2/docs/lib/GroupAdd.htm](../../docs-v2/docs/lib/GroupAdd.htm)

````ahk
; In global code, to be evaluated at startup:
GroupAdd "MSIE", "ahk_class IEFrame" ; Add only Internet Explorer windows to this group.
; Assign a hotkey to activate this group, which traverses
; through all open MSIE windows, one at a time (i.e. each
; press of the hotkey).
Numpad1::GroupA
````
