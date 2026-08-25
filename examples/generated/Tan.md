# Tan

- Linux status: `IMPL` (P1)
- Example kind: `verified`
- Environment profile: `headless`
- Verified source: [tests/doccheck/assert_math.ahk:25](../../tests/doccheck/assert_math.ahk#L25)
- Profile command: `bash tests/doccheck/run_check.sh "$BIN"`

## Linux-verified example excerpt

This excerpt is executed by the profile command above; surrounding setup and assertions remain in the linked source.

````ahk
MsgBox "Sin=" Sin(0)
MsgBox "Cos=" Cos(0)
MsgBox "Tan=" Tan(0)
MsgBox "ASin=" ASin(0)
MsgBox "ACos=" ACos(1)
MsgBox "ATan=" ATan(0)
````
