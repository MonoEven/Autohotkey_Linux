# RegExMatch

- Linux status: `IMPL` (P1)
- Example kind: `verified`
- Environment profile: `headless`
- Verified source: [tests/doccheck/assert_general.ahk:90](../../tests/doccheck/assert_general.ahk#L90)
- Profile command: `bash tests/doccheck/run_check.sh "$BIN"`

## Additional verified environments

- `x11`: [tests/doccheck/assert_inputhook.ahk:63](../../tests/doccheck/assert_inputhook.ahk#L63)
- `headless`: [tests/doccheck/assert_regex.ahk:8](../../tests/doccheck/assert_regex.ahk#L8)

Determines whether a string contains a pattern (regular expression).

## Syntax

````text
FoundPos := RegExMatch(Haystack, NeedleRegEx , &OutputVar, StartingPos)
````

## Linux-verified example excerpt

This excerpt is executed by the profile command above; surrounding setup and assertions remain in the linked source.

````ahk
MsgBox "A_UserName_nn=" (A_UserName != "")
MsgBox "A_OSVersion_nn=" (A_OSVersion != "")
MsgBox "A_Language_hex=" (RegExMatch(A_Language, "^[0-9A-F]{4}$") = 1)
MsgBox "A_MyDocuments_nn=" (A_MyDocuments != "")
MsgBox "A_AhkPath_nn=" (A_AhkPath != "")
MsgBox "A_ScriptHwnd=" (A_ScriptHwnd = 0)
````

## Upstream reference example

Source: [docs-v2/docs/lib/RegExMatch.htm](../../docs-v2/docs/lib/RegExMatch.htm)

````ahk
MsgBox RegExMatch("xxxabc123xyz", "abc.*xyz")
````
