# WinGetList

- Linux status: `IMPL` (P1)
- Example kind: `verified`
- Environment profile: `x11`
- Verified source: [tests/doccheck/assert_win.ahk:67](../../tests/doccheck/assert_win.ahk#L67)
- Profile command: `bash tests/doccheck/run_check.sh --xvfb "$BIN"`

Returns an array of unique IDs (HWNDs) for all existing windows that match the specified criteria.

## Syntax

````text
HWNDs := WinGetList(WinTitle, WinText, ExcludeTitle, ExcludeText)
````

## Linux-verified example excerpt

This excerpt is executed by the profile command above; surrounding setup and assertions remain in the linked source.

````ahk
Log("getid_ok=" (id_second != "" && WinGetTitle("ahk_id " id_second) = "DocCheck Second"))
Log("getidlast_neq=" (WinGetIDLast("ahk_class DocCheckClass") != WinGetID("ahk_class DocCheckClass") ? 1 : 0))
arr := WinGetList("ahk_class DocCheckClass")
Log("getlist_len=" (arr.Length = 2))
Log("getlist_valid=" (WinExist("ahk_id " arr[1]) != "" && WinExist("ahk_id " arr[2]) != "" ? 1 : 0))
Log("getlist_none=" (WinGetList("ahk_class NopeClass").Length = 0))
````

## Upstream reference example

Source: [docs-v2/docs/lib/WinGetList.htm](../../docs-v2/docs/lib/WinGetList.htm)

````ahk
ids := WinGetList(,, "Program Manager")
for this_id in ids
{
    WinActivate this_id
    this_class := WinGetClass(this_id)
    this_title := WinGetTitle(this_id)
    Result := MsgBox(
    (
        "Visiting All Windows
        " A_Index " of " ids.Length "
        ahk_id " this
````
