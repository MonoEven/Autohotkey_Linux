# HasBase

- Linux status: `IMPL` (P1)
- Example kind: `verified`
- Environment profile: `headless`
- Verified source: [tests/doccheck/assert_object.ahk:8](../../tests/doccheck/assert_object.ahk#L8)
- Profile command: `bash tests/doccheck/run_check.sh "$BIN"`

Returns a non-zero number if the specified value is derived from the specified base object.

## Syntax

````text
HasBase := HasBase(Value, BaseObj)
````

## Linux-verified example excerpt

This excerpt is executed by the profile command above; surrounding setup and assertions remain in the linked source.

````ahk
MsgBox "ObjHasOwnProp_yes=" ObjHasOwnProp(o, "a")
MsgBox "ObjHasOwnProp_no=" ObjHasOwnProp(o, "z")
MsgBox "HasBase=" HasBase(o, Object.Prototype)
MsgBox "HasProp_yes=" HasProp(o, "a")
MsgBox "HasProp_no=" HasProp(o, "z")
MsgBox "ObjGetBase_nonempty=" (ObjGetBase(o) != "")
````

## Upstream reference example

Source: [docs-v2/docs/lib/HasBase.htm](../../docs-v2/docs/lib/HasBase.htm)

````ahk
thebase := {key: "value"}
derived := {base: thebase}
MsgBox HasBase(thebase, derived) ; 0
MsgBox HasBase(derived, thebase) ; 1
````
