# A_ParityLevel

- Linux status: `IMPL` (P1)
- Example kind: `verified`
- Environment profile: `headless`
- Verified source: [tests/doccheck/assert_parity.ahk:28](../../tests/doccheck/assert_parity.ahk#L28)
- Profile command: `bash tests/doccheck/run_check.sh "$BIN"`

## Additional verified environments

- `headless`: [tests/doccheck/assert_strict.ahk:18](../../tests/doccheck/assert_strict.ahk#L18)

## Linux-verified example excerpt

This excerpt is executed by the profile command above; surrounding setup and assertions remain in the linked source.

````ahk

; --- A_ParityLevel(FuncName) in-script ---
MsgBox "parity_level_p4=" (A_ParityLevel("ComObjArray") = 4 ? 1 : 0)
MsgBox "parity_level_p2=" (A_ParityLevel("SendPlay") = 2 ? 1 : 0)
MsgBox "parity_level_p3=" (A_ParityLevel("RegRead") = 3 ? 1 : 0)
MsgBox "parity_level_p1=" (A_ParityLevel("MsgBox") = 1 ? 1 : 0)
````
