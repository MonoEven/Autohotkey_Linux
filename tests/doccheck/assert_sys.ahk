; System module doc-check (v2 docs): settings (CoordMode/DetectHidden*/
; SetTitleMatchMode/Set*Delay/SendMode/SendLevel/SetRegView/FileEncoding/
; SetStoreCapsLockMode), ProcessGet*, FileCopy/FileMove/FileInstall/
; FileRecycle/FileRecycleEmpty/FileGetVersion, SysGet/SysGetIPAddresses,
; Download, Drive* and SoundPlay error behaviour.
; Scratch space lives on /tmp (ext4) because the DrvFS filesystem (/mnt/f)
; has a stale directory-entry cache that makes create-then-stat flaky.
#Requires AutoHotkey v2.0

; --- Isolate the XDG trash location used by FileRecycle. ---
EnvSet("HOME", "/tmp/ahk_dc_home_r5")
EnvSet("XDG_DATA_HOME", "/tmp/ahk_dc_data_r5")

; ============================================================================
; Settings module (docs: each returns the previous setting)
; ============================================================================

; Docs: "By default, coordinates are relative to the active window's client
; area", so the default CoordMode is Client.
MsgBox "CoordMode_prev=" (CoordMode("Mouse") = "Client")
CoordMode("Mouse", "Screen")
MsgBox "CoordMode_set=" (A_CoordModeMouse = "Screen")
MsgBox "CoordMode_return=" (CoordMode("Mouse", "Client") = "Screen")
; Docs: invalid TargetType / RelativeTo throw TypeError.
try
    CoordMode("Bogus")
catch ValueError
    MsgBox "CoordMode_badcmd=1"
try
    CoordMode("Mouse", "Bogus")
catch ValueError
    MsgBox "CoordMode_badmode=1"

; Docs: hidden window detection is disabled by default; DetectHiddenWindows
; returns the previous setting.
MsgBox "DetectHiddenWindows_prev=" (DetectHiddenWindows(1) = 0)
MsgBox "DetectHiddenWindows_set=" (A_DetectHiddenWindows = 1)
MsgBox "DetectHiddenWindows_return=" (DetectHiddenWindows(0) = 1)
; Docs: hidden text detection is enabled by default.
MsgBox "DetectHiddenText_prev=" (DetectHiddenText(0) = 1)
MsgBox "DetectHiddenText_set=" (A_DetectHiddenText = 0)
MsgBox "DetectHiddenText_return=" (DetectHiddenText(1) = 0)

; Docs: default match mode is 2; returns the previous value (1/2/3/"RegEx").
MsgBox "TitleMatchMode_prev=" (SetTitleMatchMode(3) = 2)
MsgBox "TitleMatchMode_return=" (SetTitleMatchMode("RegEx") = 3)
MsgBox "TitleMatchMode_var=" (A_TitleMatchMode = "RegEx")
; Docs: Fast/Slow change the search speed and return the previous speed
; (the default speed is fast, even while the match mode is "RegEx").
MsgBox "TitleMatchMode_slow=" (SetTitleMatchMode("Slow") = "Fast")
MsgBox "TitleMatchMode_speedvar=" (A_TitleMatchModeSpeed = "Slow")
MsgBox "TitleMatchMode_restore=" (SetTitleMatchMode("2") = "RegEx")
try
    SetTitleMatchMode("Bogus")
catch ValueError
    MsgBox "TitleMatchMode_bad=1"

; Docs: SetKeyDelay has no return value; affects A_KeyDelay/A_KeyDuration.
SetKeyDelay(10, 5)
MsgBox "KeyDelay_set=" (A_KeyDelay = 10 && A_KeyDuration = 5)
SetKeyDelay(20, 15, "Play")
MsgBox "KeyDelay_play=" (A_KeyDelayPlay = 20 && A_KeyDurationPlay = 15)
SetKeyDelay(-1, -1)
SetKeyDelay(-1, -1, "Play")
try
    SetKeyDelay(-2)
catch ValueError
    MsgBox "KeyDelay_bad=1"

; Docs: SetMouseDelay returns the previous delay (default 10).
MsgBox "MouseDelay_prev=" (SetMouseDelay(20) = 10)
MsgBox "MouseDelay_set=" (A_MouseDelay = 20)
mousedelay_play_prev := SetMouseDelay(30, "Play")
MsgBox "MouseDelay_play=" (mousedelay_play_prev = -1 && A_MouseDelayPlay = 30)
MsgBox "MouseDelay_return=" (SetMouseDelay(10) = 20)
SetMouseDelay(10)
SetMouseDelay(10, "Play")
try
    SetMouseDelay(-2)
