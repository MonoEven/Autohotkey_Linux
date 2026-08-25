# StrReplace

- Linux status: `IMPL` (P1)
- Example kind: `verified`
- Environment profile: `headless`
- Verified source: [tests/doccheck/assert_string.ahk:17](../../tests/doccheck/assert_string.ahk#L17)
- Profile command: `bash tests/doccheck/run_check.sh "$BIN"`

Replaces the specified substring with a new string.

## Syntax

````text
ReplacedStr := StrReplace(Haystack, Needle , ReplaceText, CaseSense, &OutputVarCount, Limit)
````

## Linux-verified example excerpt

This excerpt is executed by the profile command above; surrounding setup and assertions remain in the linked source.

````ahk
MsgBox "InStr_nth=" InStr("abcabc", "b", , , 2) ; Occurrence is param #5
MsgBox "InStr_missing=" InStr("abc", "d")
MsgBox "StrReplace_all=" StrReplace("abcabc", "b", "X")
cnt := 0
MsgBox "StrReplace_limit=" StrReplace("aaa", "a", "b", , &cnt, 2)
MsgBox "StrReplace_count=" cnt
````

## Upstream reference example

Source: [docs-v2/docs/lib/StrReplace.htm](../../docs-v2/docs/lib/StrReplace.htm)

````ahk
A_Clipboard := StrReplace(A_Clipboard, "`r`n")
````
