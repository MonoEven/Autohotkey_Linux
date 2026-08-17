; Coverage suite (round-1 backlog): minimal runtime assertions for worklist
; IMPL functions with no direct reference in any assert source.
;
;   * 51 of the 54 previously unreferenced functions get a real invocation
;     here (deterministic, no side effects beyond the test itself).
;   * 4 are documented as NOT automatable and deliberately excluded:
;       - Exit / Shutdown : process/system destructive; a real invocation
;         would terminate or reboot the test environment.
;       - Reload          : Linux restart semantics are not implemented
;         (the new instance gets an unhandled /restart argument and errors
;         out; see AUDIT_2026_WEAKENED.md §2.6) -- code-audited, not run.
;       - InputBox        : interactive; blocks on a dialog.
;   * Also adds code-level name references for names that previously
;     appeared only in other suites' comments: String, Class, Menu,
;     ObjBindMethod, Persistent, WinWaitNotActive.  ("GuiControl" is not a
;     global identifier in v2 -- the control classes are Gui.Control /
;     Gui.Text -- so it is covered through control instances, not by name.)
;   * Documented port limitations exercised here (asserted via error paths):
;       - ComCall(0, ComValue(3,100), "Int") -> "Invalid arg type." (the
;         Linux port has no COM vtable; D-Bus proxies are not callable).
;       - OnClipboardChange registers but the callback never fires (the
;         Win32 clipboard-change listener has no Linux counterpart;
;         AddClipboardFormatListener is a no-op stub) -- registration and
;         unregistration are asserted to be error-free.
;   * Requires --xvfb (xwin_helper windows for WinActivateBottom /
;     GroupDeactivate) and a D-Bus session bus (run_check.sh provides both).
#Requires AutoHotkey v2.0

OUT := "/tmp/ahk_dc_misc_out.txt"
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

; --- OnExit handler (runs at ExitApp; appends onexit_ran). ---------------
OnExit((*) => (FileAppend("onexit_ran=1`n", OUT), 0))

; --- Primitive classes / function objects -------------------------------
Check("float_val", () => Float("2.5"))
Check("float_type", () => Type(Float("2.5")))
Check("int_val", () => Integer("7"))
Check("int_type", () => Type(Integer(7)))
Check("func_is", () => StrLen is Func)
Check("func_name", () => StrLen.Name)
Check("func_min", () => StrLen.MinParams)
Check("func_call", () => StrLen.Call("abc"))
Check("enum_type", () => Type([1, 2].__Enum(1)))
Check("enum_class", () => [1, 2].__Enum(1) is Enumerator)
EnumCall() {
    e := [10, 20].__Enum(1)
    k := 0
    first := e.Call(&k)
    return first ":" k
}
Check("enum_call", EnumCall)

; --- Extra primitive/class names that were only comment-referenced --------
Check("str_is", () => "abc" is String)
Check("class_is", () => Float is Class)
Check("menu_type", () => Type(Menu()))
Check("menu_is", () => Menu() is Menu)
class BindClsM {
    m() {
        return 42
    }
}
BindClsFn() {
    o := BindClsM()
    f := ObjBindMethod(o, "m")
    return f.Call()
}
Check("objbind_method", BindClsFn)

; --- IsSet / IsSetRef ----------------------------------------------------
Check("isset_unset", () => IsSet(probe_never_assigned))
Check("isset_set", () => (probe_assigned := 1, IsSet(probe_assigned)))
RefCheck(&p) {
    return IsSetRef(&p)
}
Check("issetref_unset", () => IsSetRef(&probe_ref_unset))
Check("issetref_set", () => (probe_ref_set := 5, IsSetRef(&probe_ref_set)))
Check("issetref_byref", () => RefCheck(&probe_ref_unset))

