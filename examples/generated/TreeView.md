# TreeView

- Linux status: `IMPL` (P1)
- Example kind: `verified`
- Environment profile: `x11`
- Verified source: [examples/gui/control_types.ahk:9](../../examples/gui/control_types.ahk#L9)
- Profile command: `bash tests/doccheck/run_check.sh --xvfb "$BIN"`

## Syntax

````text
ItemID := TV.Add(Name, ParentItemID, Options)
````

## Linux-verified example excerpt

This excerpt is executed by the profile command above; surrounding setup and assertions remain in the linked source.

````ahk
guiWindow := Gui(, "AHK GUI control example")
editCtrl := guiWindow.AddEdit("w260", "editable text") ; GuiControl instance.
treeCtrl := guiWindow.Add("TreeView", "w260 h100") ; TreeView instance.
rootItem := treeCtrl.Add("Root")
treeCtrl.Add("Child", rootItem)
listCtrl := guiWindow.Add("ListView", "w260 h100") ; ListView instance.
````

## Upstream reference example

Source: [docs-v2/docs/lib/TreeView.htm](../../docs-v2/docs/lib/TreeView.htm)

````ahk
MyGui := Gui()
TV := MyGui.Add("TreeView", "-ReadOnly")
TV.Add("A")
TV.Add("B")
TV.Add("C")
TV.OnEvent("ItemEdit", TV_Edit)  ; Call TV_Edit whenever we finish editing an item.
MyGui.Show()
TV_Edit(TV, Item)
{
    TV.Modify(TV.GetParent(Item), "Sort")  ; This works even if the item
````
