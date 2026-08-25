# WinGetMinMax

- Linux status: `IMPL` (P1)
- Example kind: `verified`
- Environment profile: `x11`
- Verified source: [tests/doccheck/assert_win.ahk:96](../../tests/doccheck/assert_win.ahk#L96)
- Profile command: `bash tests/doccheck/run_check.sh --xvfb "$BIN"`

Returns a non-zero number if the specified window is maximized or minimized.

## Syntax

````text
MinMax := WinGetMinMax(WinTitle, WinText, ExcludeTitle, ExcludeText)
````

## Linux-verified example excerpt

This excerpt is executed by the profile command above; surrounding setup and assertions remain in the linked source.

````ahk
; --- WinMinimize / WinMaximize / WinRestore + WinGetMinMax. ---
WinMinimize("DocCheck Alpha")
Log("minmax_min=" (WinGetMinMax("DocCheck Alpha") = -1 ? 1 : 0))
WinRestore("DocCheck Alpha")
Log("minmax_restored=" (WinGetMinMax("DocCheck Alpha") = 0 ? 1 : 0))
WinMaximize("DocCheck Alpha")
````

## Upstream reference example

Source: [docs-v2/docs/lib/WinGetMinMax.htm](../../docs-v2/docs/lib/WinGetMinMax.htm)

The upstream page has no standalone code block; use the Linux-verified excerpt above.
