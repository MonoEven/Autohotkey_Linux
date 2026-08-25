# TrayTip

- Linux status: `IMPL` (P2)
- Example kind: `verified`
- Environment profile: `headless`
- Verified source: [tests/doccheck/assert_general.ahk:118](../../tests/doccheck/assert_general.ahk#L118)
- Profile command: `bash tests/doccheck/run_check.sh "$BIN"`
- Linux adaptation: Delivered as an org.freedesktop.Notifications notification (per-desktop daemon); empty TrayTip is a no-op

Shows a balloon message window or, on Windows 10 and later, a toast notification near the tray icon.

## Syntax

````text
TrayTip Text, Title, Options
````

## Linux-verified example excerpt

This excerpt is executed by the profile command above; surrounding setup and assertions remain in the linked source.

````ahk
tt_ok := 1
try
    TrayTip("tray title", "tray body")
catch
    tt_ok := 0
MsgBox "traytip_noerr=" tt_ok
````

## Upstream reference example

Source: [docs-v2/docs/lib/TrayTip.htm](../../docs-v2/docs/lib/TrayTip.htm)

````ahk
TrayTip "Multiline`nText", "My Title", "Iconi Mute"
````
