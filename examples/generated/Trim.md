# Trim

- Linux status: `IMPL` (P1)
- Example kind: `verified`
- Environment profile: `headless`
- Verified source: [tests/doccheck/assert_string.ahk:23](../../tests/doccheck/assert_string.ahk#L23)
- Profile command: `bash tests/doccheck/run_check.sh "$BIN"`

Trims characters from the beginning and/or end of a string.

## Syntax

````text
NewString := Trim(String , OmitChars) NewString := LTrim(String , OmitChars) NewString := RTrim(String , OmitChars)
````

## Linux-verified example excerpt

This excerpt is executed by the profile command above; surrounding setup and assertions remain in the linked source.

````ahk
MsgBox "StrReplace_empty=" StrReplace("aaa", "a", "")
MsgBox "StrReplace_none=" StrReplace("abc", "z", "X")
MsgBox "Trim=" Trim("  x  ")
MsgBox "LTrim=" LTrim("  x  ")
MsgBox "RTrim=" RTrim("  x  ")
MsgBox "StrUpper=" StrUpper("abc")
````

## Upstream reference example

Source: [docs-v2/docs/lib/Trim.htm](../../docs-v2/docs/lib/Trim.htm)

````ahk
text := "  text  "
MsgBox
(
    "No trim:`t'" text "'
    Trim:`t'" Trim(text) "'
    LTrim:`t'" LTrim(text) "'
    RTrim:`t'" RTrim(text) "'"
)
````
