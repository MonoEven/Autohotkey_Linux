; Window module doc-check (v2 docs: WinExist/WinActive/WinGet*/WinSet*/WinMove/
; WinClose/WinKill/WinWait*/WinActivate/WinMinimize/Maximize/Restore/Hide/Show/
; Group*).  Runs under Xvfb (run_check.sh --xvfb) with windows created by the
; xwin_helper test client.  Output goes to a file (MsgBox would block with a
; display present).
#Requires AutoHotkey v2.0

WINOUT := "/tmp/ahk_dc_win_out.txt"
FileDelete(WINOUT)
Log(line) => FileAppend(line "`n", WINOUT)

; --- Launch helper windows (visible: Alpha, Beta, Second; hidden: Gamma). ---
Run('out/xwin_helper -title "DocCheck Alpha" -class DocCheckClass -x 100 -y 100 -w 400 -h 300')
Run('out/xwin_helper -title "DocCheck Beta" -class OtherClass -x 500 -y 400 -w 300 -h 200')
Run('out/xwin_helper -title "DocCheck Gamma Hidden" -class DocCheckClass -x 50 -y 50 -w 200 -h 150 -hidden')
Run('out/xwin_helper -title "DocCheck Second" -class DocCheckClass -x 200 -y 200 -w 320 -h 240')

; --- WinWait (docs: returns the HWND, or 0 on timeout). ---
Log("winwait_alpha=" (WinWait("DocCheck Alpha",, 5) != 0 ? 1 : 0))
Log("winwait_beta=" (WinWait("DocCheck Beta",, 5) != 0 ? 1 : 0))
Log("winwait_second=" (WinWait("DocCheck Second",, 5) != 0 ? 1 : 0))
Log("winwait_timeout=" (WinWait("No Such Window Ever",, 0.3) = 0 ? 1 : 0))
Sleep(100)

; --- WinExist / title matching (default mode 2: contains). ---
id_alpha := WinExist("DocCheck Alpha")
Log("exist_alpha=" (id_alpha != "" ? 1 : 0))
Log("exist_count=" (WinGetCount("DocCheck") = 3))  ; Alpha, Beta, Second (Gamma hidden).
; Docs: SetTitleMatchMode 3 = exact match.
SetTitleMatchMode(3)
Log("exact_mode=" (WinGetCount("DocCheck") = 0))
Log("exact_alpha=" (WinExist("DocCheck Alpha") != "" ? 1 : 0))
; Docs: RegEx mode.
SetTitleMatchMode("RegEx")
Log("regex_mode=" (WinGetCount("^DocCheck") = 3))
SetTitleMatchMode(2)

; --- ahk_class / ahk_exe / ahk_pid / ahk_id criteria. ---
Log("class_count=" (WinGetCount("ahk_class DocCheckClass") = 2))  ; Alpha + Second (Gamma hidden).
; Combined criteria (title + ahk_exe): robust against stray helper windows
; left by other suites; also exercises multi-criteria matching per docs.
Log("exe_count=" (WinGetCount("DocCheck ahk_exe xwin_helper") = 3))
Log("exe_missing=" (WinGetCount("ahk_exe no_such_proc_xyz") = 0))
wpid := WinGetPID("DocCheck Alpha")
Log("pid_criteria=" (WinExist("ahk_pid " wpid) != "" ? 1 : 0))
Log("id_criteria=" (WinGetTitle("ahk_id " id_alpha) = "DocCheck Alpha"))

; --- WinGetTitle / WinGetClass / WinGetPID / WinGetProcessName / WinGetProcessPath. ---
Log("gettitle=" (WinGetTitle("DocCheck Alpha") = "DocCheck Alpha"))
Log("getclass=" (WinGetClass("DocCheck Alpha") = "DocCheckClass"))
Log("getpid_gt0=" (WinGetPID("DocCheck Alpha") > 0 ? 1 : 0))
Log("getprocname=" (WinGetProcessName("DocCheck Alpha") = "xwin_helper"))
Log("getprocpath=" (SubStr(WinGetProcessPath("DocCheck Alpha"), -11) = "xwin_helper"))
Log("gettitle_after_rename=" (WinGetTitle("DocCheck Beta") = "DocCheck Beta"))
try {
    WinGetTitle("No Such Window Ever")
    gettitle_missing := "noerr"
} catch TargetError {
    gettitle_missing := "TargetError"
}
Log("gettitle_missing=" gettitle_missing)

