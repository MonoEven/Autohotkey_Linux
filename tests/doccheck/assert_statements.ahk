; Statement / directive / category-page code-form doc-check.
;
; doc_index.tsv includes pages that are NOT functions: control-flow
; statements (If/Else/For/While/Switch/Try/Catch/Throw/Goto/Loop*/Until/
; Break/Continue/Return/Block/Class), directives (#Requires/#Warn/...), and
; category/method pages (File/Array/Map/Object/Buffer/Error/...).  These have
; code forms (syntax + examples) that must be validated too; this suite
; exercises the ones with runnable code forms.
#Requires AutoHotkey v2.0

OUT := "/tmp/ahk_dc_statements_out.txt"
FileDelete(OUT)
Log(line) => FileAppend(line "`n", OUT)
Check(name, fn) {
    try {
        r := fn()
        Log(name "=" r)
    } catch as e {
        Log(name "=ERR:" e.Message)
    }
}

; --- Helper functions exercising each statement form ------------------------

IfElse() {
    x := 5
    return x > 3 ? "gt" : "le"
}
IfBlock() {
    y := 10
    return y > 0 && y < 20 ? "in" : "out"
}
ForLoop() {
    s := 0
    for i in [1, 2, 3, 4]
        s += i
    return s
}
WhileLoop() {
    i := 0
    while (i < 3)
        i++
    return i
}
LoopCount() {
    n := 0
    Loop 5
        n++
    return n
}
BreakCont() {
    s := 0
    Loop 10 {
        if A_Index = 8
            break
        if Mod(A_Index, 2) = 0
            continue
        s += A_Index
    }
    return s  ; 1+3+5+7 = 16
}
SwCase() {
    v := "b"
    r := ""
    switch v {
        case "a": r := "A"
        case "b": r := "B"
        default: r := "?"
    }
    return r
}
TryCatch() {
    r := ""
    try
        throw ValueError("boom")
    catch as e
        r := e.Message
    finally
        r .= "|finally"
    return r
}
UntilLoop() {
    i := 0
    Loop {
        i++
    } until (i = 4)
    return i
}
ArrayMethods() {
    a := [3, 1, 2]
    a.Push(4)
    return a.Has(3) ":" a.Length
}
MapMethods() {
    m := Map("a", 1, "b", 2)
    return m["a"] + m.Count
}
ObjectMethods() {
    o := Object()
    o.x := 10
    return o.x + (o.HasOwnProp("x") ? 1 : 0)
}
BufferMethods() {
    b := Buffer(8, 0)
    return b.Size
}
ErrorClass() {
    e := Error("msg", "what")
    return e is Error ? "ok" : "bad"
}
StrConcat() {
    return "Hello" . "-" . "World"
}
NumClass() {
    return Number("3.5") + 0.5
}
RoundMath() {
    return Round(3.7)
}
MonCount() {
    return MonitorGetCount() >= 1 ? "ok" : "bad"
}
FileReadWrite() {
    f := A_Temp "\_stmt_test.txt"
    FileAppend("abc", f)
    t := FileRead(f)
    FileDelete(f)
    return t
}

; --- Check each -------------------------------------------------------------
Check("if_else", IfElse)
Check("if_block", IfBlock)
Check("for_loop", ForLoop)
Check("while_loop", WhileLoop)
Check("loop_count", LoopCount)
Check("break_cont", BreakCont)
Check("switch", SwCase)
Check("try_catch", TryCatch)
Check("until", UntilLoop)
Check("array_methods", ArrayMethods)
Check("map_methods", MapMethods)
Check("object_methods", ObjectMethods)
Check("buffer_methods", BufferMethods)
Check("error_class", ErrorClass)
Check("string_concat", StrConcat)
Check("number_class", NumClass)
Check("math_round", RoundMath)
Check("monitor_count", MonCount)
Check("file_rw", FileReadWrite)

ExitApp 0