; --- Obj* pointer / capacity / base helpers -----------------------------
ObjPtrFn() {
    b := Buffer(8, 0)
    return ObjPtr(b) != 0
}
Check("objptr", ObjPtrFn)
ObjPtrAddRefFn() {
    b := Buffer(8, 0)
    p := ObjPtrAddRef(b)
    ObjRelease(p)  ; Balance the AddRef.
    return p != 0
}
Check("objptraddref", ObjPtrAddRefFn)
ObjAddRefFn() {
    b := Buffer(8, 0)
    n := ObjAddRef(ObjPtr(b))
    ObjRelease(ObjPtr(b))  ; Balance.
    return n >= 1 ? "ok" : n
}
Check("objaddref", ObjAddRefFn)
ObjReleaseFn() {
    b := Buffer(8, 0)
    ObjAddRef(ObjPtr(b))  ; 2 refs now.
    n := ObjRelease(ObjPtr(b))
    return n >= 1 ? "ok" : n
}
Check("objrelease", ObjReleaseFn)
ObjFromPtrFn() {
    ; ObjFromPtr does NOT increment the reference count (docs); pair it
    ; with ObjPtrAddRef so the returned alias owns that extra reference.
    b := Buffer(8, 0)
    p := ObjPtrAddRef(b)
    o := ObjFromPtr(p)
    return Type(o)
}
Check("objfromptr", ObjFromPtrFn)
ObjFromPtrAddRefFn() {
    ; ObjFromPtrAddRef increments the count; the wrapper owns that ref.
    b := Buffer(8, 0)
    o := ObjFromPtrAddRef(ObjPtr(b))
    return Type(o)
}
Check("objfromptraddref", ObjFromPtrAddRefFn)
ObjCap() {
    o := Object()
    ObjSetCapacity(o, 100)
    return ObjGetCapacity(o)
}
Check("objcap", ObjCap)
ObjProps() {
    o := {a: 1, b: 2, c: 3}
    n := 0
    for k in ObjOwnProps(o)
        n++
    return n
}
Check("objownprops", ObjProps)
ObjBase() {
    base := {x: 10}
    o := {y: 5}
    ObjSetBase(o, base)
    return o.x
}
Check("objsetbase", ObjBase)
Check("hasmethod", () => HasMethod({m: () => 1}, "m"))
VarCap() {
    s := "abc"
    c := VarSetStrCapacity(&s, 100)
    return c >= 100 ? "ok" : c
}
Check("varsetstrcap", VarCap)
Check("strptr", () => StrPtr("hello") != 0)

; --- Process (own PID via ProcessExist() with no args) -------------------
Check("ownpid", () => ProcessExist() > 0)
Check("pwait", () => ProcessWait(ProcessExist(), 1) > 0)
Check("pwaitclose", () => ProcessWaitClose(ProcessExist(), 0.05) > 0)
Check("psetprio_omit", () => ProcessSetPriority("Normal") > 0)
Check("psetprio_own", () => ProcessSetPriority("Normal", ProcessExist()) > 0)
PersistFn() {
    Persistent()
    return 1
}
Check("persistent", PersistFn)

; --- Drive (statvfs + /proc/mounts; "/" is always mounted) ---------------
Check("drivefs_root", () => DriveGetFileSystem("/") != "")
Check("drivefs2_root", () => DriveGetFilesystem("/") != "")
Check("drivelabel", () => DriveGetLabel("/"))
Check("driveserial", () => DriveGetSerial("/"))
Check("drivestatus", () => DriveGetStatus("/"))
Check("drivestatuscd", () => DriveGetStatusCD())
DriveListType() {
    l := DriveGetList()
    return Type(l)
}
Check("drivelist_type", DriveListType)

; --- COM (D-Bus) ---------------------------------------------------------
Check("comobjactive", () => ComObjType(ComObjActive("org.freedesktop.DBus")))
Check("comobjfromptr", () => ComObjType(ComObjFromPtr(0x1234)))
; ComCall: no COM vtable on Linux; doc-style invocation must fail with a
; clear error, never crash.
Check("comcall_err", () => ComCall(0, ComValue(3, 100), "Int"))

; --- Key lookup (headless-safe: static keysym tables) --------------------
Check("getkeyvk", () => GetKeyVK("a") != 0)
Check("getkeysc", () => GetKeySC("a") != 0)

