# SetTitleMatchMode

- Linux status: `IMPL` (P1)
- Example kind: `verified`
- Environment profile: `x11`
- Verified source: [tests/doccheck/assert_ctrl.ahk:115](../../tests/doccheck/assert_ctrl.ahk#L115)
- Profile command: `bash tests/doccheck/run_check.sh --xvfb "$BIN"`

## Additional verified environments

- `headless`: [tests/doccheck/assert_sys.ahk:45](../../tests/doccheck/assert_sys.ahk#L45)
- `x11`: [tests/doccheck/assert_win.ahk:30](../../tests/doccheck/assert_win.ahk#L30)

Sets the matching behavior of the WinTitle parameter in built-in functions such as WinWait.

## Syntax

````text
PrevMatchMode := SetTitleMatchMode(MatchMode) PrevSpeed := SetTitleMatchMode(Speed)
````

## Linux-verified example excerpt

This excerpt is executed by the profile command above; surrounding setup and assertions remain in the linked source.

````ahk
Log("gettext_textmatch=" (ControlGetText("Hello World", "CtlMain") = "Hello World" ? 1 : 0))
; TitleMatchMode 3 (exact): "Hello" must NOT match "Hello World".
SetTitleMatchMode(3)
try
    ControlGetText("Hello", "CtlMain")
catch TargetError
````

## Upstream reference example

Source: [docs-v2/docs/lib/SetTitleMatchMode.htm](../../docs-v2/docs/lib/SetTitleMatchMode.htm)

````ahk
SetTitleMatchMode 1
````
