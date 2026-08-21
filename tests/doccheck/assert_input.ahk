; Input module doc-check (v2 docs: Send/SendEvent/SendInput/SendPlay/SendText,
; MouseMove/MouseClick/MouseClickDrag/MouseGetPos, Click, KeyWait, BlockInput,
; InstallKeybdHook/InstallMouseHook, SetCapsLockState/SetNumLockState/
; SetScrollLockState).  Runs under Xvfb (run_check.sh --xvfb) with the xkeycap
; capture client; output goes to a file (MsgBox would block with a display).
#Requires AutoHotkey v2.0

WINOUT := "/tmp/ahk_dc_input_out.txt"
FileDelete(WINOUT)
Log(line) => FileAppend(line "`n", WINOUT)

KEYCAP := "/tmp/ahk_dc_keycap.txt"
FileDelete(KEYCAP)
prev_bytes := 0

Run('out/xkeycap -out ' KEYCAP)
WinWait("KeyCap Capture",, 5)
Sleep(200)

; Read the lines appended to the capture file since the last call.
next_lines() {
    global prev_bytes
    f := FileOpen(KEYCAP, "r")
    f.Seek(prev_bytes)
    rest := f.Read()
    f.Close()
    prev_bytes := FileGetSize(KEYCAP)
    return StrSplit(rest, "`n")
}

; "name1,name2,..." of key-down events.
downs(lines) {
    out := ""
    for l in lines {
        if l = ""
            continue
        p := StrSplit(l, ":")
        if p.Length >= 3 && p[1] = "k" && p[2] = "down"
            out .= (out = "" ? "" : ",") p[3]
    }
    return out
}

; "down:N,up:N,..." of button events.
btns(lines) {
    out := ""
    for l in lines {
        if l = ""
            continue
        p := StrSplit(l, ":")
        if p.Length >= 3 && p[1] = "b"
            out .= (out = "" ? "" : ",") p[2] ":" p[3]
    }
    return out
}

; --- Send: literal text (each char: down + up). ---
Send("hello")
Sleep(60)
Log("send_text=" (downs(next_lines()) = "h,e,l,l,o" ? 1 : 0))

; --- Send: shifted character (Shift held, keysym resolves to "A"). ---
Send("A")
Sleep(60)
Log("send_shift=" (downs(next_lines()) = "Shift_L,A" ? 1 : 0))

; --- Send: special key + modifiers + explicit hold + repeat. ---
Send("{Enter}")
Sleep(60)
Log("send_enter=" (downs(next_lines()) = "Return" ? 1 : 0))
Send("^a")
Sleep(60)
Log("send_ctrla=" (downs(next_lines()) = "Control_L,a" ? 1 : 0))
Send("{Ctrl down}c{Ctrl up}")
Sleep(60)
Log("send_hold=" (downs(next_lines()) = "Control_L,c" ? 1 : 0))
Send("{Enter 3}")
Sleep(60)
Log("send_repeat=" (downs(next_lines()) = "Return,Return,Return" ? 1 : 0))

; --- SendText: everything literal (braces included). ---
SendText("a{bc")
Sleep(60)
Log("sendtext=" (downs(next_lines()) = "a,Shift_L,braceleft,b,c" ? 1 : 0))

; --- SendText/Send: non-ASCII (Unicode keysym transmission). ---
; CJK: no keycode produces these on a US layout, so the send engine borrows a
; spare keycode (transient XChangeKeyboardMapping) and delivers the Unicode
; keysym; xkeycap refreshes its map on MappingNotify and reports U+4F60/U+597D.
SendText("你好")
Sleep(80)
Log("sendtext_unicode_cjk=" (downs(next_lines()) = "U4F60,U597D" ? 1 : 0))
; Latin-1 supplement: XK_eacute (0x00E9) via the same borrowed-keycode path.
SendText("é")
Sleep(80)
Log("sendtext_unicode_latin1=" (downs(next_lines()) = "eacute" ? 1 : 0))
; The borrowed keycode must be reverted: a plain char still resolves to "b".
Send("b")
Sleep(80)
Log("sendtext_after_unicode=" (downs(next_lines()) = "b" ? 1 : 0))

; --- SendMode variants: all deliver XTEST events on Linux. ---
SendEvent("x")
Sleep(60)
Log("sendevent=" (downs(next_lines()) = "x" ? 1 : 0))
SendInput("y")
Sleep(60)
Log("sendinput=" (downs(next_lines()) = "y" ? 1 : 0))
SendPlay("z")
Sleep(60)
Log("sendplay=" (downs(next_lines()) = "z" ? 1 : 0))

