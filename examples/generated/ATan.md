# ATan

- Linux status: `IMPL` (P1)
- Example kind: `verified`
- Environment profile: `headless`
- Verified source: [tests/doccheck/assert_math.ahk:28](../../tests/doccheck/assert_math.ahk#L28)
- Profile command: `bash tests/doccheck/run_check.sh "$BIN"`

## Linux-verified example excerpt

This excerpt is executed by the profile command above; surrounding setup and assertions remain in the linked source.

````ahk
MsgBox "ASin=" ASin(0)
MsgBox "ACos=" ACos(1)
MsgBox "ATan=" ATan(0)
MsgBox "ATan2=" ATan(0)
MsgBox "Random_in_range=" (Random(1, 10) >= 1 && Random(1, 10) <= 10)
MsgBox "Random_seed=" (Random(5, 5))
````