catch ValueError
    MsgBox "MouseDelay_bad=1"

; Docs: SetWinDelay returns the previous delay (default 100).
MsgBox "WinDelay_prev=" (SetWinDelay(50) = 100)
MsgBox "WinDelay_set=" (A_WinDelay = 50)
MsgBox "WinDelay_return=" (SetWinDelay(100) = 50)
; Docs: SetControlDelay returns the previous delay (default 20).
MsgBox "ControlDelay_prev=" (SetControlDelay(30) = 20)
MsgBox "ControlDelay_set=" (A_ControlDelay = 30)
MsgBox "ControlDelay_return=" (SetControlDelay(20) = 30)

; Docs: SetDefaultMouseSpeed returns the previous speed (default 2).
MsgBox "DefaultMouseSpeed_prev=" (SetDefaultMouseSpeed(5) = 2)
MsgBox "DefaultMouseSpeed_set=" (A_DefaultMouseSpeed = 5)
MsgBox "DefaultMouseSpeed_return=" (SetDefaultMouseSpeed(2) = 5)

; Docs: "The default sending mode is Input"; SendMode returns the previous mode.
MsgBox "SendMode_prev=" (SendMode("Play") = "Input")
MsgBox "SendMode_set=" (A_SendMode = "Play")
MsgBox "SendMode_return=" (SendMode("InputThenPlay") = "Play")
MsgBox "SendMode_restore=" (SendMode("Input") = "InputThenPlay")
try
    SendMode("Bogus")
catch ValueError
    MsgBox "SendMode_bad=1"

; Docs: SendLevel returns the previous level; valid range 0-100.
MsgBox "SendLevel_prev=" (SendLevel(5) = 0)
MsgBox "SendLevel_set=" (A_SendLevel = 5)
MsgBox "SendLevel_return=" (SendLevel(0) = 5)
try
    SendLevel(101)
catch ValueError
    MsgBox "SendLevel_bad=1"

; Docs: SetRegView returns the previous setting ("Default"/"32"/"64").
MsgBox "RegView_prev=" (SetRegView("64") = "Default")
MsgBox "RegView_set=" (A_RegView = "64")
MsgBox "RegView_return=" (SetRegView("Default") = "64")
try
    SetRegView("Bogus")
catch ValueError
    MsgBox "RegView_bad=1"

; Docs: FileEncoding returns the previous setting ("CP0"/"UTF-8"/...).
MsgBox "FileEncoding_prev=" (FileEncoding("UTF-8") = "CP0")
MsgBox "FileEncoding_set=" (A_FileEncoding = "UTF-8")
MsgBox "FileEncoding_return=" (FileEncoding("UTF-8-RAW") = "UTF-8")
MsgBox "FileEncoding_restore=" (FileEncoding("CP0") = "UTF-8-RAW")
try
    FileEncoding("BOGUS")
catch ValueError
    MsgBox "FileEncoding_bad=1"
; Docs: the default encoding is used by FileRead/FileAppend.  Write UTF-16
; with the new default and read it back.
D := "/tmp/ahk_dc_sys"
DirDelete(D, 1)
DirCreate(D)
FileEncoding("UTF-16")
FileAppend("héllo", D "/enc.txt")
MsgBox "FileEncoding_utf16=" (FileRead(D "/enc.txt") = "héllo")
FileEncoding("CP0")

; Docs: SetStoreCapsLockMode returns the previous setting (default On).
MsgBox "StoreCapsLockMode_prev=" (SetStoreCapsLockMode(0) = 1)
MsgBox "StoreCapsLockMode_set=" (A_StoreCapsLockMode = 0)
MsgBox "StoreCapsLockMode_return=" (SetStoreCapsLockMode(1) = 0)

; ============================================================================
; Process module (docs: ProcessGetName/GetPath, TargetError if not found)
; ============================================================================

MsgBox "ProcName_self=" (ProcessGetName() = "ahk_core")
MsgBox "ProcPath_suffix=" (SubStr(ProcessGetPath(), -8) = "ahk_core")
pid := ProcessExist()
MsgBox "ProcName_bypid=" (ProcessGetName(pid) = "ahk_core")
MsgBox "ProcParent_alive=" (ProcessGetParent() > 0 && ProcessGetName(ProcessGetParent()) != "")
MsgBox "ProcName_byname=" (ProcessGetName("ahk_core") = "ahk_core")
; Docs: "The name is not case-sensitive".
MsgBox "ProcName_case=" (ProcessGetName("AHK_CORE") = "ahk_core")
MsgBox "ProcPath_byname=" (SubStr(ProcessGetPath("ahk_core"), -8) = "ahk_core")
try
    ProcessGetName("no-such-process-xyz-42")
