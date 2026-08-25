#Requires AutoHotkey v2.0
; Interactive example: this intentionally waits for a person.
result := InputBox("Enter a value to echo:", "AutoHotkey Linux InputBox", "w420 h140", "世界")
if result.Result = "OK"
    MsgBox("You entered: " result.Value)
else
    MsgBox("Input was cancelled or timed out: " result.Result)
