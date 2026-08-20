; Wayland-mode doc-check (v2 docs).  Runs under sway (headless) via
; wayland_run.sh with WAYLAND_DISPLAY=wayland-1 and no X display.
;
; Linux Wayland semantics (documented in CHECK_REPORT):
;   - Send/Mouse* use the zwp_virtual_keyboard_v1 and
;     zwlr_virtual_pointer_manager_v1 protocols.  sway's bindsym hooks in
;     the runner config create marker files when our key events reach the
;     compositor, giving end-to-end verification of the virtual keyboard
;     path.  The virtual keyboard's modifier state is pushed explicitly
;     (zwp_virtual_keyboard_v1.modifiers) before each key event, so sway's
;     combo matching sees Shift/Control exactly as a physical keyboard
;     would: modifier-combo bindings (Shift+Return, Control+Return) fire.
;     Note that binding a modifier key alone (bindsym Shift_L) never fires
;     in sway -- when the modifier key is pressed its xkb modifier bit is
;     already set, so the mods=0 binding does not match; this is the same
;     for a physical keyboard, and is not a limitation of the virtual
;     device path.  Pointer buttons require the pointer to hover a surface
;     (we move it onto the ToolTip toplevel first); sway then delivers the
;     button event to its bindsym handlers.
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
KSR := "/tmp/wl_key_sr"
KCR := "/tmp/wl_key_cr"
FileDelete(KA)
FileDelete(KSR)
FileDelete(KCR)

; --- Input via the virtual keyboard protocol (end-to-end through sway) ---
Send("a")
Sleep(800)
Log("wl_send_a=" (FileExist(KA) ? 1 : 0))
; Modifier-combo bindings: need the virtual keyboard's modifier state to
; be pushed to the compositor (see LinuxWaylandKeyEvent).
Send("+{Return}")
Sleep(800)
Log("wl_send_shift_enter=" (FileExist(KSR) ? 1 : 0))
Send("^{Return}")
Sleep(800)
Log("wl_send_ctrl_enter=" (FileExist(KCR) ? 1 : 0))
Send("Hello World") ; Text send: must not raise.
Sleep(500)
Log("wl_send_text=1")
; Non-ASCII SendText on pure Wayland: the clipboard-paste fallback (set
; clipboard -> Ctrl+V -> restore).  sway's Control_L+v bindsym marker
; proves the paste key sequence reached the compositor; the restore is
; verified through A_Clipboard.
A_Clipboard := "wl-saved-clip"
SendText("你")
Sleep(1200)
Log("wl_unicode_paste=" (FileExist("/tmp/wl_key_cv") ? 1 : 0))
Log("wl_unicode_restore=" (A_Clipboard = "wl-saved-clip" ? 1 : 0))
; check0820 P1: when the clipboard was EMPTY, the paste fallback must
; restore empty (never leave the sentinel text behind).
A_Clipboard := ""
Sleep(200)
SendText("好")
Sleep(1200)
Log("wl_unicode_empty_paste=" (FileExist("/tmp/wl_key_cv") ? 1 : 0))
Log("wl_unicode_empty_restore=" (A_Clipboard = "" ? 1 : 0))
; GetKeyState cannot query a Wayland seat: reports 0 (documented).
Log("wl_gks=" (GetKeyState("a") = 0 ? 1 : 0))
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
; Virtual pointer: move onto the toplevel, then click.  sway's button3
; bindsym fires only while the pointer hovers a surface, so the ToolTip
; window is both the pointer target and the surface the click lands on.
MouseMove(640, 360)
Sleep(600)
MouseClick("Right")
Sleep(800)
Log("wl_mouse_btn=" (FileExist("/tmp/wl_btn3") ? 1 : 0))
MouseMove(100, 100)
Sleep(300)
Log("wl_mousemove=1")
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
