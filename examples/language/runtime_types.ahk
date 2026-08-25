#Requires AutoHotkey v2.0
; Primitive/container type examples: Array, Class, Enumerator, Func and String.
out := A_Args.Length ? A_Args[1] : "/tmp/ahk-example-runtime-types.txt"
if FileExist(out)
    FileDelete(out)

values := Array(10, 20, 30)
enumObject := values.__Enum(1)
first := 0
enumObject(&first)
functionObject := StrLen
text := String(42)

class ExampleRecord {
    __New(name) {
        this.Name := name
    }
}
record := ExampleRecord("linux")

FileAppend("array=" values.Length " first=" first
    " class=" (ExampleRecord is Class)
    " enum=" (enumObject is Enumerator)
    " func=" (functionObject is Func)
    " string=" text
    " record=" record.Name "`n", out)
ExitApp
