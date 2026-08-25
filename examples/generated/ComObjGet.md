# ComObjGet

- Linux status: `IMPL` (P2)
- Example kind: `verified`
- Environment profile: `dbus`
- Verified source: [tests/doccheck/assert_com.ahk:61](../../tests/doccheck/assert_com.ahk#L61)
- Profile command: `bash tests/doccheck/run_check.sh --xvfb "$BIN"`
- Linux adaptation: COM is a D-Bus adaptation: D-Bus service specs create proxies and known Windows ProgIDs raise a migration error

Returns a reference to an object provided by a COM component.

## Syntax

````text
ComObj := ComObjGet(Name)
````

## Linux-verified example excerpt

This excerpt is executed by the profile command above; surrounding setup and assertions remain in the linked source.

````ahk

; --- ComObjGet alias ---
bus2 := ComObjGet("org.freedesktop.DBus")
MsgBox "get_alias=" (ComObjType(bus2) = 9)

; Windows ProgIDs must not be silently reinterpreted as D-Bus names.
````

## Upstream reference example

Source: [docs-v2/docs/lib/ComObjGet.htm](../../docs-v2/docs/lib/ComObjGet.htm)

````ahk
+Esc::
{
    pid := WinGetPID("A")
    ; Get WMI service object.
    wmi := ComObjGet("winmgmts:")
    ; Run query to retrieve matching process(es).
    queryEnum := wmi.ExecQuery(""
        . "Select * from Win32_Process where ProcessId=" . pid)
        ._NewEnum()
    ; Get first
````
