#Requires AutoHotkey v2.0
m := Menu()
FileAppend("menu-ok type=" Type(m) "`n", "/tmp/dbg_menu.txt")
FileAppend("handle=" m.Handle "`n", "/tmp/dbg_menu.txt")
ExitApp(0)
