# StrSplit

- Linux status: `IMPL` (P1)
- Example kind: `verified`
- Environment profile: `x11`
- Verified source: [tests/doccheck/assert_clipboard_change.ahk:42](../../tests/doccheck/assert_clipboard_change.ahk#L42)
- Profile command: `bash tests/doccheck/run_check.sh --xvfb "$BIN"`

## Additional verified environments

- `x11`: [tests/doccheck/assert_ctrl.ahk:30](../../tests/doccheck/assert_ctrl.ahk#L30)
- `x11`: [tests/doccheck/assert_input.ahk:28](../../tests/doccheck/assert_input.ahk#L28)
- `x11`: [tests/doccheck/assert_layout.ahk:24](../../tests/doccheck/assert_layout.ahk#L24)
- `x11`: [tests/doccheck/assert_repeat.ahk:31](../../tests/doccheck/assert_repeat.ahk#L31)
- `headless`: [tests/doccheck/assert_string.ahk:34](../../tests/doccheck/assert_string.ahk#L34)

Separates a string into an array of substrings using the specified delimiters.

## Syntax

````text
Array := StrSplit(String , Delimiters, OmitChars, MaxParts)
````

## Linux-verified example excerpt

This excerpt is executed by the profile command above; surrounding setup and assertions remain in the linked source.

````ahk

; Tally the recorded callbacks.
For line in StrSplit(FileRead(CBFILE), "`n")
{
    if (line = "cb-1")
        count1++
````

## Upstream reference example

Source: [docs-v2/docs/lib/StrSplit.htm](../../docs-v2/docs/lib/StrSplit.htm)

````ahk
TestString := "This is a test."
word_array := StrSplit(TestString, A_Space, ".")  ; Omits periods.
MsgBox "The 4th word is " word_array[4]
````
