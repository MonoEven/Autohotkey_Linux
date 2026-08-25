# ImeGetState

- Linux status: `IMPL` (P2)
- Example kind: `verified`
- Environment profile: `desktop-session`
- Verified source: [tests/doccheck/assert_ime.ahk:19](../../tests/doccheck/assert_ime.ahk#L19)
- Profile command: `bash tests/oracle/run_gui_host_matrix.sh "$BIN"`
- Linux adaptation: Linux extension: reports IBus/Fcitx framework ownership plus XKB group; no Windows v2 counterpart

## Linux-verified example excerpt

This excerpt is executed by the profile command above; surrounding setup and assertions remain in the linked source.

````ahk
Log(line) => FileAppend(line "`n", OUT)

s := ImeGetState()
Log("ime_raw=" s)
; parse "framework|group"
sep := InStr(s, "|")
````
