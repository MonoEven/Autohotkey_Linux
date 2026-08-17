; GUI-module doc-check (v2 docs: Gui / GuiControl / Menu / MenuBar).
; Runs under Xvfb (run_check.sh --xvfb): GTK3 windows are created and
; exercised - controls, values, Submit, OnEvent dispatch through the pump,
; and a menu bar.  Output goes to /tmp/ahk_dc_gui_out.txt.
#Requires AutoHotkey v2.0

OUT := "/tmp/ahk_dc_gui_out.txt"
FileDelete(OUT)
Log(line) => FileAppend(line "`n", OUT)

try {
    ; --- basic window + controls --------------------------------------
    g := Gui()
    g.Title := "DocCheck GUI"
    e := g.Add("Edit", "w200 vTheEdit", "hello")
    cb := g.Add("CheckBox", "x+10 vTheCheck", "Check")
    g.Add("Radio", "x+10 vTheRadio", "R1")
    g.Add("Radio", "yp", "R2")
    ddl := g.Add("DropDownList", "w120 vTheDDL", ["A", "B", "C"])
    ddl.Value := 2
    lb := g.Add("ListBox", "w100 r3 vTheList", ["X", "Y", "Z"])
    lb.Value := 2
    tv := g.Add("TreeView", "w150 h80 vTheTree")
    root := tv.Add("Root")
    child := tv.Add("Child", root)
    sb := g.Add("StatusBar", "vTheStatus")
    sb.SetText("ready")
    lv := g.Add("ListView", "w200 h80 vTheLV")
    lv.InsertCol(1, "80", "ColA")
    lv.InsertCol(2, "80", "ColB")
    lv.Add(, "A1", "A2")
    lv.Add(, "B1", "B2")
    g.Add("Button", "Default vTheButton", "OK")

    ; --- Menu + MenuBar ------------------------------------------------
    fileMenu := MenuBar()
    fileMenu.Add("&New", (*) => 0)
    fileMenu.Add("&Open", (*) => 0)
    fileMenu.Add()
    fileMenu.Add("E&xit", (*) => ExitApp())
    fileMenu.Check("&New")
    fileMenu.Disable("&Open")
    fileMenu.Rename("&New", "&Create")
    fileMenu.Uncheck("&Create")
    fileMenu.Enable("&Open")
    g.MenuBar := fileMenu
    menu_ok := (g.MenuBar = fileMenu)
    Log("menu_bar=" menu_ok)

    ; --- OnEvent dispatch through the pump -----------------------------
    close_fired := false
    g.OnEvent("Close", (*) => close_fired := true)
    g.Show("w420 h320")

    Log("title=" g.Title)
    Log("hwnd_set=" (g.Hwnd != 0))
    Log("edit_value=" e.Value)
    Log("edit_text=" e.Text)
    e.Value := "hi2"
    Log("edit_set=" e.Value)
    Log("check_value=" cb.Value)
    cb.Value := 1
    Log("check_set=" cb.Value)
    Log("radio_value=" g["TheRadio"].Value)
    Log("ddl_value=" ddl.Value)
    Log("ddl_text=" ddl.Text)
    Log("list_value=" lb.Value)
    Log("lv_count=" lv.GetCount())
    Log("lv_text=" lv.GetText(2, 2))
    Log("lv_row_count=" lv.GetCount("Col"))
    Log("tv_text=" tv.GetText(child))
    Log("tv_parent=" (tv.GetParent(child) = root))
    Log("tv_count=" tv.GetCount())
    Log("focused_edit=" (g.FocusedCtrl = e))
    Log("visible=" (g.Opt("+Visible") = ""))
    Log("enabled=" e.Enabled)
    Log("type_edit=" e.Type)

    ; Submit with named controls.
    res := g.Submit(false)
    Log("submit_edit=" res.TheEdit)
    Log("submit_check=" res.TheCheck)
    Log("submit_ddl=" res.TheDDL)

    ; Event registration (dispatch of a user-close is exercised by
    ; xdotool-based integration elsewhere; registration must not error).
    g.OnEvent("Close", (*) => 0)
    Log("onevent_close=ok")

    ; Reverse HWND mapping: GuiFromHwnd / GuiCtrlFromHwnd resolve script
    ; objects from a live GTK window/control handle (docs: return the object
    ; or an empty string when there is none).
    gh := g.Hwnd
    Log("gfr_hwnd=" (Type(GuiFromHwnd(gh)) = "Gui" ? "ok" : "bad"))
    Log("gfr_recurse=" (Type(GuiFromHwnd(e.Hwnd, 1)) = "Gui" ? "ok" : "bad"))
    Log("gfr_norecurse=" (GuiFromHwnd(e.Hwnd) = "" ? "ok" : "bad"))
    Log("gfr_ctrl=" (IsObject(GuiCtrlFromHwnd(e.Hwnd)) ? "ok" : "bad"))
    Log("gfr_notfound=" (GuiFromHwnd(123456789) = "" ? "ok" : "bad"))

    g.Destroy()
    Log("gfr_destroyed=" (GuiFromHwnd(gh) = "" ? "ok" : "bad"))
    Log("destroyed_ok=1")
    Log("gui_ok=1")
} catch as err {
    Log("gui_err=" err.Message)
    try Log("gui_err_what=" err.What)
    try Log("gui_err_stack=" err.Stack)
}
ExitApp 0
