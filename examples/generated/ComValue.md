# ComValue

- Linux status: `IMPL` (P2)
- Example kind: `verified`
- Environment profile: `dbus`
- Verified source: [tests/doccheck/assert_com.ahk:7](../../tests/doccheck/assert_com.ahk#L7)
- Profile command: `bash tests/doccheck/run_check.sh --xvfb "$BIN"`
- Linux adaptation: COM is a D-Bus adaptation: wraps typed values for the D-Bus layer

## Additional verified environments

- `headless`: [tests/doccheck/assert_notimpl.ahk:26](../../tests/doccheck/assert_notimpl.ahk#L26)
- `x11`: [tests/doccheck/assert_misc_cov.ahk:193](../../tests/doccheck/assert_misc_cov.ahk#L193)

Wraps a value, SafeArray or COM object for use by the script or for passing to a COM method.

## Syntax

````text
ComObj := ComValue(VarType, Value , Flags)
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

Source: [docs-v2/docs/lib/ComValue.htm](../../docs-v2/docs/lib/ComValue.htm)

````ahk
#Requires AutoHotkey v2 32-bit ; 32-bit for ScriptControl.
code := "
(
Sub Example(Var)
    MsgBox Var
    Var = "out value!"
End Sub
)"
sc := ComObject("ScriptControl"), sc.Language := "VBScript", sc.AddCode(code)
; Example: Pass a VARIANT ByRef to a COM method.
var := ComVar()
````