; --- WinGetID / WinGetIDLast / WinGetList / WinGetCount. ---
id_second := WinGetID("DocCheck Second")
Log("getid_ok=" (id_second != "" && WinGetTitle("ahk_id " id_second) = "DocCheck Second"))
Log("getidlast_neq=" (WinGetIDLast("ahk_class DocCheckClass") != WinGetID("ahk_class DocCheckClass") ? 1 : 0))
arr := WinGetList("ahk_class DocCheckClass")
Log("getlist_len=" (arr.Length = 2))
Log("getlist_valid=" (WinExist("ahk_id " arr[1]) != "" && WinExist("ahk_id " arr[2]) != "" ? 1 : 0))
Log("getlist_none=" (WinGetList("ahk_class NopeClass").Length = 0))

; --- WinGetPos / WinGetClientPos. ---
WinGetPos(&x, &y, &w, &h, "DocCheck Alpha")
Log("getpos=" (x = 100 && y = 100 && w = 400 && h = 300 ? 1 : 0))
WinGetClientPos(&cx, &cy, &cw, &ch, "DocCheck Alpha")
Log("getclientpos=" (cx = 100 && cy = 100 && cw = 400 && ch = 300 ? 1 : 0))

; --- WinGetText / WinGetControls / WinGetControlsHwnd (no X11 controls). ---
Log("gettext=" (WinGetText("DocCheck Alpha") = ""))
Log("getcontrols=" (WinGetControls("DocCheck Alpha").Length = 0))
Log("getcontrolshwnd=" (WinGetControlsHwnd("DocCheck Alpha").Length = 0))

; --- WinMove (docs: X/Y/Width/Height then WinTitle). ---
WinMove(150, 160, 420, 310, "DocCheck Alpha")
WinGetPos(&x, &y, &w, &h, "DocCheck Alpha")
Log("winmove=" (x = 150 && y = 160 && w = 420 && h = 310 ? 1 : 0))
WinMove(100, 100, 400, 300, "DocCheck Alpha")

; --- WinSetTitle. ---
WinSetTitle("DocCheck Alpha Renamed", "DocCheck Alpha")
Log("winsettitle=" (WinGetTitle("DocCheck Alpha Renamed") = "DocCheck Alpha Renamed"))
WinSetTitle("DocCheck Alpha", "DocCheck Alpha Renamed")

; --- WinMinimize / WinMaximize / WinRestore + WinGetMinMax. ---
WinMinimize("DocCheck Alpha")
Log("minmax_min=" (WinGetMinMax("DocCheck Alpha") = -1 ? 1 : 0))
WinRestore("DocCheck Alpha")
Log("minmax_restored=" (WinGetMinMax("DocCheck Alpha") = 0 ? 1 : 0))
WinMaximize("DocCheck Alpha")
Log("minmax_max=" (WinGetMinMax("DocCheck Alpha") = 1 ? 1 : 0))
WinRestore("DocCheck Alpha")
Log("minmax_norm=" (WinGetMinMax("DocCheck Alpha") = 0 ? 1 : 0))

; --- WinSetAlwaysOnTop + WinGetExStyle (WS_EX_TOPMOST = 0x8). ---
WinSetAlwaysOnTop(1, "DocCheck Alpha")
Log("topmost_on=" ((WinGetExStyle("DocCheck Alpha") & 0x8) ? 1 : 0))
WinSetAlwaysOnTop(0, "DocCheck Alpha")
Log("topmost_off=" ((WinGetExStyle("DocCheck Alpha") & 0x8) ? 0 : 1))

; --- WinSetTransparent / WinGetTransparent. ---
WinSetTransparent(128, "DocCheck Alpha")
Log("transparent_set=" (WinGetTransparent("DocCheck Alpha") = 128 ? 1 : 0))
WinSetTransparent("Off", "DocCheck Alpha")
Log("transparent_off=" (WinGetTransparent("DocCheck Alpha") = "" ? 1 : 0))

; --- WinSetTransColor / WinGetTransColor (stored; no X11 equivalent). ---
WinSetTransColor("0x112233", "DocCheck Alpha")
Log("transcolor_set=" (WinGetTransColor("DocCheck Alpha") = "0x112233" ? 1 : 0))
WinSetTransColor("Off", "DocCheck Alpha")
Log("transcolor_off=" (WinGetTransColor("DocCheck Alpha") = "" ? 1 : 0))
try {
    WinSetTransColor("NotAColor", "DocCheck Alpha")
    transcolor_bad := "noerr"
} catch ValueError {
    transcolor_bad := "ValueError"
}
Log("transcolor_bad=" transcolor_bad)

