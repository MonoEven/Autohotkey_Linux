#Requires AutoHotkey v2.0
; ============================================================================
; inputd-style remap template (check_detail0821 §1-B / R4).
; Runs on the evdev/uinput lane (AHK_INPUT_BACKEND=evdev): physical keys are
; read from /dev/input/event*, the remaps below fire, and the mapped key is
; replayed through /dev/uinput so the compositor receives the remapped key.
; Needs: input group (read /dev/input) + writable /dev/uinput (see
; tools/linux/permissions/install_permissions.sh).
;
; The CapsLock dual-role (tap = CapsLock, hold = Esc) is the reference remap;
; add simple remaps as `FromKey::` hotkeys whose callback Sends the target.
; ============================================================================

; --- Dual-role: CapsLock tap = CapsLock, CapsLock hold = Esc ---
caps_down := 0
CapsLock::{
    global caps_down
    caps_down := A_TickCount
}
CapsLock up::{
    global caps_down
    if (A_TickCount - caps_down < 200)
        Send("{CapsLock}")   ; tap -> re-send CapsLock
    else
        Send("{Esc}")        ; hold -> Esc
}

; --- Simple remaps (examples; verify with uinput-inject + evtest) ---
; a::Send("{b}")            ; physical 'a' becomes 'b'
; F12::Send("{Media_Play_Pause}")

; --- Stay alive ---
SetTimer(() => ExitApp(), 0)
Persistent(True)