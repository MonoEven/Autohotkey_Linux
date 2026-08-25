# IsInteger

- Linux status: `IMPL` (P1)
- Example kind: `verified`
- Environment profile: `headless`
- Verified source: [tests/doccheck/assert_math.ahk:33](../../tests/doccheck/assert_math.ahk#L33)
- Profile command: `bash tests/doccheck/run_check.sh "$BIN"`

## Additional verified environments

- `host-tools`: [tests/doccheck/assert_sound_etc.ahk:26](../../tests/doccheck/assert_sound_etc.ahk#L26)

## Linux-verified example excerpt

This excerpt is executed by the profile command above; surrounding setup and assertions remain in the linked source.

````ahk
MsgBox "Random_seed=" (Random(5, 5))
MsgBox "IsNumber=" IsNumber("3.5")
MsgBox "IsInteger=" IsInteger(3)
MsgBox "IsInteger_float=" IsInteger(3.0)
MsgBox "IsFloat=" IsFloat(3.5)
MsgBox "IsAlnum=" IsAlnum("A")
````
