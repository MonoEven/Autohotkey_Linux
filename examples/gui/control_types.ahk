#Requires AutoHotkey v2.0
; GuiControl, ListView and TreeView on X11/XWayland.
out := A_Args.Length ? A_Args[1] : "/tmp/ahk-example-gui-controls.txt"
if FileExist(out)
    FileDelete(out)

guiWindow := Gui(, "AHK GUI control example")
editCtrl := guiWindow.AddEdit("w260", "editable text") ; GuiControl instance.
treeCtrl := guiWindow.Add("TreeView", "w260 h100") ; TreeView instance.
rootItem := treeCtrl.Add("Root")
treeCtrl.Add("Child", rootItem)
listCtrl := guiWindow.Add("ListView", "w260 h100") ; ListView instance.
listCtrl.InsertCol(1, "140", "Name")
listCtrl.InsertCol(2, "80", "Value")
listCtrl.Add(, "Alpha", "1")
listCtrl.Add(, "世界", "2")
guiWindow.AddButton("Default", "Close").OnEvent("Click", (*) => guiWindow.Destroy())
guiWindow.OnEvent("Close", (*) => guiWindow.Destroy())
guiWindow.Show()

FileAppend("gui_control=" Type(editCtrl) " tree_items=2 list_rows=" listCtrl.GetCount() "`n", out)
SetTimer(() => guiWindow.Destroy(), -1200)
SetTimer(() => ExitApp(), -1500)
