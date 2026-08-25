# Hotstring

- Linux status: `IMPL` (P1)
- Example kind: `verified`
- Environment profile: `x11`
- Verified source: [tests/doccheck/assert_hotstring.ahk:23](../../tests/doccheck/assert_hotstring.ahk#L23)
- Profile command: `bash tests/doccheck/run_check.sh --xvfb "$BIN"`

## Additional verified environments

- `x11`: [tests/doccheck/assert_msg.ahk:150](../../tests/doccheck/assert_msg.ahk#L150)

Creates, modifies, enables, or disables a hotstring while the script is running.

## Syntax

````text
Hotstring String , Replacement, OnOffToggle Hotstring NewOptions Hotstring SubFunction , Value1
````

## Linux-verified example excerpt

This excerpt is executed by the profile command above; surrounding setup and assertions remain in the linked source.

````ahk
Sleep(300)

Hotstring("::xq", "ZZ")     ; end char required.
Hotstring(":O:cd", "Q")     ; omit the end char.
Hotstring(":X:ef", CB)      ; callback form.
Hotstring(":*:ab", "W")     ; no end char required.
````

## Upstream reference example

Source: [docs-v2/docs/lib/Hotstring.htm](../../docs-v2/docs/lib/Hotstring.htm)

````ahk
#h::  ; Win+H hotkey
{
    ; Get the text currently selected. The clipboard is used instead of
    ; EditGetSelectedText because it works in a greater variety of editors
    ; (namely word processors). Save the current clipboard contents to be
    ; restored later. Although this handles on
````
