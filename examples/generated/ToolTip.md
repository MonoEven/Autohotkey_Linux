# ToolTip

- Linux status: `IMPL` (P1)
- Example kind: `verified`
- Environment profile: `x11`
- Verified source: [tests/doccheck/assert_timer.ahk:80](../../tests/doccheck/assert_timer.ahk#L80)
- Profile command: `bash tests/doccheck/run_check.sh --xvfb "$BIN"`

## Additional verified environments

- `wayland`: [tests/doccheck/assert_wayland.ahk:77](../../tests/doccheck/assert_wayland.ahk#L77)

Shows an always-on-top window anywhere on the screen.

## Syntax

````text
ToolTip Text, X, Y, WhichToolTip
````

## Linux-verified example excerpt

This excerpt is executed by the profile command above; surrounding setup and assertions remain in the linked source.

````ahk
; --- ToolTip (docs: returns the tooltip's HWND; update in place; blank/omitted
; Text hides the tooltip and returns 0; X/Y position). ---
h1 := ToolTip("Hello Tip", 300, 200)
Log("tip_hwnd=" (h1 != 0 ? 1 : 0))
Log("tip_title=" (WinGetTitle("ahk_id " h1) = "Hello Tip" ? 1 : 0))
h2 := ToolTip("Updated Tip", 300, 200)
````

## Upstream reference example

Source: [docs-v2/docs/lib/ToolTip.htm](../../docs-v2/docs/lib/ToolTip.htm)

````ahk
ToolTip "Multiline`nTooltip", 100, 150
````