; --- §2-B (check_detail0821): SendEvent pacing via SetKeyDelay/PressDuration.
; xkeycap records relative arrival timestamps (k:...:t:<rel-ms>), so the
; inter-key gaps and the down->up gap are measurable. ---
; Relative ms of every key event (down and up), in arrival order.
all_key_times(lines) {
    out := []
    for l in lines {
        if l = ""
            continue
        p := StrSplit(l, ":")
        ; k:down:<name>:mods:<state>:t:<rel-ms>
        if p.Length >= 7 && p[1] = "k" && p[6] = "t"
            out.Push(Integer(p[7]))
    }
    return out
}
; SetKeyDelay 50: consecutive key-downs must be >=45ms apart.
SetKeyDelay(50)
SendEvent("abc")
Sleep(400)
ts := all_key_times(next_lines())
; ts = [a-down, a-up, b-down, b-up, c-down, c-up]; downs at indices 1,3,5.
gap_ok := 0
if ts.Length >= 5
    gap_ok := (ts[3] - ts[1] >= 45 && ts[5] - ts[3] >= 45 ? 1 : 0)
Log("sendevent_delay50=" gap_ok)
; PressDuration 50 (SetKeyDelay 0,50): down->up gap must be >=45ms.
SetKeyDelay(0, 50)
SendEvent("q")
Sleep(400)
ts := all_key_times(next_lines())
press_ok := 0
if ts.Length >= 2
    press_ok := (ts[2] - ts[1] >= 45 ? 1 : 0)
Log("sendevent_press50=" press_ok)
SetKeyDelay(-1)

; --- §2-B: SendInput is a fast batch (<200ms for 100 keys) and delivers
; every key.  (SendInput ignores SetKeyDelay, so no pacing sleeps.) ---
s100 := ""
Loop 100
    s100 .= "a"
t0 := A_TickCount
SendInput(s100)
dt := A_TickCount - t0
Sleep(100)
Log("sendinput_100_fast=" (dt < 200 ? 1 : 0))
exp100 := ""
Loop 100
    exp100 .= (exp100 = "" ? "" : ",") "a"
Log("sendinput_100_keys=" (downs(next_lines()) = exp100 ? 1 : 0))

; --- §2-B: SendInput must not re-fire the script's own hotkeys (Windows
; unloads the hook during SendInput).  SendEvent of the same key DOES fire
; the hotkey (Windows SendEvent can trigger).  Note: a grabbed key that
; SendInput injects is consumed, not delivered to the target -- on X11 the
; passive grab intercepts every press of it (same limitation as `~`
; passthrough on servers that re-activate the grab; documented in
; core_hotkey_linux.cpp). ---
self_fired := 0
HotkeyXCB(ThisHotkey) {   ; named fn: hotkey threads need `global` inside.
    global self_fired
    self_fired++
}
Hotkey("x", HotkeyXCB)
Sleep(150)   ; let the grab install + reconcile run
SendInput("x")
Sleep(150)
Log("sendinput_self_no_fire=" (self_fired = 0 ? 1 : 0))
SendEvent("x")
Sleep(150)
Log("sendevent_self_fire=" (self_fired = 1 ? 1 : 0))
Hotkey("x", "Off")

; --- MouseMove + MouseGetPos. ---
MouseMove(300, 200)
Sleep(50)
MouseGetPos(&mx, &my)
Log("mousemove=" (mx = 300 && my = 200 ? 1 : 0))

; --- MouseClick. ---
MouseClick("Left", 100, 100)
Sleep(80)
Log("mouseclick_btns=" (btns(next_lines()) = "down:1,up:1" ? 1 : 0))
MouseGetPos(&mx, &my)
Log("mouseclick_pos=" (mx = 100 && my = 100 ? 1 : 0))
; Docs: ClickCount.
MouseClick("Right", 200, 150, 2)
Sleep(100)
Log("mouseclick_count=" (btns(next_lines()) = "down:3,up:3,down:3,up:3" ? 1 : 0))
; Docs: DownOrUp "D" holds the button.
MouseClick("Left", 50, 50, 1, , "D")
Sleep(50)
Log("mouseclick_down=" (btns(next_lines()) = "down:1" ? 1 : 0))
MouseClick("Left", 50, 50, 1, , "U")
Sleep(50)
Log("mouseclick_up=" (btns(next_lines()) = "up:1" ? 1 : 0))

