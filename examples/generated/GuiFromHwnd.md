# GuiFromHwnd

- Linux status: `IMPL` (P1)
- Example kind: `verified`
- Environment profile: `x11`
- Verified source: [tests/doccheck/assert_gui.ahk:94](../../tests/doccheck/assert_gui.ahk#L94)
- Profile command: `bash tests/doccheck/run_check.sh --xvfb "$BIN"`

## Additional verified environments

- `x11`: [tests/doccheck/assert_shape.ahk:114](../../tests/doccheck/assert_shape.ahk#L114)

Retrieves the Gui object of a GUI window associated with the specified window handle.

## Syntax

````text
GuiObj := GuiFromHwnd(Hwnd , RecurseParent)
````

## Linux-verified example excerpt

This excerpt is executed by the profile command above; surrounding setup and assertions remain in the linked source.

````ahk
    ; or an empty string when there is none).
    gh := g.Hwnd
    Log("gfr_hwnd=" (Type(GuiFromHwnd(gh)) = "Gui" ? "ok" : "bad"))
    Log("gfr_recurse=" (Type(GuiFromHwnd(e.Hwnd, 1)) = "Gui" ? "ok" : "bad"))
    Log("gfr_norecurse=" (GuiFromHwnd(e.Hwnd) = "" ? "ok" : "bad"))
    Log("gfr_ctrl=" (IsObject(GuiCtrlFromHwnd(e.Hwnd)) ? "ok" : "bad"))
````

## Upstream reference example

Source: [docs-v2/docs/lib/GuiFromHwnd.htm](../../docs-v2/docs/lib/GuiFromHwnd.htm)

````ahk
MyGui := Gui(, "Title of Window")
MyGui.Add("Text",, "Some text to display.")
MyGui.Show()
MsgBox(GuiFromHwnd(MyGui.Hwnd).Title)
````
