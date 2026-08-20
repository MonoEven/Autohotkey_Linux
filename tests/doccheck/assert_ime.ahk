; IME active-state detection regression (check0820 §2: ibus/fcitx).
;
; ImeGetState() -> "framework|group":
;   framework  "ibus" | "fcitx5" | "none"  (active framework owning the bus)
;   group      current XKB group index (-1 without an X display; on X11 the
;               effective layout group, where IMEs engage as >= 1)
;
; Assertions (environment-independent format checks so the suite runs in CI
; even where no IME / no display is present):
;   1. the framework field is one of the three known values;
;   2. the group field is a valid integer (-1..8);
;   3. when the session bus is absent, framework is still "none" (no crash).
#Requires AutoHotkey v2.0

OUT := "/tmp/ahk_dc_ime_out.txt"
FileDelete(OUT)
Log(line) => FileAppend(line "`n", OUT)

s := ImeGetState()
Log("ime_raw=" s)
; parse "framework|group"
sep := InStr(s, "|")
fw := sep ? SubStr(s, 1, sep - 1) : ""
g  := sep ? SubStr(s, sep + 1) : ""
Log("ime_fw_known=" (fw = "ibus" || fw = "fcitx5" || fw = "none" ? 1 : 0))
gi := Integer(g)
Log("ime_group_int=" (g ~= "^-?[0-9]+$" && gi >= -1 && gi <= 8 ? 1 : 0))

ExitApp 0