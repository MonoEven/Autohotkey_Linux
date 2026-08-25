# ComObjType

- Linux status: `IMPL` (P2)
- Example kind: `verified`
- Environment profile: `dbus`
- Verified source: [tests/doccheck/assert_com.ahk:8](../../tests/doccheck/assert_com.ahk#L8)
- Profile command: `bash tests/doccheck/run_check.sh --xvfb "$BIN"`
- Linux adaptation: COM is a D-Bus adaptation: 'IID' returns a "(D-Bus)" placeholder

## Additional verified environments

- `x11`: [tests/doccheck/assert_misc_cov.ahk:189](../../tests/doccheck/assert_misc_cov.ahk#L189)

Retrieves type information from a COM object.

## Syntax

````text
Info := ComObjType(ComObj , InfoType)
````

## Linux-verified example excerpt

This excerpt is executed by the profile command above; surrounding setup and assertions remain in the linked source.

````ahk
; --- ComValue wrappers (scalar types, no bus needed) ---
v := ComValue(3, 42)          ; VT_I4
MsgBox "cv_i4=" (ComObjType(v) = 3)
MsgBox "cv_val=" (ComObjValue(v) = 42)
MsgBox "cv_name=" (ComObjType(v, "Name") = "ComValue")
````

## Upstream reference example

Source: [docs-v2/docs/lib/ComObjType.htm](../../docs-v2/docs/lib/ComObjType.htm)

````ahk
d := ComObject("Scripting.Dictionary")
MsgBox
(
    "Variant type:`t" ComObjType(d) "
    Interface name:`t" ComObjType(d, "Name") "
    Interface ID:`t" ComObjType(d, "IID") "
    Class name:`t" ComObjType(d, "Class") "
    Class ID (CLSID):`t" ComObjType(d, "CLSID")
)
````
