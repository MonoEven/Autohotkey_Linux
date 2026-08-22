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
; A loaded CI runner can take a while to reap the terminated process; poll
; (bounded) instead of a fixed short sleep so the assertion never flakes.
reaped := 0
Loop 50 {   ; up to ~5s
    if ProcessExist(pid) = 0 {
        reaped := 1
        break
    }
    Sleep 100
}
MsgBox "ProcessClose_gone=" reaped

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

; --- Environment / built-in variables (v2 docs) ---
; (Placed before the Critical() call below: A_IsCritical reflects the peek
; frequency when the current thread is critical, which Critical() enables.)
MsgBox "A_Args_count=" A_Args.Length
MsgBox "A_Args_1=" A_Args[1]
MsgBox "A_Args_2=" A_Args[2]
MsgBox "A_LastError=" (A_LastError = 0)
A_LastError := 42
MsgBox "A_LastError_set=" (A_LastError = 42)
MsgBox "A_IsPaused=" A_IsPaused
MsgBox "A_IsSuspended=" A_IsSuspended
MsgBox "A_IsCritical=" A_IsCritical
MsgBox "A_LineNumber_gt=" (A_LineNumber > 0)
MsgBox "A_LineFile_nn=" (A_LineFile != "")
MsgBox "A_ComputerName_nn=" (A_ComputerName != "")
MsgBox "A_UserName_nn=" (A_UserName != "")
MsgBox "A_OSVersion_nn=" (A_OSVersion != "")
MsgBox "A_Language_hex=" (RegExMatch(A_Language, "^[0-9A-F]{4}$") = 1)
MsgBox "A_MyDocuments_nn=" (A_MyDocuments != "")
MsgBox "A_AhkPath_nn=" (A_AhkPath != "")
MsgBox "A_ScriptHwnd=" (A_ScriptHwnd = 0)
MsgBox "A_EventInfo=" (A_EventInfo = 0)
dc_args_func()
dc_args_func()
{
    MsgBox "A_ThisFunc=" (A_ThisFunc = "dc_args_func")
}

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

; TrayTip -> desktop notification (check_detail0821 §5-M5): must NOT raise,
; even headless with no notification daemon (documented "never a critical
; error").  With a daemon the Notify call is sent (VM-verified separately).
tt_ok := 1
try
    TrayTip("tray title", "tray body")
catch
    tt_ok := 0
MsgBox "traytip_noerr=" tt_ok
