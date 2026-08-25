# HasProp

- Linux status: `IMPL` (P1)
- Example kind: `verified`
- Environment profile: `headless`
- Verified source: [tests/doccheck/assert_object.ahk:9](../../tests/doccheck/assert_object.ahk#L9)
- Profile command: `bash tests/doccheck/run_check.sh "$BIN"`

Returns a non-zero number if the specified value has a property by the specified name.

## Syntax

````text
HasProp := HasProp(Value, Name)
````

## Linux-verified example excerpt

This excerpt is executed by the profile command above; surrounding setup and assertions remain in the linked source.

````ahk
MsgBox "ObjHasOwnProp_no=" ObjHasOwnProp(o, "z")
MsgBox "HasBase=" HasBase(o, Object.Prototype)
MsgBox "HasProp_yes=" HasProp(o, "a")
MsgBox "HasProp_no=" HasProp(o, "z")
MsgBox "ObjGetBase_nonempty=" (ObjGetBase(o) != "")
MsgBox "Type_obj=" Type(o)
````

## Upstream reference example

Source: [docs-v2/docs/lib/HasProp.htm](../../docs-v2/docs/lib/HasProp.htm)

````ahk
MsgBox HasProp({}, "x") ; 0
MsgBox HasProp({x:1}, "x") ; 1
MsgBox HasProp(0, "Base") ; 1
````
