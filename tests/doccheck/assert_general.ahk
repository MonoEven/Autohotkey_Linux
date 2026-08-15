; General/environment/system module doc-check (v2 docs).
#Requires AutoHotkey v2.0

; Env
EnvSet("AHK_DC_VAR", "dc-value")
MsgBox "EnvGet=" EnvGet("AHK_DC_VAR")
MsgBox "EnvGet_missing=" EnvGet("AHK_DC_NOPE_123")
EnvSet("AHK_DC_VAR", "")
MsgBox "EnvGet_cleared=" EnvGet("AHK_DC_VAR")

; Working dir
old := A_WorkingDir
SetWorkingDir("/tmp")
MsgBox "SetWorkingDir=" (A_WorkingDir = "/tmp")
SetWorkingDir(old)
MsgBox "SetWorkingDir_restore=" (A_WorkingDir = old)

; Sleep timing
t0 := A_TickCount
Sleep 250
dt := A_TickCount - t0
MsgBox "Sleep_ge=" (dt >= 200)

; MsgBox return value (headless console mode returns OK)
MsgBox "MsgBox_ret=" MsgBox("test")
; Script vars / A_* environment
MsgBox "A_ScriptName=" A_ScriptName
MsgBox "A_PtrSize=" A_PtrSize
MsgBox "A_Is64bitOS=" A_Is64bitOS
MsgBox "A_Temp_nonempty=" (A_Temp != "")
MsgBox "A_WorkingDir_nonempty=" (A_WorkingDir != "")

; Process module
MsgBox "ProcessExist_self=" (ProcessExist() > 0)
Run "sleep 5", , , &pid
MsgBox "Run_pid=" (pid > 0)
MsgBox "ProcessExist_pid=" (ProcessExist(pid) = pid)
ProcessClose(pid)
Sleep 200
MsgBox "ProcessClose_gone=" (ProcessExist(pid) = 0)

; RunWait exit code
MsgBox "RunWait_code=" RunWait("/bin/sh -c `"exit 3`"")

; Drive
MsgBox "DriveGetType=" (DriveGetType("/") != "")
MsgBox "DriveGetSpaceFree=" (DriveGetSpaceFree("/") > 0)
MsgBox "DriveGetCapacity=" (DriveGetCapacity("/") > 0)

; Ini
ini := "/tmp/ahk_dc_ini.ini"
IniWrite("v1", ini, "S", "K")
MsgBox "IniRead=" IniRead(ini, "S", "K")
MsgBox "IniRead_default=" IniRead(ini, "S", "NOPE", "dflt")
IniDelete(ini, "S", "K")
MsgBox "IniDelete=" (IniRead(ini, "S", "K") = "")
FileDelete(ini)

; Clipboard
A_Clipboard := "dc-clip"
MsgBox "Clipboard=" A_Clipboard
A_Clipboard := ""

; ListLines / Critical / Thread (state functions)
MsgBox "ListLines_ret=" (ListLines() >= 0)
MsgBox "Critical_ret=" (Critical() >= 0)
MsgBox "Thread_ret=" (Thread("NoTimers") = 0)

; Sort / VerCompare / Type already covered elsewhere; spot checks:
MsgBox "VerCompare=" VerCompare("2.0.26", "2.0.20")
MsgBox "GetKeyName=" GetKeyName("Enter")
MsgBox "IsLabel_yes=" IsLabel("dc_label")
MsgBox "IsLabel_no=" IsLabel("nope")
dc_label:
