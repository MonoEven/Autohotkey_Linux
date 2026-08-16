; D-Bus COM doc-check: ComObject/ComValue/ComObjType/ComObjValue against a
; real session bus.  Requires a D-Bus session (run via run_check.sh which
; starts dbus-daemon when available; headless MsgBox prints name=value).
#Requires AutoHotkey v2.0

; --- ComValue wrappers (scalar types, no bus needed) ---
v := ComValue(3, 42)          ; VT_I4
MsgBox "cv_i4=" (ComObjType(v) = 3)
MsgBox "cv_val=" (ComObjValue(v) = 42)
MsgBox "cv_name=" (ComObjType(v, "Name") = "ComValue")

vf := ComValue(5, 2.5)        ; VT_R8
MsgBox "cv_float=" (ComObjType(vf) = 5)
MsgBox "cv_fval=" (ComObjValue(vf) = 2.5)

vs := ComValue(8, "hello")    ; VT_BSTR
MsgBox "cv_str=" (ComObjType(vs) = 8)

vb := ComValue(11, 1)         ; VT_BOOL
MsgBox "cv_bool=" (ComObjType(vb) = 11)
MsgBox "cv_bval=" (ComObjValue(vb) = 1)

vptr := ComValue(3, 7)
MsgBox "cv_ptr=" (vptr.Ptr = 7)

; --- ComObjType on non-ComObject ---
MsgBox "notcom=" (ComObjType(123) = "")

; --- ComObject proxy against the session bus ---
bus := ComObject("org.freedesktop.DBus")
MsgBox "proxy_type=" (ComObjType(bus) = 9)
MsgBox "proxy_name=" (ComObjType(bus, "Name") = "ComObject")

; Call a real D-Bus method: org.freedesktop.DBus.GetId() returns the bus id.
try {
    id := bus.GetId()
    MsgBox "getid_ok=" (StrLen(id) > 5)
} catch as e {
    MsgBox "getid_err=1"
}

; org.freedesktop.DBus.ListNames() returns an array of service names.
try {
    names := bus.ListNames()
    MsgBox "listnames_ok=" (names is Array ? 1 : 0)
} catch as e {
    MsgBox "listnames_err=1"
}

; Property access: org.freedesktop.DBus has no standard properties, but
; the Peer interface (org.freedesktop.DBus.Peer) has no properties either.
; Test that calling with an unknown member raises an error (not a crash).
try {
    bogus := bus.NoSuchMethod()
    MsgBox "bogus_ok=1"
} catch as e {
    MsgBox "bogus_err=" (Type(e) = "OSError")
}

; --- ComObjGet alias ---
bus2 := ComObjGet("org.freedesktop.DBus")
MsgBox "get_alias=" (ComObjType(bus2) = 9)

; --- ComObjFlags ---
vf2 := ComValue(3, 1)
MsgBox "flags0=" (ComObjFlags(vf2) = 0)
ComObjFlags(vf2, 1)
MsgBox "flags1=" (ComObjFlags(vf2) = 1)
