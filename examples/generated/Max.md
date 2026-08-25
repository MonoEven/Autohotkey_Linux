# Max

- Linux status: `IMPL` (P1)
- Example kind: `verified`
- Environment profile: `headless`
- Verified source: [tests/doccheck/assert_math.ahk:18](../../tests/doccheck/assert_math.ahk#L18)
- Profile command: `bash tests/doccheck/run_check.sh "$BIN"`

## Linux-verified example excerpt

This excerpt is executed by the profile command above; surrounding setup and assertions remain in the linked source.

````ahk
MsgBox "Ceil=" Ceil(3.2)
MsgBox "Min=" Min(3, 1, 2)
MsgBox "Max=" Max(3, 1, 2)
MsgBox "Min_str=" Min("10", "9")  ; numeric strings compared numerically
MsgBox "Exp=" Exp(0)
MsgBox "Ln=" Ln(1)
````
