#Requires AutoHotkey v2.0
; Holder for the multiscript_conflict scenario: registers F7 + stays alive +
; signals readiness so the main script cannot race it.
Hotkey("F7", (*) => 0)
FileAppend("ready`n", "/tmp/scn_ms_holder_ready")
Sleep 8000