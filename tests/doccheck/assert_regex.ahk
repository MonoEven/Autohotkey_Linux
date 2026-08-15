; RegExMatch/RegExReplace doc-check (v2 docs).
; Docs: RegExMatch(Haystack, NeedleRegEx, &OutputVar, StartingPos)
;       RegExReplace(Haystack, NeedleRegEx, Replacement, &OutputVarCount, Limit, StartingPos)
;       Patterns use PCRE syntax; options like "i)" prefix the pattern.
#Requires AutoHotkey v2.0

; Return value: position of the match (1-based) or 0.
MsgBox "pos=" (RegExMatch("abc123", "\d+") = 4)
MsgBox "no=" (RegExMatch("abc", "xyz") = 0)
MsgBox "at_start=" (RegExMatch("123abc", "\d+") = 1)
MsgBox "case_sens=" (RegExMatch("ABC", "abc") = 0)
MsgBox "case_ins=" (RegExMatch("ABC", "i)abc") = 1)
MsgBox "tilde=" ("abc123" ~= "\d+")

; RegExReplace: default replacement is "".
MsgBox "rep=" (RegExReplace("abc123", "\d+", "X") = "abcX")
MsgBox "rep_none=" (RegExReplace("abc", "xyz", "X") = "abc")
MsgBox "rep_ref=" (RegExReplace("abc", "(b)", "$1$1") = "abbc")
MsgBox "rep_case=" (RegExReplace("ABC", "i)abc", "x") = "x")
MsgBox "rep_limit=" (RegExReplace("a1b2c3", "\d", "X", , 2) = "aXbXc3")

; Match object: &OutputVar receives a RegExMatchInfo object.
m := ""
MsgBox "match_pos=" (RegExMatch("test123", "(\d+)", &m) = 5)
MsgBox "match_val=" (m[1] = "123")
MsgBox "match_full=" (m[0] = "123")
MsgBox "match_count=" (m.Count = 1)

; Named subpatterns.
n := ""
RegExMatch("key=value", "(?<k>\w+)=(?<v>\w+)", &n)
MsgBox "named_k=" (n["k"] = "key")
MsgBox "named_v=" (n["v"] = "value")

; RegExReplace count output var.
cnt := 0
MsgBox "rep_cnt=" (RegExReplace("a1b2", "\d", "X", &cnt) = "aXbX")
MsgBox "cnt=" (cnt = 2)

; Non-ASCII (UTF) matching.
MsgBox "utf8=" (RegExMatch("héllo", "é") = 2)
MsgBox "utf8_pos=" (RegExMatch("héllo", "llo") = 3)

; Invalid pattern throws.
try
    RegExMatch("abc", "[")
catch
    MsgBox "bad_pattern_err=1"
