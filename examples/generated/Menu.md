# Menu

- Linux status: `IMPL` (P1)
- Example kind: `verified`
- Environment profile: `x11`
- Verified source: [tests/doccheck/assert_misc_cov.ahk:66](../../tests/doccheck/assert_misc_cov.ahk#L66)
- Profile command: `bash tests/doccheck/run_check.sh --xvfb "$BIN"`

Provides an interface to create and modify a menu or menu bar, add and modify menu items, and retrieve information about the menu or menu bar.

## Syntax

````text
MyMenu := Menu() MyMenuBar := MenuBar() MyMenu := Menu.Call() MyMenuBar := MenuBar.Call()
````

## Linux-verified example excerpt

This excerpt is executed by the profile command above; surrounding setup and assertions remain in the linked source.

````ahk
Check("str_is", () => "abc" is String)
Check("class_is", () => Float is Class)
Check("menu_type", () => Type(Menu()))
Check("menu_is", () => Menu() is Menu)
class BindClsM {
    m() {
````

## Upstream reference example

Source: [docs-v2/docs/lib/Menu.htm](../../docs-v2/docs/lib/Menu.htm)

````ahk
A_TrayMenu.Add()  ; Creates a separator line.
A_TrayMenu.Add("Item1", MenuHandler)  ; Creates a new menu item.
Persistent
MenuHandler(ItemName, ItemPos, MyMenu) {
    MsgBox "You selected " ItemName " (position " ItemPos ")"
}
````
