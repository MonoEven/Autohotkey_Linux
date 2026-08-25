# ACos

- Linux status: `IMPL` (P1)
- Example kind: `verified`
- Environment profile: `headless`
- Verified source: [tests/doccheck/assert_math.ahk:27](../../tests/doccheck/assert_math.ahk#L27)
- Profile command: `bash tests/doccheck/run_check.sh "$BIN"`

## Linux-verified example excerpt

This excerpt is executed by the profile command above; surrounding setup and assertions remain in the linked source.

````ahk
MsgBox "Tan=" Tan(0)
MsgBox "ASin=" ASin(0)
MsgBox "ACos=" ACos(1)
MsgBox "ATan=" ATan(0)
MsgBox "ATan2=" ATan(0)
MsgBox "Random_in_range=" (Random(1, 10) >= 1 && Random(1, 10) <= 10)
````