catch TargetError
    MsgBox "ProcName_missing=TargetError"
try
    ProcessGetName(99999999)
catch TargetError
    MsgBox "ProcName_badpid=TargetError"
try
    ProcessGetParent("no-such-process-xyz-42")
catch TargetError
    MsgBox "ProcParent_missing=TargetError"

; ============================================================================
; SysGet / SysGetIPAddresses (docs)
; ============================================================================

; Display-independent metrics (identical with or without an X display):
MsgBox "SysGet_buttons=" (SysGet(43) = 3)      ; SM_CMOUSEBUTTONS
MsgBox "SysGet_mouse=" (SysGet(19) = 1)        ; SM_MOUSEPRESENT
MsgBox "SysGet_network=" (SysGet(63) = 1)      ; SM_NETWORK
MsgBox "SysGet_cleanboot=" (SysGet(67) = 0)    ; SM_CLEANBOOT
MsgBox "SysGet_caret=" (SysGet(0x2002) = 1)    ; SM_CARETBLINKINGENABLED
; Without a display, screen metrics are 0 (the X11-backed values are
; validated separately under Xvfb; see CHECK_REPORT.md).
MsgBox "SysGet_cxscreen=" (SysGet(0) = 0)
MsgBox "SysGet_screenw=" (A_ScreenWidth = 0)
MsgBox "SysGet_screenh=" (A_ScreenHeight = 0)
; Docs: an unsupported index returns 0 (no error).
MsgBox "SysGet_invalid=" (SysGet(9999) = 0)
; Docs: SysGetIPAddresses returns an Array of IPv4 strings.
ips := SysGetIPAddresses()
MsgBox "SysGetIP_type=" (Type(ips) = "Array")
MsgBox "SysGetIP_len=" (ips.Length >= 1)
has_loopback := 0
for ip in ips
    if ip = "127.0.0.1"
        has_loopback := 1
MsgBox "SysGetIP_loopback=" has_loopback

; ============================================================================
; File ops (scratch on /tmp; docs: FileCopy/FileMove throw an Error with
; Extra = number of failures; wildcard patterns that match nothing are ok)
; ============================================================================

D := "/tmp/ahk_dc_sys"
DirDelete(D, 1)
DirCreate(D)
FileAppend("hello", D "/src.txt")
FileCopy(D "/src.txt", D "/dst.txt")
MsgBox "Copy_basic=" (FileRead(D "/dst.txt") = "hello")
try {
    FileCopy(D "/src.txt", D "/dst.txt", 0)
    copy_overwrite := "noerr"
} catch Error as e {
    copy_overwrite := (e.Extra = 1) ? "err1" : "errN"
}
MsgBox "Copy_overwrite_err=" (copy_overwrite = "err1")
FileCopy(D "/src.txt", D "/dst.txt", 1)
MsgBox "Copy_overwrite_ok=" (FileRead(D "/dst.txt") = "hello")
DirCreate(D "/sub")
FileCopy(D "/*.txt", D "/sub")
MsgBox "Copy_wildcard=" (FileExist(D "/sub/dst.txt") ? 1 : 0) (FileExist(D "/sub/src.txt") ? 1 : 0)
; Docs: copying a wildcard pattern that matches nothing is a success.
try
    FileCopy(D "/*.nomatch", D "/x")
catch
    MsgBox "Copy_nomatch=thrown"
MsgBox "Copy_nomatch=1"
; Docs: without wildcards, a missing source is an error.
try
    FileCopy(D "/nope.txt", D "/nope2.txt")
catch
    MsgBox "Copy_missing=1"
; Copy into an existing directory keeps the source name.
DirCreate(D "/destdir")
FileCopy(D "/src.txt", D "/destdir")
MsgBox "Copy_destdir=" (FileRead(D "/destdir/src.txt") = "hello")

