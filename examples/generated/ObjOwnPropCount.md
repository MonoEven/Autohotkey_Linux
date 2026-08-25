# ObjOwnPropCount

- Linux status: `IMPL` (P1)
- Example kind: `verified`
- Environment profile: `headless`
- Verified source: [tests/doccheck/assert_object.ahk:5](../../tests/doccheck/assert_object.ahk#L5)
- Profile command: `bash tests/doccheck/run_check.sh "$BIN"`

## Linux-verified example excerpt

This excerpt is executed by the profile command above; surrounding setup and assertions remain in the linked source.

````ahk

o := {a: 1, b: 2}
MsgBox "ObjOwnPropCount=" ObjOwnPropCount(o)
MsgBox "ObjHasOwnProp_yes=" ObjHasOwnProp(o, "a")
MsgBox "ObjHasOwnProp_no=" ObjHasOwnProp(o, "z")
MsgBox "HasBase=" HasBase(o, Object.Prototype)
````
