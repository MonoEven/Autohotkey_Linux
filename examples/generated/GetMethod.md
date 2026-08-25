# GetMethod

- Linux status: `IMPL` (P1)
- Example kind: `verified`
- Environment profile: `headless`
- Verified source: [tests/doccheck/assert_object.ahk:48](../../tests/doccheck/assert_object.ahk#L48)
- Profile command: `bash tests/doccheck/run_check.sh "$BIN"`

Retrieves the implementation function of a method.

## Syntax

````text
Method := GetMethod(Value , Name, ParamCount)
````

## Linux-verified example excerpt

This excerpt is executed by the profile command above; surrounding setup and assertions remain in the linked source.

````ahk
    static Double(x) => x * 2
}
MsgBox "GetMethod=" (Type(C.GetMethod("Double")) != "")
MsgBox "BindMethod=" C.Double(21)
MsgBox "StaticCall=" C.Double(4)
````

## Upstream reference example

Source: [docs-v2/docs/lib/GetMethod.htm](../../docs-v2/docs/lib/GetMethod.htm)

````ahk
method := GetMethod({}, "GetMethod")  ; It's also a method.
MsgBox method.MaxParams  ; Takes 3 parameters, including 'this'.
MsgBox method = GetMethod  ; Actually the same object in this case.
````
