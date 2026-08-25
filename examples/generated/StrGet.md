# StrGet

- Linux status: `IMPL` (P1)
- Example kind: `verified`
- Environment profile: `headless`
- Verified source: [tests/doccheck/assert_dllcall.ahk:59](../../tests/doccheck/assert_dllcall.ahk#L59)
- Profile command: `bash tests/doccheck/run_check.sh "$BIN"`

## Additional verified environments

- `headless`: [tests/doccheck/assert_interop.ahk:35](../../tests/doccheck/assert_interop.ahk#L35)

Copies a string from a memory address or buffer, optionally converting it from a given code page.

## Syntax

````text
String := StrGet(Source , Length, Encoding) String := StrGet(Source , Encoding)
````

## Linux-verified example excerpt

This excerpt is executed by the profile command above; surrounding setup and assertions remain in the linked source.

````ahk
b := Buffer(8)
DllCall("sprintf", "Ptr", b.Ptr, "AStr", "%d-%d", "Int", 3, "Int", 4)
MsgBox "out_buf=" (StrGet(b, 3, "UTF-8") = "3-4")
; --- Failure cases ---
try
    DllCall("no_such_function_xyz")
````

## Upstream reference example

Source: [docs-v2/docs/lib/StrGet.htm](../../docs-v2/docs/lib/StrGet.htm)

````ahk
str := StrGet(address, "cp0")  ; Code page 0, unspecified length
str := StrGet(address, n, 0)   ; Maximum n chars, code page 0
str := StrGet(address, 0)      ; Maximum 0 chars (always blank)
````
