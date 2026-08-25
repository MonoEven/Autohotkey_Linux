# HotIf

- Linux status: `IMPL` (P1)
- Example kind: `verified`
- Environment profile: `x11`
- Verified source: [tests/doccheck/assert_hotkey_btn.ahk:64](../../tests/doccheck/assert_hotkey_btn.ahk#L64)
- Profile command: `bash tests/doccheck/run_check.sh --xvfb "$BIN"`

## Additional verified environments

- `x11`: [tests/doccheck/assert_hotkey_pt.ahk:60](../../tests/doccheck/assert_hotkey_pt.ahk#L60)
- `x11`: [tests/doccheck/assert_misc_cov.ahk:210](../../tests/doccheck/assert_misc_cov.ahk#L210)

Specifies the criteria for subsequently created or modified hotkey variants and hotstring variants.

## Syntax

````text
HotIf "Expression" HotIf Callback
````

## Linux-verified example excerpt

This excerpt is executed by the profile command above; surrounding setup and assertions remain in the linked source.

````ahk
    return false
}
HotIf(HotCritF)             ; condition false -> passthrough.
Hotkey("XButton2", CB6)
HotIf()                     ; reset.
Sleep(300)
````

## Upstream reference example

Source: [docs-v2/docs/lib/HotIf.htm](../../docs-v2/docs/lib/HotIf.htm)

````ahk
HotIfWinActive "ahk_class Notepad"
Hotkey "^!a", ShowMsgBox
Hotkey "#c", ShowMsgBox
Hotstring "::btw", "This replacement text will occur only in Notepad."
HotIfWinActive
Hotkey "#c", (*) => MsgBox("You pressed Win-C in a window other than Notepad.")
ShowMsgBox(HotkeyName)
{
    MsgBo
````
