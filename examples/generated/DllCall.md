# DllCall

- Linux status: `IMPL` (P2)
- Example kind: `verified`
- Environment profile: `headless`
- Verified source: [tests/doccheck/assert_dllcall.ahk:6](../../tests/doccheck/assert_dllcall.ahk#L6)
- Profile command: `bash tests/doccheck/run_check.sh "$BIN"`
- Linux adaptation: Linux-native FFI uses .so/dlopen/libffi (maximum 64 ABI arguments); Windows .dll/user32-style specifications raise a migration error; Str/AStr are UTF-8 and WStr is UTF-16LE

Calls a function inside a DLL, such as a standard Windows API function.

## Syntax

````text
Result := DllCall("DllFile\Function" , Type1, Arg1, Type2, Arg2, "Cdecl ReturnType")
````

## Linux-verified example excerpt

This excerpt is executed by the profile command above; surrounding setup and assertions remain in the linked source.

````ahk

; --- Function-only form (searched in the process / all loaded objects) ---
MsgBox "abs=" (DllCall("abs", "Int", -42) = 42)
MsgBox "abs_pos=" (DllCall("abs", "Int", 7) = 7)
MsgBox "abs64=" (DllCall("labs", "Int64", -21474836480, "Int64") = 21474836480)
MsgBox "strlen=" (DllCall("strlen", "AStr", "hello") = 5)
````

## Upstream reference example

Source: [docs-v2/docs/lib/DllCall.htm](../../docs-v2/docs/lib/DllCall.htm)

````ahk
WhichButton := DllCall("MessageBox", "Int", 0, "Str", "Press Yes or No", "Str", "Title of box", "Int", 4)
MsgBox "You pressed button #" WhichButton
````
