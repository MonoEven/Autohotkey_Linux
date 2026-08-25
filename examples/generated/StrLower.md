# StrLower

- Linux status: `IMPL` (P1)
- Example kind: `verified`
- Environment profile: `x11`
- Verified source: [tests/doccheck/assert_inputhook.ahk:111](../../tests/doccheck/assert_inputhook.ahk#L111)
- Profile command: `bash tests/doccheck/run_check.sh --xvfb "$BIN"`

## Additional verified environments

- `headless`: [tests/doccheck/assert_string.ahk:27](../../tests/doccheck/assert_string.ahk#L27)

Converts a string to lowercase, uppercase or title case.

## Syntax

````text
NewString := StrLower(String) NewString := StrUpper(String) NewString := StrTitle(String)
````

## Linux-verified example excerpt

This excerpt is executed by the profile command above; surrounding setup and assertions remain in the linked source.

````ahk
ih3.Wait(2500)
Log("ih3_val=" (ih3.Input = "ab" ? 1 : 0))
Log("ih3_end=" (ih3.EndReason = "EndKey" && StrLower(ih3.EndKey) = "z" ? 1 : 0))
Sleep(50)
; E matches by produced character but still reports EndReason=EndKey; EndKey
; returns the actual character (official v2 contract).
````

## Upstream reference example

Source: [docs-v2/docs/lib/StrLower.htm](../../docs-v2/docs/lib/StrLower.htm)

````ahk
String1 := "This is a test."
String1 := StrLower(String1)  ; i.e. output can be the same as input.
````
