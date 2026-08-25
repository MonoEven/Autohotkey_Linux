# IsTime

- Linux status: `IMPL` (P1)
- Example kind: `verified`
- Environment profile: `headless`
- Verified source: [tests/doccheck/assert_math.ahk:43](../../tests/doccheck/assert_math.ahk#L43)
- Profile command: `bash tests/doccheck/run_check.sh "$BIN"`

## Linux-verified example excerpt

This excerpt is executed by the profile command above; surrounding setup and assertions remain in the linked source.

````ahk
MsgBox "IsSpace=" IsSpace(" ")
MsgBox "IsXDigit=" IsXDigit("F")
MsgBox "IsTime=" IsTime("20240101120000")
MsgBox "Type_int=" Type(1)
MsgBox "Type_float=" Type(1.5)
MsgBox "Type_str=" Type("x")
````