; --- Output / state no-ops ----------------------------------------------
Check("outputdebug", () => (OutputDebug("misc-cov probe"), 1))
Check("pause_false", () => (Pause(false), 1))
Check("suspend_toggle", () => (Suspend(true), Suspend(false), 1))

; --- HotIf family (criterion callbacks take 1 param) ---------------------
HotCrit(c) {
    return true
}
Check("hotif_fn", () => (HotIf(HotCrit), HotIf(), 1))
Check("hotif_reset", () => (HotIf(""), 1))
Check("hotif_wactive", () => (HotIfWinActive("misc-cov-none"), HotIf(), 1))
Check("hotif_wexist", () => (HotIfWinExist("misc-cov-none"), HotIf(), 1))
Check("hotif_wnactive", () => (HotIfWinNotActive("misc-cov-none"), HotIf(), 1))
Check("hotif_wnexist", () => (HotIfWinNotExist("misc-cov-none"), HotIf(), 1))

; --- Clipboard -----------------------------------------------------------
Check("clip_set", () => (A_Clipboard := "misc-cov-text", 1))
Check("clipwait", () => ClipWait(0.2, 1))
ClipAll() {
    b := Buffer(8, 0x41)
    ca := ClipboardAll(b, 8)
    return Type(ca)
}
Check("clipboardall", ClipAll)

; --- OnClipboardChange: registration only (never fires on Linux) ---------
ocb := (*) => 0
Check("onclip_reg", () => (OnClipboardChange(ocb, 1), 1))
Check("onclip_unreg", () => (OnClipboardChange(ocb, -1), 1))

; --- Window group ops (xwin_helper windows, Xvfb) ------------------------
Run('out/xwin_helper -title "MiscCov Alpha" -class MiscCovClass -x 100 -y 100 -w 300 -h 200')
Run('out/xwin_helper -title "MiscCov Beta" -class MiscCovClass -x 500 -y 300 -w 300 -h 200')
WinWait("MiscCov Alpha",, 5)
WinWait("MiscCov Beta",, 5)
Sleep(100)
Check("winab_title", () => (WinActivateBottom("MiscCov"), 1))
idA := WinExist("MiscCov Alpha")
Check("winab_id", () => (WinActivateBottom("ahk_id " idA), 1))
GroupDeact() {
    GroupAdd("covgrp", "MiscCov Alpha")
    GroupAdd("covgrp", "MiscCov Beta")
    GroupDeactivate("covgrp")
    return 1
}
Check("groupdeact", GroupDeact)
Check("groupdeact_unknown", () => (GroupDeactivate("no-such-group-xyz"), 1))
; Clean up the helper windows: Xvfb has no WM, so iconified windows stay
; mapped and would disturb later suites' PixelSearch/ImageSearch checks.
RunWait("pkill -x xwin_helper")
Sleep(50)
; WinWaitNotActive: nonexistent window is treated as already inactive.
WnaFn() {
    return WinWaitNotActive("No Such Window Ever",, 0.2)
}
Check("wna_none", WnaFn)

; --- SoundBeep (bell; returns 0) -----------------------------------------
Check("soundbeep", () => SoundBeep(523, 5))

; --- Gui control instances (v2 has no global "GuiControl" identifier; the
; control classes are Gui.Control/Gui.Text, covered here and in assert_gui) -
GuiCtrlType() {
    g := Gui()
    c := g.Add("Text",, "x")
    t := Type(c)
    g.Destroy()
    return t
}
Check("guictrl_type", GuiCtrlType)

; --- OnError: registered handler must fire on an uncaught error; the
; handler controls the exit (ExitApp 0) so the runner sees rc=0 ---
OnErr(e, m) {
    Log("onerror_ran=" (m ? "1" : "0"))
    ExitApp 0
}
Check("onerror_reg", () => (OnError(OnErr), 1))
Provoke() {
    throw "misc-cov-probe-boom"
}
Provoke()

ExitApp 0
