# WinActivateBottom

- Linux status: `IMPL` (P1)
- Example kind: `verified`
- Environment profile: `x11`
- Verified source: [tests/doccheck/assert_misc_cov.ahk:238](../../tests/doccheck/assert_misc_cov.ahk#L238)
- Profile command: `bash tests/doccheck/run_check.sh --xvfb "$BIN"`

Same as WinActivate except that it activates the bottommost matching window rather than the topmost.

## Syntax

````text
WinActivateBottom WinTitle, WinText, ExcludeTitle, ExcludeText
````

## Linux-verified example excerpt

This excerpt is executed by the profile command above; surrounding setup and assertions remain in the linked source.

````ahk
WinWait("MiscCov Beta",, 5)
Sleep(100)
Check("winab_title", () => (WinActivateBottom("MiscCov"), 1))
idA := WinExist("MiscCov Alpha")
Check("winab_id", () => (WinActivateBottom("ahk_id " idA), 1))
GroupDeact() {
````

## Upstream reference example

Source: [docs-v2/docs/lib/WinActivateBottom.htm](../../docs-v2/docs/lib/WinActivateBottom.htm)

````ahk
#i:: ; Win+I
{
    SetTitleMatchMode 2
    WinActivateBottom "- Microsoft Internet Explorer"
}
````
