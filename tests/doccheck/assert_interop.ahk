; NumGet/NumPut/StrGet/StrPut + Buffer doc-check (v2 docs).
; Docs: NumPut Type, Number, [Type2, Number2, ...] Target [, Offset]
;       NumGet Source [, Offset], Type  (2-parameter mode: Source, Type)
#Requires AutoHotkey v2.0

b := Buffer(32)
NumPut("Int", 42, b, 0)
MsgBox "NumGet_int=" (NumGet(b, 0, "Int") = 42)
MsgBox "NumGet_2param=" (NumGet(b, "Int") = 42)
NumPut("Char", -7, b, 0)
MsgBox "NumGet_char=" (NumGet(b, 0, "Char") = -7)
NumPut("Double", 3.14, b, 8)
MsgBox "NumGet_double=" (NumGet(b, 8, "Double") = 3.14)
NumPut("UShort", 65535, b, 16)
MsgBox "NumGet_ushort=" (NumGet(b, 16, "UShort") = 65535)
NumPut("Int64", 0x1122334455667788, b, 0)
MsgBox "NumGet_i64=" (NumGet(b, 0, "Int64") = 0x1122334455667788)
MsgBox "NumGet_addr=" (NumGet(b.Ptr, 0, "Int64") = 0x1122334455667788)
; Docs: reading/writing past the buffer's Size throws.
try
    NumGet(b, 100, "Int")
catch
    MsgBox "NumGet_oob_err=1"
try
    NumPut("Int", 1, b, 100)
catch
    MsgBox "NumPut_oob_err=1"
; Multi-pair form.
NumPut("UShort", 7, "UChar", 9, b)
MsgBox "Multi_ushort=" (NumGet(b, 0, "UShort") = 7)
MsgBox "Multi_uchar=" (NumGet(b, 2, "UChar") = 9)

; StrPut/StrGet: UTF-8 and UTF-16 round trips.
StrPut("hi", b, "UTF-8")
MsgBox "StrGet_utf8=" (StrGet(b, 2, "UTF-8") = "hi")
MsgBox "StrPut_reqsize=" (StrPut("hello", "UTF-8") = 6)
MsgBox "StrPut_utf16_n=" (StrPut("hi", b, "UTF-16") = 6)
MsgBox "StrGet_utf16=" (StrGet(b, 2, "UTF-16") = "hi")
MsgBox "StrGet_addr=" (StrGet(b.Ptr, 2, "UTF-16") = "hi")
; Docs: Length is a character count (multi-byte encodings included).
n := StrPut("你好", b, "UTF-8")
MsgBox "cn_n=" (n = 7)
MsgBox "cn_get=" (StrGet(b, 2, "UTF-8") = "你好")
n2 := StrPut("😀", b, "UTF-8")
MsgBox "emoji_n=" (n2 = 5)
MsgBox "emoji_get=" (StrGet(b, 1, "UTF-8") = "😀")
