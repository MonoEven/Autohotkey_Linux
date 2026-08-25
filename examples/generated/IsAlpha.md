# IsAlpha

- Linux status: `IMPL` (P1)
- Example kind: `verified`
- Environment profile: `headless`
- Verified source: [tests/doccheck/assert_math.ahk:38](../../tests/doccheck/assert_math.ahk#L38)
- Profile command: `bash tests/doccheck/run_check.sh "$BIN"`

## Linux-verified example excerpt

This excerpt is executed by the profile command above; surrounding setup and assertions remain in the linked source.

````ahk
MsgBox "IsAlnum=" IsAlnum("A")
MsgBox "IsDigit=" IsDigit("5")
MsgBox "IsAlpha=" IsAlpha("a")
MsgBox "IsUpper=" IsUpper("A")
MsgBox "IsLower=" IsLower("a")
MsgBox "IsSpace=" IsSpace(" ")
````
