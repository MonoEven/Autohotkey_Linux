# GuiCtrlFromHwnd

- Linux status: `IMPL` (P1)
- Example kind: `verified`
- Environment profile: `x11`
- Verified source: [tests/doccheck/assert_gui.ahk:97](../../tests/doccheck/assert_gui.ahk#L97)
- Profile command: `bash tests/doccheck/run_check.sh --xvfb "$BIN"`

## Additional verified environments

- `x11`: [tests/doccheck/assert_shape.ahk:117](../../tests/doccheck/assert_shape.ahk#L117)

Retrieves the GuiControl object of a GUI control associated with the specified window handle.

## Syntax

````text
GuiControlObj := GuiCtrlFromHwnd(Hwnd)
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

Source: [docs-v2/docs/lib/GuiCtrlFromHwnd.htm](../../docs-v2/docs/lib/GuiCtrlFromHwnd.htm)

The upstream page has no standalone code block; use the Linux-verified excerpt above.
