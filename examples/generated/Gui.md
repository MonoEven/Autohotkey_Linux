# Gui

- Linux status: `IMPL` (P1)
- Example kind: `verified`
- Environment profile: `x11`
- Verified source: [tests/doccheck/assert_gui.ahk:13](../../tests/doccheck/assert_gui.ahk#L13)
- Profile command: `bash tests/doccheck/run_check.sh --xvfb "$BIN"`

## Additional verified environments

- `x11`: [tests/doccheck/assert_misc_cov.ahk:265](../../tests/doccheck/assert_misc_cov.ahk#L265)

## Syntax

````text
MyGui := Gui(Options, Title, EventObj) MyGui := Gui.Call(Options, Title, EventObj)
````

## Linux-verified example excerpt

This excerpt is executed by the profile command above; surrounding setup and assertions remain in the linked source.

````ahk
try {
    ; --- basic window + controls --------------------------------------
    g := Gui()
    g.Title := "DocCheck GUI"
    e := g.Add("Edit", "w200 vTheEdit", "hello")
    cb := g.Add("CheckBox", "x+10 vTheCheck", "Check")
````

## Upstream reference example

Source: [docs-v2/docs/lib/Gui.htm](../../docs-v2/docs/lib/Gui.htm)

````ahk
MyGui := Gui(, "Title of Window")
MyGui.Opt("+AlwaysOnTop +Disabled -SysMenu +Owner")  ; +Owner avoids a taskbar button.
MyGui.Add("Text",, "Some text to display.")
MyGui.Show("NoActivate")  ; NoActivate avoids deactivating the currently active window.
````
