# TraySetIcon

- Linux status: `IMPL` (P2)
- Example kind: `verified`
- Environment profile: `headless`
- Verified source: [tests/doccheck/assert_general.ahk:128](../../tests/doccheck/assert_general.ahk#L128)
- Profile command: `bash tests/doccheck/run_check.sh "$BIN"`
- Linux adaptation: Delivered as a StatusNotifierItem (org.kde.StatusNotifierItem + com.canonical.dbusmenu); the menu is the script's A_TrayMenu (customizable) with default Pause/Suspend/Reload/Exit when empty; an image-file path is exposed as both IconName and IconPixmap; no watcher is a silent no-op

Changes the script's tray icon (which is also used by GUI and dialog windows).

## Syntax

````text
TraySetIcon FileName, IconNumber, Freeze
````

## Linux-verified example excerpt

This excerpt is executed by the profile command above; surrounding setup and assertions remain in the linked source.

````ahk
ts_ok := 1
try
    TraySetIcon("application-x-executable")
catch
    ts_ok := 0
MsgBox "trayseticon_noerr=" ts_ok
````

## Upstream reference example

Source: [docs-v2/docs/lib/TraySetIcon.htm](../../docs-v2/docs/lib/TraySetIcon.htm)

The upstream page has no standalone code block; use the Linux-verified excerpt above.
