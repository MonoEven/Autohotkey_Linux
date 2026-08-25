# ListViewGetContent

- Linux status: `IMPL` (P1)
- Example kind: `verified`
- Environment profile: `x11`
- Verified source: [tests/doccheck/assert_edit.ahk:121](../../tests/doccheck/assert_edit.ahk#L121)
- Profile command: `bash tests/doccheck/run_check.sh --xvfb "$BIN"`

Returns content data from a list-view control, such as rows, columns, or count values.

## Syntax

````text
Data := ListViewGetContent(Options, ControlID, WinTitle, WinText, ExcludeTitle, ExcludeText)
````

## Linux-verified example excerpt

This excerpt is executed by the profile command above; surrounding setup and assertions remain in the linked source.

````ahk
; --- ListViewGetContent: empty control (external ListView; filling rows via
; ControlAddItem is virtual state and throws OSError, M5-B) ---
Log("lv_count0=" (ListViewGetContent("Count", "LV1", "EdMain") = 0 ? 1 : 0))
Log("lv_colcount0=" (ListViewGetContent("Count Col", "LV1", "EdMain") = -1 ? 1 : 0))
Log("lv_sel0=" (ListViewGetContent("Count Selected", "LV1", "EdMain") = 0 ? 1 : 0))
Log("lv_foc0=" (ListViewGetContent("Count Focused", "LV1", "EdMain") = 0 ? 1 : 0))
````

## Upstream reference example

Source: [docs-v2/docs/lib/ListViewGetContent.htm](../../docs-v2/docs/lib/ListViewGetContent.htm)

````ahk
List := ListViewGetContent("Selected", "SysListView321", WinTitle)
Loop Parse, List, "`n"  ; Rows are delimited by linefeeds (`n).
{
    RowNumber := A_Index
    Loop Parse, A_LoopField, A_Tab  ; Fields (columns) in each row are delimited by tabs (A_Tab).
        MsgBox "Row #" RowNumber "
````
