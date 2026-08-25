# ComObjValue

- Linux status: `IMPL` (P2)
- Example kind: `verified`
- Environment profile: `dbus`
- Verified source: [tests/doccheck/assert_com.ahk:9](../../tests/doccheck/assert_com.ahk#L9)
- Profile command: `bash tests/doccheck/run_check.sh --xvfb "$BIN"`
- Linux adaptation: COM is a D-Bus adaptation: returns the opaque D-Bus handle value

Retrieves the value or pointer stored in a COM wrapper object.

## Syntax

````text
Value := ComObjValue(ComObj)
````

## Linux-verified example excerpt

This excerpt is executed by the profile command above; surrounding setup and assertions remain in the linked source.

````ahk
v := ComValue(3, 42)          ; VT_I4
MsgBox "cv_i4=" (ComObjType(v) = 3)
MsgBox "cv_val=" (ComObjValue(v) = 42)
MsgBox "cv_name=" (ComObjType(v, "Name") = "ComValue")

vf := ComValue(5, 2.5)        ; VT_R8
````

## Upstream reference example

Source: [docs-v2/docs/lib/ComObjValue.htm](../../docs-v2/docs/lib/ComObjValue.htm)

The upstream page has no standalone code block; use the Linux-verified excerpt above.
