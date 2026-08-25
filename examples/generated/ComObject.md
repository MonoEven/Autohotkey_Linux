# ComObject

- Linux status: `IMPL` (P2)
- Example kind: `verified`
- Environment profile: `dbus`
- Verified source: [tests/doccheck/assert_com.ahk:30](../../tests/doccheck/assert_com.ahk#L30)
- Profile command: `bash tests/doccheck/run_check.sh --xvfb "$BIN"`
- Linux adaptation: COM is a D-Bus adaptation: no IUnknown/IDispatch pointers; D-Bus service specs create proxies and known Windows ProgIDs raise a migration error

## Additional verified environments

- `headless`: [tests/doccheck/assert_notimpl.ahk:27](../../tests/doccheck/assert_notimpl.ahk#L27)

Creates a COM object.

## Syntax

````text
ComObj := ComObject(CLSID , IID)
````

## Linux-verified example excerpt

This excerpt is executed by the profile command above; surrounding setup and assertions remain in the linked source.

````ahk

; --- ComObject proxy against the session bus ---
bus := ComObject("org.freedesktop.DBus")
MsgBox "proxy_type=" (ComObjType(bus) = 9)
MsgBox "proxy_name=" (ComObjType(bus, "Name") = "ComObject")
````

## Upstream reference example

Source: [docs-v2/docs/lib/ComObject.htm](../../docs-v2/docs/lib/ComObject.htm)

````ahk
ie := ComObject("InternetExplorer.Application")
ie.Visible := true  ; This is known to work incorrectly on IE7.
ie.Navigate("https://www.autohotkey.com/")
````
