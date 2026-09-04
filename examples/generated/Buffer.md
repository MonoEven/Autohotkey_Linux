# Buffer

- Linux status: `IMPL` (P1)
- Example kind: `verified`
- Environment profile: `headless`
- Verified source: [tests/doccheck/assert_dllcall.ahk:57](../../tests/doccheck/assert_dllcall.ahk#L57)
- Profile command: `bash tests/doccheck/run_check.sh "$BIN"`

## Additional verified environments

- `headless`: [tests/doccheck/assert_interop.ahk:6](../../tests/doccheck/assert_interop.ahk#L6)
- `x11`: [tests/doccheck/assert_msg.ahk:35](../../tests/doccheck/assert_msg.ahk#L35)
- `headless`: [tests/doccheck/assert_registry.ahk:74](../../tests/doccheck/assert_registry.ahk#L74)
- `headless`: [tests/doccheck/assert_statements.ahk:104](../../tests/doccheck/assert_statements.ahk#L104)
- `x11`: [tests/doccheck/assert_misc_cov.ahk:92](../../tests/doccheck/assert_misc_cov.ahk#L92)
- `x11`: [tests/doccheck/assert_clipboard_all.ahk:88](../../tests/doccheck/assert_clipboard_all.ahk#L88)

## Syntax

````text
BufferObj := Buffer(ByteCount, FillByte) BufferObj := Buffer.Call(ByteCount, FillByte)
````

## Linux-verified example excerpt

This excerpt is executed by the profile command above; surrounding setup and assertions remain in the linked source.

````ahk

; Buffer-backed output.
b := Buffer(8)
DllCall("sprintf", "Ptr", b.Ptr, "AStr", "%d-%d", "Int", 3, "Int", 4)
MsgBox "out_buf=" (StrGet(b, 3, "UTF-8") = "3-4")
; --- Failure cases ---
````

## Upstream reference example

Source: [docs-v2/docs/lib/Buffer.htm](../../docs-v2/docs/lib/Buffer.htm)

````ahk
max_chars := 11
; Allocate a buffer for use with the Unicode version of wsprintf.
bufW := Buffer(max_chars*2)
; Print a UTF-16 string into the buffer with wsprintfW().
DllCall("wsprintfW", "Ptr", bufW, "Str", "0x%08x", "UInt", 4919, "CDecl")
; Retrieve the string from bufW and show it.
M
````
