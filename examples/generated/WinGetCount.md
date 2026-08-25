# WinGetCount

- Linux status: `IMPL` (P1)
- Example kind: `verified`
- Environment profile: `x11`
- Verified source: [tests/doccheck/assert_win.ahk:28](../../tests/doccheck/assert_win.ahk#L28)
- Profile command: `bash tests/doccheck/run_check.sh --xvfb "$BIN"`

Returns the number of existing windows that match the specified criteria.

## Syntax

````text
Count := WinGetCount(WinTitle, WinText, ExcludeTitle, ExcludeText)
````

## Linux-verified example excerpt

This excerpt is executed by the profile command above; surrounding setup and assertions remain in the linked source.

````ahk
id_alpha := WinExist("DocCheck Alpha")
Log("exist_alpha=" (id_alpha != "" ? 1 : 0))
Log("exist_count=" (WinGetCount("DocCheck") = 3))  ; Alpha, Beta, Second (Gamma hidden).
; Docs: SetTitleMatchMode 3 = exact match.
SetTitleMatchMode(3)
Log("exact_mode=" (WinGetCount("DocCheck") = 0))
````

## Upstream reference example

Source: [docs-v2/docs/lib/WinGetCount.htm](../../docs-v2/docs/lib/WinGetCount.htm)

The upstream page has no standalone code block; use the Linux-verified excerpt above.
