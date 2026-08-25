# Format

- Linux status: `IMPL` (P1)
- Example kind: `verified`
- Environment profile: `headless`
- Verified source: [tests/doccheck/assert_string.ahk:37](../../tests/doccheck/assert_string.ahk#L37)
- Profile command: `bash tests/doccheck/run_check.sh "$BIN"`

Formats a variable number of input values according to a format string.

## Syntax

````text
String := Format(FormatStr , Values...)
````

## Linux-verified example excerpt

This excerpt is executed by the profile command above; surrounding setup and assertions remain in the linked source.

````ahk
MsgBox "StrSplit_first=" StrSplit("a,b,c", ",")[1]
MsgBox "StrSplit_multi=" StrSplit("a,b;c", [",", ";"]).Length
MsgBox "Format_simple=" Format("{1} {2}", "a", "b")
MsgBox "Format_reorder=" Format("{2} {1}", "a", "b")
MsgBox "Format_int=" Format("{1:02d}", 7)
MsgBox "Format_float=" Format("{1:.2f}", 3.14159)
````

## Upstream reference example

Source: [docs-v2/docs/lib/Format.htm](../../docs-v2/docs/lib/Format.htm)

````ahk
s := ""
; Simple substitution
s .= Format("{2}, {1}!`r`n", "World", "Hello")
; Padding with spaces
s .= Format("|{:-10}|`r`n|{:10}|`r`n", "Left", "Right")
; Hexadecimal
s .= Format("{1:#x} {2:X} 0x{3:x}`r`n", 3735928559, 195948557, 0)
; Floating-point
s .= Format("{1:0.3f} {1:.10f}",
````
