# ObjGetBase

- Linux status: `IMPL` (P1)
- Example kind: `verified`
- Environment profile: `headless`
- Verified source: [tests/doccheck/assert_object.ahk:11](../../tests/doccheck/assert_object.ahk#L11)
- Profile command: `bash tests/doccheck/run_check.sh "$BIN"`

## Linux-verified example excerpt

This excerpt is executed by the profile command above; surrounding setup and assertions remain in the linked source.

````ahk
MsgBox "HasProp_yes=" HasProp(o, "a")
MsgBox "HasProp_no=" HasProp(o, "z")
MsgBox "ObjGetBase_nonempty=" (ObjGetBase(o) != "")
MsgBox "Type_obj=" Type(o)
MsgBox "Type_arr=" Type([])
MsgBox "IsObject=" IsObject(o)
````
