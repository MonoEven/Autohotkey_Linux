# DetectHiddenText

- Linux status: `IMPL` (P1)
- Example kind: `verified`
- Environment profile: `headless`
- Verified source: [tests/doccheck/assert_sys.ahk:40](../../tests/doccheck/assert_sys.ahk#L40)
- Profile command: `bash tests/doccheck/run_check.sh "$BIN"`

Determines whether invisible text in a window is "seen" for the purpose of finding the window. This affects windowing functions such as WinExist and WinActivate.

## Syntax

````text
PrevSetting := DetectHiddenText(Setting)
````

## Linux-verified example excerpt

This excerpt is executed by the profile command above; surrounding setup and assertions remain in the linked source.

````ahk
MsgBox "DetectHiddenWindows_return=" (DetectHiddenWindows(0) = 1)
; Docs: hidden text detection is enabled by default.
MsgBox "DetectHiddenText_prev=" (DetectHiddenText(0) = 1)
MsgBox "DetectHiddenText_set=" (A_DetectHiddenText = 0)
MsgBox "DetectHiddenText_return=" (DetectHiddenText(1) = 0)
````

## Upstream reference example

Source: [docs-v2/docs/lib/DetectHiddenText.htm](../../docs-v2/docs/lib/DetectHiddenText.htm)

````ahk
DetectHiddenText false
````
