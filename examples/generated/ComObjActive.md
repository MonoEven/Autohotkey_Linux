# ComObjActive

- Linux status: `IMPL` (P2)
- Example kind: `verified`
- Environment profile: `x11`
- Verified source: [tests/doccheck/assert_misc_cov.ahk:189](../../tests/doccheck/assert_misc_cov.ahk#L189)
- Profile command: `bash tests/doccheck/run_check.sh --xvfb "$BIN"`
- Linux adaptation: COM is a D-Bus adaptation: approximated by ComObjGet; known Windows ProgIDs raise a migration error

Retrieves a registered COM object.

## Syntax

````text
ComObj := ComObjActive(CLSID)
````

## Linux-verified example excerpt

This excerpt is executed by the profile command above; surrounding setup and assertions remain in the linked source.

````ahk

; --- COM (D-Bus) ---------------------------------------------------------
Check("comobjactive", () => ComObjType(ComObjActive("org.freedesktop.DBus")))
Check("comobjfromptr", () => ComObjType(ComObjFromPtr(0x1234)))
; ComCall: no COM vtable on Linux; doc-style invocation must fail with a
; clear error, never crash.
````

## Upstream reference example

Source: [docs-v2/docs/lib/ComObjActive.htm](../../docs-v2/docs/lib/ComObjActive.htm)

````ahk
try word := ComObjActive("Word.Application")
if not IsSet(word)
    MsgBox "Word isn't open."
else
    MsgBox word.ActiveDocument.FullName
````
