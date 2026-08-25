# IsObject

- Linux status: `IMPL` (P1)
- Example kind: `verified`
- Environment profile: `x11`
- Verified source: [tests/doccheck/assert_gui.ahk:97](../../tests/doccheck/assert_gui.ahk#L97)
- Profile command: `bash tests/doccheck/run_check.sh --xvfb "$BIN"`

## Additional verified environments

- `x11`: [tests/doccheck/assert_hotkey.ahk:124](../../tests/doccheck/assert_hotkey.ahk#L124)
- `headless`: [tests/doccheck/assert_object.ahk:14](../../tests/doccheck/assert_object.ahk#L14)

Returns a non-zero number if the specified value is an object.

## Syntax

````text
Boolean := IsObject(Value)
````

## Linux-verified example excerpt

This excerpt is executed by the profile command above; surrounding setup and assertions remain in the linked source.

````ahk
    Log("gfr_recurse=" (Type(GuiFromHwnd(e.Hwnd, 1)) = "Gui" ? "ok" : "bad"))
    Log("gfr_norecurse=" (GuiFromHwnd(e.Hwnd) = "" ? "ok" : "bad"))
    Log("gfr_ctrl=" (IsObject(GuiCtrlFromHwnd(e.Hwnd)) ? "ok" : "bad"))
    Log("gfr_notfound=" (GuiFromHwnd(123456789) = "" ? "ok" : "bad"))

    g.Destroy()
````

## Upstream reference example

Source: [docs-v2/docs/lib/IsObject.htm](../../docs-v2/docs/lib/IsObject.htm)

````ahk
obj := {key: "value"}
if IsObject(obj)
    MsgBox "This is an object."
else
    MsgBox "This is not an object."
````