; --- WinSetStyle / WinGetStyle, WinSetExStyle / WinGetExStyle. ---
WinSetStyle("+0x800000", "DocCheck Alpha")
Log("style_add=" (WinGetStyle("DocCheck Alpha") = 0x800000 ? 1 : 0))
WinSetStyle("-0x800000", "DocCheck Alpha")
Log("style_sub=" (WinGetStyle("DocCheck Alpha") = 0 ? 1 : 0))
WinSetStyle("0x1234", "DocCheck Alpha")
Log("style_set=" (WinGetStyle("DocCheck Alpha") = 0x1234 ? 1 : 0))
WinSetExStyle("+0x100", "DocCheck Alpha")
Log("exstyle_add=" (WinGetExStyle("DocCheck Alpha") & 0x100 ? 1 : 0))

; --- WinHide / WinShow + DetectHiddenWindows. ---
WinHide("DocCheck Beta")
Log("hide_notexist=" (WinExist("DocCheck Beta") = "" ? 1 : 0))
DetectHiddenWindows(1)
Log("hide_dhw=" (WinExist("DocCheck Beta") != "" ? 1 : 0))
; Docs: a hidden window is only detectable with DetectHiddenWindows On, so
; WinShow on the hidden window requires it too.
WinShow("DocCheck Beta")
Sleep(100)
DetectHiddenWindows(0)
Log("show_again=" (WinExist("DocCheck Beta") != "" ? 1 : 0))

; --- WinActivate / WinActive / WinWaitActive / WinWaitNotActive. ---
WinActivate("DocCheck Alpha")
Sleep(100)
Log("winactive=" (WinActive("DocCheck Alpha") != "" ? 1 : 0))
Log("winnotactive=" (WinActive("DocCheck Beta") = "" ? 1 : 0))
Log("winwaitactive=" (WinWaitActive("DocCheck Alpha",, 3) != 0 ? 1 : 0))
Log("winwaitnotactive=" (WinWaitActive("DocCheck Beta",, 0.3) = 0 ? 1 : 0))

; --- WinClose + WinWaitClose. ---
WinClose("DocCheck Beta")
Log("winwaitclose=" (WinWaitClose("DocCheck Beta",, 5) = 1 ? 1 : 0))
Log("close_gone=" (WinExist("DocCheck Beta") = "" ? 1 : 0))

; --- WinKill. ---
Run('out/xwin_helper -title "Kill Me Now" -class KillClass -x 30 -y 30 -w 150 -h 120')
WinWait("Kill Me Now",, 5)
WinKill("Kill Me Now")
Log("winkill=" (WinWaitClose("Kill Me Now",, 5) = 1 ? 1 : 0))

; --- WinMinimizeAll / WinMinimizeAllUndo. ---
WinMinimizeAll()
Sleep(50)
Log("minall=" (WinGetMinMax("DocCheck Alpha") = -1 ? 1 : 0))
WinMinimizeAllUndo()
Sleep(50)
Log("minall_undo=" (WinGetMinMax("DocCheck Alpha") = 0 ? 1 : 0))

; --- WinGetID of the last found window via empty WinExist. ---
id_before := WinExist("DocCheck Alpha")
id_after := WinExist()
Log("lastused=" (id_after = id_before ? 1 : 0))

; --- GroupAdd / GroupActivate (docs: HWND of the activated window). ---
GroupAdd("g1", "ahk_class DocCheckClass")
ga1 := GroupActivate("g1")
Log("group_activate1=" (ga1 != 0 ? 1 : 0))
ga2 := GroupActivate("g1")
Log("group_cycle=" (ga2 != 0 && ga2 != ga1 ? 1 : 0))
; Docs: GroupClose closes the active window if it is a member of the group.
id_active := WinActive("ahk_class DocCheckClass")
Log("group_active_found=" (id_active != "" ? 1 : 0))
GroupClose("g1")
Log("group_close=" (WinWaitClose("ahk_id " id_active,, 5) = 1 ? 1 : 0))

; --- WinRedraw / WinMoveTop / WinMoveBottom / WinSetEnabled: no error. ---
id_survivor := WinExist("ahk_class DocCheckClass")
Log("group_survivor=" (id_survivor != "" ? 1 : 0))
WinRedraw("ahk_id " id_survivor)
WinMoveTop("ahk_id " id_survivor)
WinMoveBottom("ahk_id " id_survivor)
WinSetEnabled(0, "ahk_id " id_survivor)
WinSetEnabled(1, "ahk_id " id_survivor)
Log("misc_ok=1")

; --- Cleanup: kill any remaining helpers. ---
try
    WinKill("DocCheck Alpha")
try
    WinKill("DocCheck Second")
try
    WinKill("DocCheck Gamma Hidden")
Run("pkill -f xwin_helper")
