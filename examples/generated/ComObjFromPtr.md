# ComObjFromPtr

- Linux status: `IMPL` (P2)
- Example kind: `verified`
- Environment profile: `x11`
- Verified source: [tests/doccheck/assert_misc_cov.ahk:190](../../tests/doccheck/assert_misc_cov.ahk#L190)
- Profile command: `bash tests/doccheck/run_check.sh --xvfb "$BIN"`
- Linux adaptation: COM is a D-Bus adaptation: the pointer is treated as an opaque D-Bus handle

Wraps a raw IDispatch pointer (COM object) for use by the script.

## Syntax

````text
ComObj := ComObjFromPtr(DispPtr)
````

## Linux-verified example excerpt

This excerpt is executed by the profile command above; surrounding setup and assertions remain in the linked source.

````ahk
; --- COM (D-Bus) ---------------------------------------------------------
Check("comobjactive", () => ComObjType(ComObjActive("org.freedesktop.DBus")))
Check("comobjfromptr", () => ComObjType(ComObjFromPtr(0x1234)))
; ComCall: no COM vtable on Linux; doc-style invocation must fail with a
; clear error, never crash.
Check("comcall_err", () => ComCall(0, ComValue(3, 100), "Int"))
````

## Upstream reference example

Source: [docs-v2/docs/lib/ComObjFromPtr.htm](../../docs-v2/docs/lib/ComObjFromPtr.htm)

The upstream page has no standalone code block; use the Linux-verified excerpt above.
