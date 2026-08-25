# DetectHiddenWindows

- Linux status: `IMPL` (P1)
- Example kind: `verified`
- Environment profile: `headless`
- Verified source: [tests/doccheck/assert_sys.ahk:36](../../tests/doccheck/assert_sys.ahk#L36)
- Profile command: `bash tests/doccheck/run_check.sh "$BIN"`

## Additional verified environments

- `x11`: [tests/doccheck/assert_win.ahk:142](../../tests/doccheck/assert_win.ahk#L142)

Determines whether invisible windows are "seen" by the script.

## Syntax

````text
PrevSetting := DetectHiddenWindows(Setting)
````

## Linux-verified example excerpt

This excerpt is executed by the profile command above; surrounding setup and assertions remain in the linked source.

````ahk
; Docs: hidden window detection is disabled by default; DetectHiddenWindows
; returns the previous setting.
MsgBox "DetectHiddenWindows_prev=" (DetectHiddenWindows(1) = 0)
MsgBox "DetectHiddenWindows_set=" (A_DetectHiddenWindows = 1)
MsgBox "DetectHiddenWindows_return=" (DetectHiddenWindows(0) = 1)
; Docs: hidden text detection is enabled by default.
````

## Upstream reference example

Source: [docs-v2/docs/lib/DetectHiddenWindows.htm](../../docs-v2/docs/lib/DetectHiddenWindows.htm)

````ahk
DetectHiddenWindows true
````
