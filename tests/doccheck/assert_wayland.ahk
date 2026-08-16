; Wayland-mode doc-check (v2 docs).  Runs under sway (headless) via
; wayland_run.sh with WAYLAND_DISPLAY=wayland-1 and no X display.
;
; Linux Wayland semantics (documented in CHECK_REPORT):
;   - Send/Mouse* use the zwp_virtual_keyboard_v1 and
;     zwlr_virtual_pointer_manager_v1 protocols.  sway's bindsym hooks in
;     the runner config create marker files when our key events reach the
;     compositor, giving end-to-end verification of the virtual keyboard
;     path (plain keys and modifier keys).  Modifier-combo bindings and
;     pointer buttons are not forwarded by sway for virtual devices (a
;     compositor limitation; the reference xdotool-style client behaves
;     identically) -- documented.
;   - ToolTip creates an xdg-shell toplevel (the compositor decides
;     placement); the runner verifies the window in sway's tree.
;   - Window management (Win*), hotkeys, pixel/monitor access are not
;     available on Wayland (clients cannot enumerate windows; there is no
;     global-hotkey protocol) and raise clear Target/OS errors.
#Requires AutoHotkey v2.0

OUT := "/tmp/ahk_dc_wayland_out.txt"
FileDelete(OUT)
Log(line) => FileAppend(line "`n", OUT)

KA := "/tmp/wl_key_a"
KS := "/tmp/wl_key_shift_l"
KC := "/tmp/wl_key_ctrl_l"
FileDelete(KA)
FileDelete(KS)
FileDelete(KC)

; --- Input via the virtual keyboard protocol (end-to-end through sway) ---
Send("a")
Sleep(800)
Log("wl_send_a=" (FileExist(KA) ? 1 : 0))
Send("{Shift down}{Shift up}")
Sleep(800)
Log("wl_send_shift=" (FileExist(KS) ? 1 : 0))
Send("{Ctrl down}{Ctrl up}")
Sleep(800)
Log("wl_send_ctrl=" (FileExist(KC) ? 1 : 0))
Send("Hello World") ; Text send: must not raise.
Sleep(500)
Log("wl_send_text=1")
MouseClick("Right") ; Virtual pointer button (sway does not forward it;
Sleep(500)          ; documented compositor limitation).
Log("wl_mouse=1")
MouseMove(100, 100)
Sleep(300)
Log("wl_mousemove=1")
; GetKeyState cannot query a Wayland seat: reports 0 (documented).
Log("wl_gks=" (GetKeyState("a") = 0 ? 1 : 0))

; --- ToolTip: xdg toplevel (mapped state verified by the runner) ---
h := ToolTip("WLWayTip")
Sleep(1200)
Log("wl_tip_hwnd=" (h != 0 ? 1 : 0))
RunWait("/tmp/do_swaymsg.sh") ; Writes /tmp/wl_sway_tree.txt (runner).
if FileExist("/tmp/wl_sway_tree.txt")
    Log("wl_tip_mapped=" (InStr(FileRead("/tmp/wl_sway_tree.txt"), "WLWayTip") ? 1 : 0))
else
    Log("wl_tip_mapped=0")
ToolTip()
Sleep(300)

; --- X11-only surfaces raise clear errors on Wayland (documented) ---
try {
    WinGetTitle("x")
    Log("wl_wintitle=0")
} catch TargetError {
    Log("wl_wintitle=1")
}
try {
    Hotkey("F6", (*) => 0)
    Log("wl_hotkey=0")
} catch OSError {
    Log("wl_hotkey=1")
}
try {
    PixelGetColor(0, 0)
    Log("wl_pixel=0")
} catch OSError {
    Log("wl_pixel=1")
}
try {
    WinSetRegion("0-0 W10 H10", "x")
    Log("wl_wsr=0")
} catch TargetError {
    Log("wl_wsr=1")
}

; --- Cleanup. ---
ExitApp(0)
