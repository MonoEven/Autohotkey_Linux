# Round

- Linux status: `IMPL` (P1)
- Example kind: `verified`
- Environment profile: `headless`
- Verified source: [tests/doccheck/assert_math.ahk:12](../../tests/doccheck/assert_math.ahk#L12)
- Profile command: `bash tests/doccheck/run_check.sh "$BIN"`

## Additional verified environments

- `headless`: [tests/doccheck/assert_statements.ahk:118](../../tests/doccheck/assert_statements.ahk#L118)

## Linux-verified example excerpt

This excerpt is executed by the profile command above; surrounding setup and assertions remain in the linked source.

````ahk
MsgBox "Mod_neg=" Mod(-7, 3)      ; result takes the sign of the dividend
MsgBox "Mod_float=" Mod(7.5, 2)   ; supports floats
MsgBox "Round_int=" Round(3.14159, 2)
MsgBox "Round_float=" Round(3.14159)
MsgBox "Round_neg=" Round(-3.6)
MsgBox "Floor=" Floor(3.7)
````
