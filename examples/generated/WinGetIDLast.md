# WinGetIDLast

- Linux status: `IMPL` (P1)
- Example kind: `verified`
- Environment profile: `x11`
- Verified source: [tests/doccheck/assert_win.ahk:66](../../tests/doccheck/assert_win.ahk#L66)
- Profile command: `bash tests/doccheck/run_check.sh --xvfb "$BIN"`

Returns the unique ID (HWND) of the last/bottommost window if there is more than one match.

## Syntax

````text
HWND := WinGetIDLast(WinTitle, WinText, ExcludeTitle, ExcludeText)
````

## Linux-verified example excerpt

This excerpt is executed by the profile command above; surrounding setup and assertions remain in the linked source.

````ahk
id_second := WinGetID("DocCheck Second")
Log("getid_ok=" (id_second != "" && WinGetTitle("ahk_id " id_second) = "DocCheck Second"))
Log("getidlast_neq=" (WinGetIDLast("ahk_class DocCheckClass") != WinGetID("ahk_class DocCheckClass") ? 1 : 0))
arr := WinGetList("ahk_class DocCheckClass")
Log("getlist_len=" (arr.Length = 2))
Log("getlist_valid=" (WinExist("ahk_id " arr[1]) != "" && WinExist("ahk_id " arr[2]) != "" ? 1 : 0))
````

## Upstream reference example

Source: [docs-v2/docs/lib/WinGetIDLast.htm](../../docs-v2/docs/lib/WinGetIDLast.htm)

The upstream page has no standalone code block; use the Linux-verified excerpt above.
