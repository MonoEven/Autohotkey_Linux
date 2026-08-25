# Abs

- Linux status: `IMPL` (P1)
- Example kind: `verified`
- Environment profile: `headless`
- Verified source: [tests/doccheck/assert_math.ahk:5](../../tests/doccheck/assert_math.ahk#L5)
- Profile command: `bash tests/doccheck/run_check.sh "$BIN"`

## Linux-verified example excerpt

This excerpt is executed by the profile command above; surrounding setup and assertions remain in the linked source.

````ahk
#Requires AutoHotkey v2.0

MsgBox "Abs_int=" Abs(-5)
MsgBox "Abs_float=" Abs(-5.5)
MsgBox "Sqrt=" Sqrt(16)
MsgBox "Sqrt2=" (Sqrt(2) > 1.4)
````
