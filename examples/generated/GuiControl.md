# GuiControl

- Linux status: `IMPL` (P1)
- Example kind: `verified`
- Environment profile: `x11`
- Verified source: [examples/gui/control_types.ahk:8](../../examples/gui/control_types.ahk#L8)
- Profile command: `bash tests/doccheck/run_check.sh --xvfb "$BIN"`

## Syntax

````text
GuiCtrl.Add(Items)
````

## Linux-verified example excerpt

This excerpt is executed by the profile command above; surrounding setup and assertions remain in the linked source.

````ahk

guiWindow := Gui(, "AHK GUI control example")
editCtrl := guiWindow.AddEdit("w260", "editable text") ; GuiControl instance.
treeCtrl := guiWindow.Add("TreeView", "w260 h100") ; TreeView instance.
rootItem := treeCtrl.Add("Root")
treeCtrl.Add("Child", rootItem)
````

## Upstream reference example

Source: [docs-v2/docs/lib/GuiControl.htm](../../docs-v2/docs/lib/GuiControl.htm)

````ahk
MyGui := Gui()
MyListBox := MyGui.Add("ListBox",, ["Black", "White"])
MyGui.Show()
Sleep 1000 ; Wait for demonstration purposes.
MyListBox.Delete()
MyListBox.Add(["Red", "Green", "Blue"])
````
