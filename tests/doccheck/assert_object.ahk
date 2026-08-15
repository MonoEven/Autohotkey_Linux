; Object module doc-check (v2 docs: Object/Array/Map, Obj* functions).
#Requires AutoHotkey v2.0

o := {a: 1, b: 2}
MsgBox "ObjOwnPropCount=" ObjOwnPropCount(o)
MsgBox "ObjHasOwnProp_yes=" ObjHasOwnProp(o, "a")
MsgBox "ObjHasOwnProp_no=" ObjHasOwnProp(o, "z")
MsgBox "HasBase=" HasBase(o, Object.Prototype)
MsgBox "HasProp_yes=" HasProp(o, "a")
MsgBox "HasProp_no=" HasProp(o, "z")
MsgBox "ObjGetBase_nonempty=" (ObjGetBase(o) != "")
MsgBox "Type_obj=" Type(o)
MsgBox "Type_arr=" Type([])
MsgBox "IsObject=" IsObject(o)
MsgBox "IsObject_int=" IsObject(1)

arr := [10, 20, 30]
MsgBox "Array_len=" arr.Length
MsgBox "Array_index=" arr[1]
MsgBox "Array_cap_ge=" (arr.Capacity >= 3)
arr.Push(40)
MsgBox "Array_push_len=" arr.Length
MsgBox "Array_pop=" arr.Pop()
arr.InsertAt(1, 5)
MsgBox "Array_insert=" arr[1]
arr.RemoveAt(2)
MsgBox "Array_delete=" arr[2]
sum := 0
for v in arr
    sum += v
MsgBox "Array_enum=" sum

m := Map()
m["k"] := "v"
m[1] := "one"
MsgBox "Map_get=" m["k"]
MsgBox "Map_count=" m.Count
MsgBox "Map_has=" m.Has("k")
MsgBox "Map_missing=" m.Has("zzz")
MsgBox "Map_delete=" m.Delete("k")
MsgBox "Map_after_delete=" m.Count
MsgBox "Map_intkey=" m[1]

; Methods / GetMethod / ObjBindMethod
class C {
    static Double(x) => x * 2
}
MsgBox "GetMethod=" (Type(C.GetMethod("Double")) != "")
MsgBox "BindMethod=" C.Double(21)
MsgBox "StaticCall=" C.Double(4)

; Properties
class P {
    v := 10
    Prop {
        get => this.v
        set => this.v := value * 2
    }
}
inst := P()
MsgBox "Prop_get=" inst.Prop
inst.Prop := 5
MsgBox "Prop_set=" inst.v

; Object literal basics
MsgBox "Literal=" ({x: 1, y: 2}).x
MsgBox "Nested=" ({a: {b: 42}}).a.b
; Indexing a plain object requires __Item (official docs example):
obj := {}
obj[] := Map()
obj["base"] := 10
MsgBox "Key_var=" obj["base"]
; Reading a missing key through __Item (a Map) raises UnsetItemError per docs.
try
    x := obj["nope"]
catch
    MsgBox "Key_missing_err=1"