; --- MouseClickDrag. ---
MouseClickDrag("Left", 10, 10, 120, 90)
Sleep(80)
Log("mousedrag_btns=" (btns(next_lines()) = "down:1,up:1" ? 1 : 0))
MouseGetPos(&mx, &my)
Log("mousedrag_pos=" (mx = 120 && my = 90 ? 1 : 0))

; --- Click (g_BIF). ---
Click("Right")
Sleep(80)
Log("click_right=" (btns(next_lines()) = "down:3,up:3" ? 1 : 0))
Click("150 160")
Sleep(80)
Log("click_xy=" (btns(next_lines()) = "down:1,up:1" ? 1 : 0))
MouseGetPos(&mx, &my)
Log("click_pos=" (mx = 150 && my = 160 ? 1 : 0))

; --- MouseGetPos WhichWindow: pointer over the capture window. ---
MouseMove(150, 160)
Sleep(50)
MouseGetPos(, , &whwnd)
Log("mousegetpos_win=" (WinGetTitle("ahk_id " whwnd) = "KeyCap Capture" ? 1 : 0))

; --- KeyWait (docs: 1 when the condition is met; "D" waits for down). ---
Log("keywait_up=" (KeyWait("a") = 1 ? 1 : 0))
Send("{a down}")
Sleep(50)
Log("keywait_d=" (KeyWait("a", "D") = 1 ? 1 : 0))
Send("{a up}")
Sleep(50)
Log("keywait_up2=" (KeyWait("a") = 1 ? 1 : 0))

; --- SetCapsLockState/SetNumLockState/SetScrollLockState + GetKeyState "T". ---
SetCapsLockState("On")
Log("caps_on=" (GetKeyState("CapsLock", "T") = 1 ? 1 : 0))
SetCapsLockState(-1)
Log("caps_toggle=" (GetKeyState("CapsLock", "T") = 0 ? 1 : 0))
SetCapsLockState(1)
Log("caps_num=" (GetKeyState("CapsLock", "T") = 1 ? 1 : 0))
SetCapsLockState("Off")
Log("caps_off=" (GetKeyState("CapsLock", "T") = 0 ? 1 : 0))
SetNumLockState("On")
Log("num_on=" (GetKeyState("NumLock", "T") = 1 ? 1 : 0))
SetNumLockState("Off")
Log("num_off=" (GetKeyState("NumLock", "T") = 0 ? 1 : 0))
SetScrollLockState("On")
Log("scroll_on=" (GetKeyState("ScrollLock", "T") = 1 ? 1 : 0))
SetScrollLockState("Off")
Log("scroll_off=" (GetKeyState("ScrollLock", "T") = 0 ? 1 : 0))

; --- BlockInput (docs: On/Off mode blocks all user input; while blocked,
; simulated input does not reach other clients, but the script can still
; simulate it; Default turns off the Send/Mouse modes without unblocking
; On/Off; input is re-enabled when the script closes). ---
; Drain capture events left by the KeyWait section (its checks read the
; keyboard state, not the capture file).
next_lines()
BlockInput("On")
Send("{Enter}")
Sleep(60)
Log("block_send=" (downs(next_lines()) = "" ? 1 : 0))
BlockInput("Default")
Send("{Enter}")
Sleep(60)
Log("block_default=" (downs(next_lines()) = "" ? 1 : 0))
BlockInput("Off")
Send("{Enter}")
Sleep(60)
Log("block_off=" (downs(next_lines()) = "Return" ? 1 : 0))

; --- InstallKeybdHook/InstallMouseHook: no error, flags stored. ---
InstallKeybdHook()
InstallMouseHook()
InstallKeybdHook(0)
InstallMouseHook(0)
Log("install_hooks=1")

; --- GetKeyState logical/physical state follows XTEST events. ---
Send("{b down}")
Sleep(50)
Log("keystate_down=" (GetKeyState("b") = 1 ? 1 : 0))
Log("keystate_phys=" (GetKeyState("b", "P") = 1 ? 1 : 0))
Send("{b up}")
Sleep(50)
Log("keystate_up=" (GetKeyState("b") = 0 ? 1 : 0))

; --- Cleanup. ---
Run("pkill -f xkeycap")
; A hotkey was registered above, which makes the script persistent; exit
; explicitly so the runner sees a clean termination.
ExitApp
