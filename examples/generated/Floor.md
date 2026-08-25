# Floor

- Linux status: `IMPL` (P1)
- Example kind: `verified`
- Environment profile: `headless`
- Verified source: [tests/doccheck/assert_math.ahk:15](../../tests/doccheck/assert_math.ahk#L15)
- Profile command: `bash tests/doccheck/run_check.sh "$BIN"`

## Linux-verified example excerpt

This excerpt is executed by the profile command above; surrounding setup and assertions remain in the linked source.

````ahk
MsgBox "Round_float=" Round(3.14159)
MsgBox "Round_neg=" Round(-3.6)
MsgBox "Floor=" Floor(3.7)
MsgBox "Ceil=" Ceil(3.2)
MsgBox "Min=" Min(3, 1, 2)
MsgBox "Max=" Max(3, 1, 2)
````
