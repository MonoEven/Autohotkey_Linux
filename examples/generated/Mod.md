# Mod

- Linux status: `IMPL` (P1)
- Example kind: `verified`
- Environment profile: `headless`
- Verified source: [tests/doccheck/assert_math.ahk:9](../../tests/doccheck/assert_math.ahk#L9)
- Profile command: `bash tests/doccheck/run_check.sh "$BIN"`

## Additional verified environments

- `headless`: [tests/doccheck/assert_statements.ahk:56](../../tests/doccheck/assert_statements.ahk#L56)

## Linux-verified example excerpt

This excerpt is executed by the profile command above; surrounding setup and assertions remain in the linked source.

````ahk
MsgBox "Sqrt=" Sqrt(16)
MsgBox "Sqrt2=" (Sqrt(2) > 1.4)
MsgBox "Mod_pos=" Mod(7, 3)
MsgBox "Mod_neg=" Mod(-7, 3)      ; result takes the sign of the dividend
MsgBox "Mod_float=" Mod(7.5, 2)   ; supports floats
MsgBox "Round_int=" Round(3.14159, 2)
````
