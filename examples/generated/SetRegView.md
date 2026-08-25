# SetRegView

- Linux status: `IMPL` (P1)
- Example kind: `verified`
- Environment profile: `headless`
- Verified source: [tests/doccheck/assert_sys.ahk:117](../../tests/doccheck/assert_sys.ahk#L117)
- Profile command: `bash tests/doccheck/run_check.sh "$BIN"`

Sets the registry view used by RegRead, RegWrite, RegDelete, RegDeleteKey and Loop Reg, allowing them in a 32-bit script to access the 64-bit registry view and vice versa.

## Syntax

````text
PrevRegView := SetRegView(RegView)
````

## Linux-verified example excerpt

This excerpt is executed by the profile command above; surrounding setup and assertions remain in the linked source.

````ahk

; Docs: SetRegView returns the previous setting ("Default"/"32"/"64").
MsgBox "RegView_prev=" (SetRegView("64") = "Default")
MsgBox "RegView_set=" (A_RegView = "64")
MsgBox "RegView_return=" (SetRegView("Default") = "64")
try
````

## Upstream reference example

Source: [docs-v2/docs/lib/SetRegView.htm](../../docs-v2/docs/lib/SetRegView.htm)

````ahk
; Access the registry as a 32-bit application would.
SetRegView 32
RegWrite "REG_SZ", "HKLM\SOFTWARE\Test.ahk", "Value", 123
; Access the registry as a 64-bit application would.
SetRegView 64
value := RegRead("HKLM\SOFTWARE\Wow6432Node\Test.ahk", "Value")
RegDelete "HKLM\SOFTWARE\Wow6432
````
