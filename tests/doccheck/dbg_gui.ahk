MsgBox "start"
myGui := Gui()
MsgBox "created"
myGui.Add("Text", , "Hello")
MsgBox "added"
try
    myGui.Show()
catch
    MsgBox "show_err"
MsgBox "done"