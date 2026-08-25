# IsNumber

- Linux status: `IMPL` (P1)
- Example kind: `verified`
- Environment profile: `headless`
- Verified source: [tests/doccheck/assert_math.ahk:32](../../tests/doccheck/assert_math.ahk#L32)
- Profile command: `bash tests/doccheck/run_check.sh "$BIN"`

## Linux-verified example excerpt

This excerpt is executed by the profile command above; surrounding setup and assertions remain in the linked source.

````ahk
MsgBox "Random_in_range=" (Random(1, 10) >= 1 && Random(1, 10) <= 10)
MsgBox "Random_seed=" (Random(5, 5))
MsgBox "IsNumber=" IsNumber("3.5")
MsgBox "IsInteger=" IsInteger(3)
MsgBox "IsInteger_float=" IsInteger(3.0)
MsgBox "IsFloat=" IsFloat(3.5)
````
