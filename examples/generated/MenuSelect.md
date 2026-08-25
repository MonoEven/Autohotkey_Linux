# MenuSelect

- Linux status: `IMPL` (P4)
- Example kind: `verified-error`
- Environment profile: `x11`
- Verified source: [tests/doccheck/assert_msg.ahk:135](../../tests/doccheck/assert_msg.ahk#L135)
- Profile command: `bash tests/doccheck/run_check.sh --xvfb "$BIN"`
- Linux adaptation: Targets a Win32 menu that does not exist: raises an explicit error

Invokes a menu item from the menu bar of the specified window.

## Syntax

````text
MenuSelect WinTitle, WinText, Menu, SubMenu1, SubMenu2, SubMenu3, SubMenu4, SubMenu5, SubMenu6, ExcludeTitle, ExcludeText
````

## Linux-verified example excerpt

This excerpt is executed by the profile command above; surrounding setup and assertions remain in the linked source.

````ahk
; could not be found, or does not have a standard Win32 menu." ---
try {
    MenuSelect("MsgMain", "", "File")
    Log("ms_nomenu=0")
} catch TargetError {
    Log("ms_nomenu=1")
````

## Upstream reference example

Source: [docs-v2/docs/lib/MenuSelect.htm](../../docs-v2/docs/lib/MenuSelect.htm)

````ahk
MenuSelect "Untitled - Notepad",, "File", "Open"
````
