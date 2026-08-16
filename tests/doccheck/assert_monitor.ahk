; Screen-module doc-check (v2 docs: MonitorGet/GetCount/GetName/GetPrimary/
; GetWorkArea + PixelGetColor/PixelSearch).  Runs under Xvfb (run_check.sh
; --xvfb): XRandR reports one 1024x768 output named "screen"; the xwin_helper
; paints two solid-color rectangles for the pixel assertions; output goes to
; a file (MsgBox would block with a display).
#Requires AutoHotkey v2.0

WINOUT := "/tmp/ahk_dc_monitor_out.txt"
FileDelete(WINOUT)
Log(line) => FileAppend(line "`n", WINOUT)

Run('out/xwin_helper -title MonWin -class DocCheck -x 100 -y 100 -w 300 -h 200'
    ' -fill 336699 10 10 100 100 -fill 3367A9 130 10 100 100')
WinWait("MonWin",, 5)
Sleep(300)

; --- Monitor* (XRandR: one output "screen", 1024x768). ---
Log("mon_count=" (MonitorGetCount() = 1 ? 1 : 0))
Log("mon_primary=" (MonitorGetPrimary() = 1 ? 1 : 0))
Log("mon_name=" (MonitorGetName(1) = "screen" ? 1 : 0))
MonitorGet(1, &ml, &mt, &mr, &mb)
Log("mon_get=" (ml = 0 && mt = 0 && mr = 1024 && mb = 768 ? 1 : 0))
Log("mon_get_ret=" (MonitorGet(1) = 1 ? 1 : 0))
MonitorGet(, &ml2, &mt2, &mr2, &mb2) ; N omitted -> primary monitor.
Log("mon_get_primary=" (ml2 = 0 && mt2 = 0 && mr2 = 1024 && mb2 = 768 ? 1 : 0))
MonitorGetWorkArea(1, &wl, &wt, &wr, &wb)
Log("mon_workarea=" (wl = 0 && wt = 0 && wr = 1024 && wb = 768 ? 1 : 0))
try
    MonitorGet(2)
catch ValueError
    Log("mon_get_err=1")
try
    MonitorGetName(2)
catch ValueError
    Log("mon_name_err=1")

; --- PixelGetColor (docs: returns a hexadecimal numeric string). ---
Log("pix_black=" (PixelGetColor(5, 5) = "0x000000" ? 1 : 0))
Log("pix_fill=" (PixelGetColor(150, 150) = "0x336699" ? 1 : 0))
Log("pix_fill2=" (PixelGetColor(250, 150) = "0x3367A9" ? 1 : 0))

; --- PixelSearch (docs: 1/0; outputs blank when not found; variation). ---
found := PixelSearch(&px, &py, 140, 140, 160, 160, 0x336699)
Log("search_found=" (found = 1 && px = 140 && py = 140 ? 1 : 0))
found2 := PixelSearch(&px2, &py2, 0, 0, 50, 50, 0x336699)
Log("search_miss=" (found2 = 0 && px2 = "" && py2 = "" ? 1 : 0))
; 0x3367A9 vs 0x336699: green differs by 1, blue by 16.
v1 := PixelSearch(&vx1, &vy1, 240, 130, 320, 200, 0x336699, 15)
Log("search_var_lo=" (v1 = 0 ? 1 : 0))
v2 := PixelSearch(&vx2, &vy2, 240, 130, 320, 200, 0x336699, 16)
Log("search_var_hi=" (v2 = 1 ? 1 : 0))
; Reversed corner order: search starts at (X2,Y2) in this case.
v3 := PixelSearch(&vx3, &vy3, 320, 200, 240, 130, 0x3367A9)
Log("search_rev=" (v3 = 1 && vx3 = 320 && vy3 = 200 ? 1 : 0))

; --- CoordMode Pixel: CLIENT = relative to the active window's client area. ---
CoordMode("Pixel", "Client")
WinActivate("MonWin")
Sleep(100)
Log("pix_client=" (PixelGetColor(55, 55) = "0x336699" ? 1 : 0))
CoordMode("Pixel", "Screen")

; --- Cleanup. ---
Run("pkill -f xwin_helper")
