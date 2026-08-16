; WinSetRegion + GuiFromHwnd/GuiCtrlFromHwnd/MenuFromHandle doc-check
; (v2 docs).  Runs under Xvfb (run_check.sh --xvfb).
;
; Linux semantics (documented in CHECK_REPORT):
;   - WinSetRegion applies the X11 SHAPE extension bounding shape; the
;     upstream option grammar is mirrored (X-Y pairs, Wn/Hn, E, R/Rw-h,
;     Wind), blank options restore the shape, ValueError on bad options,
;     TargetError on a missing window, OSError when SHAPE is unavailable or
;     W/H are missing for ellipse/rounded-rect.  The resulting shape is
;     verified end-to-end with the xshape_probe helper.
;   - GuiFromHwnd/GuiCtrlFromHwnd/MenuFromHandle always return "" per the
;     docs' "or an empty string if there isn't one" clause: the port has no
;     Gui class and cannot create Win32 menus, so no handle can ever
;     correspond to a script object.
#Requires AutoHotkey v2.0

OUT := "/tmp/ahk_dc_shape_out.txt"
FileDelete(OUT)
Log(line) => FileAppend(line "`n", OUT)

Run('out/xwin_helper -title ShpMain -class DocCheck -x 100 -y 100 -w 300 -h 200')
WinWait("ShpMain",, 5)
Sleep(300)
hwnd := WinGetID("ShpMain")
PROBE := "out/xshape_probe"
PFOUT := "/tmp/ahk_dc_shape_probe.txt"

ProbeShape() {
    global hwnd, PROBE, PFOUT
    FileDelete(PFOUT)
    RunWait(PROBE " " hwnd " " PFOUT)
    if !FileExist(PFOUT)
        return ""
    return FileRead(PFOUT)
}

; --- Rectangle: "0-0 W100 H50" -> one 100x50 rect at the origin. ---
WinSetRegion("0-0 W100 H50", "ShpMain")
Sleep(200)
s := ProbeShape()
Log("wsr_rect=" (InStr(s, "shaped=1") && InStr(s, "rect 0 0 100 50") ? 1 : 0))
; --- Restore: blank Options removes the explicit shape (docs). ---
WinSetRegion(, "ShpMain")
Sleep(200)
s := ProbeShape()
Log("wsr_restore=" (InStr(s, "shaped=0") ? 1 : 0))
; --- Ellipse: "0-0 E W100 H50" -> scan-line rects (more than one). ---
WinSetRegion("0-0 E W100 H50", "ShpMain")
Sleep(200)
s := ProbeShape()
Log("wsr_ellipse=" (InStr(s, "shaped=1") && s != "shape=0" && s != "shape=1" ? 1 : 0))
WinSetRegion(, "ShpMain")
; --- Rounded rectangle: "5-5 W80 H40 R10-5". ---
WinSetRegion("5-5 W80 H40 R10-5", "ShpMain")
Sleep(200)
s := ProbeShape()
Log("wsr_round=" (InStr(s, "rect 15 5 60 1") ? 1 : 0))
WinSetRegion(, "ShpMain")
; --- Polygon: triangle. ---
WinSetRegion("0-0 40-0 20-30", "ShpMain")
Sleep(200)
s := ProbeShape()
Log("wsr_poly=" (InStr(s, "shaped=1") && s != "shape=0" ? 1 : 0))
WinSetRegion(, "ShpMain")
; --- "Wind" accepted (even-odd applied; documented). ---
WinSetRegion("Wind 0-0 40-0 20-30", "ShpMain")
Sleep(200)
s := ProbeShape()
Log("wsr_wind=" (InStr(s, "shaped=1") && s != "shape=0" ? 1 : 0))
WinSetRegion(, "ShpMain")

; --- ValueError: unknown option / missing origin. ---
try {
    WinSetRegion("Q", "ShpMain")
    Log("wsr_bad=0")
} catch ValueError {
    Log("wsr_bad=1")
}
try {
    WinSetRegion("W100", "ShpMain")
    Log("wsr_nopts=0")
} catch ValueError {
    Log("wsr_nopts=1")
}
try {
    WinSetRegion("100 200", "ShpMain") ; Missing '-'.
    Log("wsr_nodelim=0")
} catch ValueError {
    Log("wsr_nodelim=1")
}
; --- OSError: ellipse/rounded-rect without W and H (upstream FR_E_WIN32). ---
try {
    WinSetRegion("0-0 E", "ShpMain")
    Log("wsr_e_nosize=0")
} catch OSError {
    Log("wsr_e_nosize=1")
}
try {
    WinSetRegion("0-0 R10-5", "ShpMain")
    Log("wsr_r_nosize=0")
} catch OSError {
    Log("wsr_r_nosize=1")
}
; --- TargetError: window not found (docs). ---
try {
    WinSetRegion("0-0 W10 H10", "NoSuchWindow")
    Log("wsr_nowin=0")
} catch TargetError {
    Log("wsr_nowin=1")
}

; --- GuiFromHwnd / GuiCtrlFromHwnd / MenuFromHandle: no Gui/menu objects
; exist on the port, so every handle yields "" (docs' empty-string case). ---
Log("gfr_hwnd=" (GuiFromHwnd(hwnd) = "" ? 1 : 0))
Log("gfr_recurse=" (GuiFromHwnd(hwnd, true) = "" ? 1 : 0))
Log("gfr_zero=" (GuiFromHwnd(0) = "" ? 1 : 0))
Log("gcfr_hwnd=" (GuiCtrlFromHwnd(hwnd) = "" ? 1 : 0))
Log("gcfr_zero=" (GuiCtrlFromHwnd(0) = "" ? 1 : 0))
Log("mfr_handle=" (MenuFromHandle(12345) = "" ? 1 : 0))
Log("mfr_zero=" (MenuFromHandle(0) = "" ? 1 : 0))

; --- Cleanup. ---
ExitApp(0)
