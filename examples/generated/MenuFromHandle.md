# MenuFromHandle

- Linux status: `IMPL` (P1)
- Example kind: `verified`
- Environment profile: `x11`
- Verified source: [tests/doccheck/assert_shape.ahk:119](../../tests/doccheck/assert_shape.ahk#L119)
- Profile command: `bash tests/doccheck/run_check.sh --xvfb "$BIN"`

Retrieves the Menu or MenuBar object corresponding to a Win32 menu handle.

## Syntax

````text
Menu := MenuFromHandle(Handle)
````

## Linux-verified example excerpt

This excerpt is executed by the profile command above; surrounding setup and assertions remain in the linked source.

````ahk
Log("gcfr_hwnd=" (GuiCtrlFromHwnd(hwnd) = "" ? 1 : 0))
Log("gcfr_zero=" (GuiCtrlFromHwnd(0) = "" ? 1 : 0))
Log("mfr_handle=" (MenuFromHandle(12345) = "" ? 1 : 0))
Log("mfr_zero=" (MenuFromHandle(0) = "" ? 1 : 0))

; --- Cleanup. ---
````

## Upstream reference example

Source: [docs-v2/docs/lib/MenuFromHandle.htm](../../docs-v2/docs/lib/MenuFromHandle.htm)

The upstream page has no standalone code block; use the Linux-verified excerpt above.
