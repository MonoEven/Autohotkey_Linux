# StrPut

- Linux status: `IMPL` (P1)
- Example kind: `verified`
- Environment profile: `headless`
- Verified source: [tests/doccheck/assert_interop.ahk:34](../../tests/doccheck/assert_interop.ahk#L34)
- Profile command: `bash tests/doccheck/run_check.sh "$BIN"`

Copies a string to a memory address or buffer, optionally converting it to a given code page.

## Syntax

````text
BytesWritten := StrPut(String, Target , Length, Encoding) BytesWritten := StrPut(String, Target , Encoding) ReqBufSize := StrPut(String , Encoding)
````

## Linux-verified example excerpt

This excerpt is executed by the profile command above; surrounding setup and assertions remain in the linked source.

````ahk

; StrPut/StrGet: UTF-8 and UTF-16 round trips.
StrPut("hi", b, "UTF-8")
MsgBox "StrGet_utf8=" (StrGet(b, 2, "UTF-8") = "hi")
MsgBox "StrPut_reqsize=" (StrPut("hello", "UTF-8") = 6)
MsgBox "StrPut_utf16_n=" (StrPut("hi", b, "UTF-16") = 6)
````

## Upstream reference example

Source: [docs-v2/docs/lib/StrPut.htm](../../docs-v2/docs/lib/StrPut.htm)

````ahk
StrPut(str, address, "cp0")  ; Code page 0, unspecified buffer size
StrPut(str, address, n, 0)   ; Maximum n chars, code page 0
StrPut(str, address, 0)      ; Unsupported (maximum 0 chars)
````
