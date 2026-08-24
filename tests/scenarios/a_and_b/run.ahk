#Requires AutoHotkey v2.0
; Scenario a_and_b: with the M3-M input mux, "a & b" custom combos route to the
; evdev lane when that lane can open devices (custom_combo=true); otherwise the
; registration must raise a clear backend error -- never a silent no-op.
WINOUT := "/tmp/scn_a_and_b"
FileDelete(WINOUT)
try {
    Hotkey("a & b", (*) => 0)
    r := HotkeyBackendGet("a & b")
    FileAppend("routed=" r.backend "`n", WINOUT)
} catch OSError as e {
    FileAppend("err:`n", WINOUT)
}
ExitApp