FileAppend("x", D "/m1.txt")
FileMove(D "/m1.txt", D "/m2.txt")
MsgBox "Move_basic=" (!FileExist(D "/m1.txt") && FileRead(D "/m2.txt") = "x")
FileAppend("y", D "/m3.txt")
try {
    FileMove(D "/m3.txt", D "/m2.txt")
    move_overwrite := "noerr"
} catch Error as e {
    move_overwrite := (e.Extra = 1) ? "err1" : "errN"
}
MsgBox "Move_overwrite_err=" (move_overwrite = "err1")
FileMove(D "/m3.txt", D "/m2.txt", 1)
MsgBox "Move_overwrite_ok=" (FileRead(D "/m2.txt") = "y")
FileAppend("a", D "/w1.txt")
FileAppend("b", D "/w2.txt")
FileMove(D "/w*.txt", D "/sub")
MsgBox "Move_wildcard=" (FileExist(D "/sub/w1.txt") ? 1 : 0) (FileExist(D "/sub/w2.txt") ? 1 : 0)

; Docs: FileInstall in an uncompiled script copies the file to the target.
FileInstall(D "/src.txt", D "/inst.txt")
MsgBox "Install_copy=" (FileRead(D "/inst.txt") = "hello")
try
    FileInstall(D "/src.txt", D "/inst.txt")
catch
    MsgBox "Install_nooverwrite=1"

; Docs: FileRecycle moves the file to the recycle bin (XDG Trash on Linux).
FileRecycle(D "/src.txt")
MsgBox "Recycle_ok=" (FileExist(D "/src.txt") ? 0 : 1) (FileExist("/tmp/ahk_dc_data_r5/Trash/files/src.txt") ? 1 : 0)
MsgBox "Recycle_info=" (FileExist("/tmp/ahk_dc_data_r5/Trash/info/src.txt.trashinfo") ? 1 : 0)
; Docs: FileRecycleEmpty empties the recycle bin (OSError on failure).
FileRecycleEmpty()
MsgBox "Recycle_empty=" (!FileExist("/tmp/ahk_dc_data_r5/Trash/files/src.txt"))
try
    FileRecycleEmpty()
catch OSError
    MsgBox "RecycleEmpty_err=1"

; Docs: FileGetVersion throws OSError if the file lacks version information
; (all Linux files lack a Windows version resource).
try
    FileGetVersion(D "/dst.txt")
catch OSError
    MsgBox "FileGetVersion_noversion=OSError"
try
    FileGetVersion(D "/missing.txt")
catch OSError
    MsgBox "FileGetVersion_missing=OSError"

; ============================================================================
; Download (docs: exception on failure; an HTTP error page is saved, not
; treated as failure).  A local python http.server on :18765 is started by
; run_check.sh and serves /tmp/ahk_dc_http/serve.txt.
; ============================================================================

try {
    Download("http://127.0.0.1:18765/serve.txt", "/tmp/ahk_dc_sys/dl.txt")
    dl_ok := "noerr"
} catch OSError {
    dl_ok := "OSError"
} catch {
    dl_ok := "other"
}
MsgBox "Download_ok=" (dl_ok = "noerr" && FileRead("/tmp/ahk_dc_sys/dl.txt") = "AHK_DC_DOWNLOAD")
; Docs: "The download might appear to succeed even when the remote file
; doesn't exist... the error page is what will be saved".
try {
    Download("http://127.0.0.1:18765/missing.txt", "/tmp/ahk_dc_sys/dl404.txt")
    dl_404 := "noerr"
} catch {
    dl_404 := "thrown"
}
MsgBox "Download_404=" (dl_404 = "noerr" && FileExist("/tmp/ahk_dc_sys/dl404.txt") && FileGetSize("/tmp/ahk_dc_sys/dl404.txt") > 0)
try
    Download("http://127.0.0.1:18765/serve.txt", "/tmp/no_such_dir_xyz/out.txt")
catch OSError
    MsgBox "Download_badpath=OSError"

; ============================================================================
; Drive* / SoundPlay (docs: exceptions on failure)
; ============================================================================

try
    DriveSetLabel("/nonexistent", "X")
catch OSError
    MsgBox "DriveSetLabel_err=OSError"
try
    DriveEject("/nonexistent")
catch OSError
    MsgBox "DriveEject_err=OSError"
try
    DriveLock("/nonexistent")
catch OSError
    MsgBox "DriveLock_err=OSError"
try
    DriveUnlock("/nonexistent")
catch OSError
    MsgBox "DriveUnlock_err=OSError"
try
    DriveRetract("/nonexistent")
catch OSError
    MsgBox "DriveRetract_err=OSError"
try
    SoundPlay("/nonexistent.wav")
catch OSError
    MsgBox "SoundPlay_err=OSError"

DirDelete(D, 1)
