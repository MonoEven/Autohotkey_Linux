# Ord

- Linux status: `IMPL` (P1)
- Example kind: `verified`
- Environment profile: `headless`
- Verified source: [tests/doccheck/assert_dllcall.ahk:12](../../tests/doccheck/assert_dllcall.ahk#L12)
- Profile command: `bash tests/doccheck/run_check.sh "$BIN"`

## Additional verified environments

- `headless`: [tests/doccheck/assert_string.ahk:42](../../tests/doccheck/assert_string.ahk#L42)
- `x11`: [tests/doccheck/assert_clipboard_all.ahk:126](../../tests/doccheck/assert_clipboard_all.ahk#L126)

Returns the ordinal value (numeric character code) of the first character in the specified string.

## Syntax

````text
Number := Ord(String)
````

## Linux-verified example excerpt

This excerpt is executed by the profile command above; surrounding setup and assertions remain in the linked source.

````ahk
MsgBox "strlen_utf8=" (DllCall("strlen", "AStr", "你好") = 6)
MsgBox "strlen_empty=" (DllCall("strlen", "AStr", "") = 0)
MsgBox "isdigit=" (DllCall("isdigit", "Int", Ord("5")) != 0)
MsgBox "isalpha=" (DllCall("isalpha", "Int", Ord("x")) != 0)

; --- DllFile & Function form (dlopen candidates: libX.so etc.) ---
````

## Upstream reference example

Source: [docs-v2/docs/lib/Ord.htm](../../docs-v2/docs/lib/Ord.htm)

````ahk
MsgBox Ord("t")
MsgBox Ord("test")
````
