# ListView

- Linux status: `IMPL` (P1)
- Example kind: `verified`
- Environment profile: `x11`
- Verified source: [examples/gui/control_types.ahk:12](../../examples/gui/control_types.ahk#L12)
- Profile command: `bash tests/doccheck/run_check.sh --xvfb "$BIN"`

## Syntax

````text
RowNumber := LV.Add(Options, Col1, Col2, ...)
````

## Linux-verified example excerpt

This excerpt is executed by the profile command above; surrounding setup and assertions remain in the linked source.

````ahk
rootItem := treeCtrl.Add("Root")
treeCtrl.Add("Child", rootItem)
listCtrl := guiWindow.Add("ListView", "w260 h100") ; ListView instance.
listCtrl.InsertCol(1, "140", "Name")
listCtrl.InsertCol(2, "80", "Value")
listCtrl.Add(, "Alpha", "1")
````

## Upstream reference example

Source: [docs-v2/docs/lib/ListView.htm](../../docs-v2/docs/lib/ListView.htm)

````ahk
LV.Modify(0, "Select")   ; Select all.
LV.Modify(0, "-Select")  ; De-select all.
LV.Modify(0, "-Check")  ; Uncheck all the checkboxes.
````
