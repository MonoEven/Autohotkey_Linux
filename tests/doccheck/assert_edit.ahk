; Edit*/EditPaste/ListViewGetContent doc-check (v2 docs).  Runs under Xvfb
; (run_check.sh --xvfb) with the xwin_helper test client that creates a main
; window plus child "control" windows; output goes to a file.
;
; Semantics verified against docs-v2:
;   - EditGetLineCount: number of lines; "all edit controls have at least one
;     line, even if the control is empty" -> 1 for an empty control.
;   - EditGetLine: the text of line N; ValueError when N is out of range,
;     TargetError when the control is not found.
;   - EditGetCurrentCol/Line: the caret column/line is VIRTUAL state (Windows
;     EM_GETSEL); on an external window (M5-B) these throw OSError instead of
;     pretending a caret position.
;   - EditPaste: the TEXT insertion is a real X11 write (kept); the caret
;     after the paste is virtual (not observable on external windows).
;   - EditGetSelectedText: "" when nothing is selected.
;   - Edit: opens the script in $EDITOR (Linux extension; run_check.sh writes
;     a marker script that records its arguments).
;   - ListViewGetContent: rows come from ControlAddItem, which is virtual
;     state and therefore OSError on external windows (M5-B); an unfilled
;     ListView reports an undetermined column count and empty rows.
#Requires AutoHotkey v2.0

WINOUT := "/tmp/ahk_dc_edit_out.txt"
FileDelete(WINOUT)
Log(line) => FileAppend(line "`n", WINOUT)

MARKER := "/tmp/ahk_dc_edit_marker.txt"
FileDelete(MARKER)

Run('out/xwin_helper -title EdMain -class DocCheck -x 100 -y 100 -w 400 -h 300'
    ' -child Edit1 Edit 30 30 120 24 -child Edit2 Edit 30 70 120 24'
    ' -child LV1 SysListView32 30 120 200 120')
WinWait("EdMain",, 5)
Sleep(300)

; --- EditGetLineCount / EditGetLine: derived from the control's text ---
ControlSetText("alpha`nbeta`ngamma", "Edit1", "EdMain")
Log("ed_lines=" (EditGetLineCount("Edit1", "EdMain") = 3 ? 1 : 0))
Log("ed_line1=" (EditGetLine(1, "Edit1", "EdMain") = "alpha" ? 1 : 0))
Log("ed_line2=" (EditGetLine(2, "Edit1", "EdMain") = "beta" ? 1 : 0))
Log("ed_line3=" (EditGetLine(3, "Edit1", "EdMain") = "gamma" ? 1 : 0))
; Empty control: the helper names children, so clear the text first.
ControlSetText("", "Edit2", "EdMain")
Log("ed_empty_lines=" (EditGetLineCount("Edit2", "EdMain") = 1 ? 1 : 0))
Log("ed_empty_line1=" (EditGetLine(1, "Edit2", "EdMain") = "" ? 1 : 0))
; CRLF text: the line keeps its trailing `r (docs: "might end in a
; carriage return (`r) or a carriage return + linefeed (`r`n)").
ControlSetText("one`r`ntwo", "Edit2", "EdMain")
Log("ed_crlf_count=" (EditGetLineCount("Edit2", "EdMain") = 2 ? 1 : 0))
Log("ed_crlf_line1=" (EditGetLine(1, "Edit2", "EdMain") = "one`r" ? 1 : 0))
Log("ed_crlf_line2=" (EditGetLine(2, "Edit2", "EdMain") = "two" ? 1 : 0))

; --- M5-B: the virtual caret position (EM_GETSEL) is NotSupported on
; --- external windows. ---
try EditGetCurrentCol("Edit1", "EdMain")
catch OSError
    Log("ns_col=1")
try EditGetCurrentLine("Edit1", "EdMain")
catch OSError
    Log("ns_row=1")

