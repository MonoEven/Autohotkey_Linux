; Registry module doc-check (v2 docs: RegRead/RegWrite/RegDelete/
; RegDeleteKey/RegCreateKey).  The Linux port stores a virtual registry in
; $XDG_CONFIG_HOME/autohotkey-registry.txt (see CHECK_REPORT.md).
; The store location is isolated here via XDG_CONFIG_HOME.
#Requires AutoHotkey v2.0

EnvSet("XDG_CONFIG_HOME", "/tmp/ahk_dc_reg")
EnvSet("HOME", "/tmp/ahk_dc_home")

; RegWrite(Value, ValueType, KeyName, ValueName) returns bytes written.
n := RegWrite("hello", "REG_SZ", "HKCU\Software\DocCheck", "Str")
MsgBox "Write_sz=" (n > 0)
MsgBox "Read_sz=" (RegRead("HKCU\Software\DocCheck", "Str") = "hello")
; Docs: REG_DWORD is read as a positive decimal integer.
RegWrite(42, "REG_DWORD", "HKCU\Software\DocCheck", "Num")
MsgBox "Read_dword=" (RegRead("HKCU\Software\DocCheck", "Num") = 42)
RegWrite(0xFFFFFFFF, "REG_DWORD", "HKCU\Software\DocCheck", "Big")
MsgBox "Read_dword_big=" (RegRead("HKCU\Software\DocCheck", "Big") = 4294967295)
; Docs: REG_BINARY is read as a string of hex characters.
RegWrite("01a9ff77", "REG_BINARY", "HKCU\Software\DocCheck", "Blob")
MsgBox "Read_binary=" (RegRead("HKCU\Software\DocCheck", "Blob") = "01A9FF77")
; Docs: REG_MULTI_SZ components end in a linefeed when read.
RegWrite("a`nb", "REG_MULTI_SZ", "HKCU\Software\DocCheck", "Multi")
MsgBox "Read_multi=" (RegRead("HKCU\Software\DocCheck", "Multi") = "a`nb")
RegWrite("expanded", "REG_EXPAND_SZ", "HKCU\Software\DocCheck", "Exp")
MsgBox "Read_exp=" (RegRead("HKCU\Software\DocCheck", "Exp") = "expanded")
; Docs: omitted ValueName reads the key's default value.
RegWrite("defval", "REG_SZ", "HKLM\Software\DocCheck")
MsgBox "Read_def=" (RegRead("HKLM\Software\DocCheck") = "defval")
; Docs: root may be the short form (HKLM == HKEY_LOCAL_MACHINE).
MsgBox "Read_short=" (RegRead("HKEY_LOCAL_MACHINE\Software\DocCheck") = "defval")
; Docs: Default is returned when the value does not exist.
MsgBox "Read_missing_dflt=" (RegRead("HKCU\Software\DocCheck", "Nope", "fallback") = "fallback")
; Docs: OSError when the value/key does not exist and no Default is given.
try
    RegRead("HKCU\Software\DocCheck", "Nope")
catch
    MsgBox "Read_missing_err=1"
try
    RegRead("HKCU\Software\NoSuchKey")
catch
    MsgBox "Read_missing_key_err=1"
; RegCreateKey
RegCreateKey("HKCU\Software\Created")
MsgBox "Create=" (RegRead("HKCU\Software\Created", "X", "created-ok") = "created-ok")
; RegDelete deletes the named value; deleting a missing value throws.
RegWrite("bye", "REG_SZ", "HKCU\Software\DocCheck", "Temp")
RegDelete("HKCU\Software\DocCheck", "Temp")
try
    RegRead("HKCU\Software\DocCheck", "Temp")
catch
    MsgBox "Delete_value_err=1"
try
    RegDelete("HKCU\Software\DocCheck", "Temp")
catch
    MsgBox "Delete_missing_err=1"
; RegDeleteKey removes the key including subkeys and values.
RegWrite("v", "REG_SZ", "HKCU\Software\DelKey\Sub", "K")
RegDeleteKey("HKCU\Software\DelKey")
try
    RegRead("HKCU\Software\DelKey\Sub", "K")
catch
    MsgBox "DeleteKey_sub_err=1"
; Invalid root key and invalid value type throw.
try
    RegRead("NOPE\Key")
catch
    MsgBox "Bad_root_err=1"
try
    RegWrite("x", "REG_FOO", "HKCU\Software\DocCheck", "Bad")
catch
    MsgBox "Bad_type_err=1"
; REG_BINARY accepts a Buffer.
b := Buffer(2)
NumPut("UChar", 0xAB, b, 0)
NumPut("UChar", 0xCD, b, 1)
RegWrite(b, "REG_BINARY", "HKCU\Software\DocCheck", "Buf")
MsgBox "Read_buf=" (RegRead("HKCU\Software\DocCheck", "Buf") = "ABCD")
