# Sin

- Linux status: `IMPL` (P1)
- Example kind: `verified`
- Environment profile: `headless`
- Verified source: [tests/doccheck/assert_math.ahk:23](../../tests/doccheck/assert_math.ahk#L23)
- Profile command: `bash tests/doccheck/run_check.sh "$BIN"`

## Linux-verified example excerpt

This excerpt is executed by the profile command above; surrounding setup and assertions remain in the linked source.

````ahk
MsgBox "Ln=" Ln(1)
MsgBox "Log=" Log(100)
MsgBox "Sin=" Sin(0)
MsgBox "Cos=" Cos(0)
MsgBox "Tan=" Tan(0)
MsgBox "ASin=" ASin(0)
````