; --- EditPaste: the text insertion is REAL (WM_SETTEXT-style write); the
; --- caret after the paste is virtual and refuses to be reported. ---
EditPaste("X", "Edit1", "EdMain")
Log("ed_paste_text=" (ControlGetText("Edit1", "EdMain") = "Xalpha`nbeta`ngamma" ? 1 : 0))
EditPaste("one`ntwo", "Edit1", "EdMain")
Log("ed_paste2_text=" (ControlGetText("Edit1", "EdMain") = "Xone`ntwoalpha`nbeta`ngamma" ? 1 : 0))
Log("ed_paste2_line2=" (EditGetLine(2, "Edit1", "EdMain") = "twoalpha" ? 1 : 0))
try EditGetCurrentCol("Edit1", "EdMain")
catch OSError
    Log("ns_col_after=1")

; --- EditGetSelectedText: nothing is ever selected on the port (no way to
; create a selection without a real edit widget) -> empty string ---
Log("ed_sel_none=" (EditGetSelectedText("Edit1", "EdMain") = "" ? 1 : 0))

; --- ValueError: line index out of range (docs EditGetLine) ---
try {
    EditGetLine(9, "Edit1", "EdMain")
    Log("ed_badline=0")
} catch ValueError {
    Log("ed_badline=1")
}
try {
    EditGetLine(0, "Edit1", "EdMain")
    Log("ed_badline0=0")
} catch ValueError {
    Log("ed_badline0=1")
}

; --- TargetError: unknown control (docs: "thrown if the window or control
; could not be found") ---
try {
    EditGetLineCount("Nope", "EdMain")
    Log("ed_nope=0")
} catch TargetError {
    Log("ed_nope=1")
}
try {
    EditPaste("x", "Nope", "EdMain")
    Log("ed_nope_paste=0")
} catch TargetError {
    Log("ed_nope_paste=1")
}

; --- Edit(): opens the script in $EDITOR (run_check.sh wrote the marker) ---
EnvSet("EDITOR", "/tmp/ahk_edit_marker.sh")
Edit()
Sleep(500)
if FileExist(MARKER) {
    content := FileRead(MARKER)
    Log("ed_editor=1")
    Log("ed_editor_path=" (InStr(content, A_ScriptName) ? 1 : 0))
} else {
    Log("ed_editor=0")
    Log("ed_editor_path=0")
}

; --- ListViewGetContent: empty control (external ListView; filling rows via
; ControlAddItem is virtual state and throws OSError, M5-B) ---
Log("lv_count0=" (ListViewGetContent("Count", "LV1", "EdMain") = 0 ? 1 : 0))
Log("lv_colcount0=" (ListViewGetContent("Count Col", "LV1", "EdMain") = -1 ? 1 : 0))
Log("lv_sel0=" (ListViewGetContent("Count Selected", "LV1", "EdMain") = 0 ? 1 : 0))
Log("lv_foc0=" (ListViewGetContent("Count Focused", "LV1", "EdMain") = 0 ? 1 : 0))
Log("lv_empty=" (ListViewGetContent("", "LV1", "EdMain") = "" ? 1 : 0))
; With an undetermined column count any ColN is attempted (docs) -> "".
Log("lv_col5_empty=" (ListViewGetContent("Col5", "LV1", "EdMain") = "" ? 1 : 0))

; --- M5-B: ControlAddItem/ControlDeleteItem on an external ListView throw
; --- OSError instead of mutating a fake in-process store. ---
try ControlAddItem("RowA", "LV1", "EdMain")
catch OSError
    Log("ns_lv_add=1")
try ControlDeleteItem(1, "LV1", "EdMain")
catch OSError
    Log("ns_lv_del=1")

; --- TargetError: unknown control ---
try {
    ListViewGetContent("Count", "Nope", "EdMain")
    Log("lv_nope=0")
} catch TargetError {
    Log("lv_nope=1")
}

; --- Cleanup. ---
ExitApp(0)
