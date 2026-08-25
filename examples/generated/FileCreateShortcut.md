# FileCreateShortcut

- Linux status: `IMPL` (P2)
- Example kind: `verified`
- Environment profile: `x11`
- Verified source: [tests/doccheck/assert_display.ahk:12](../../tests/doccheck/assert_display.ahk#L12)
- Profile command: `bash tests/doccheck/run_check.sh --xvfb "$BIN"`
- Linux adaptation: Uses .desktop/.url instead of .lnk (icon index / run state fields defaulted)

Creates a shortcut (.lnk) file.

## Syntax

````text
FileCreateShortcut Target, LinkFile , WorkingDir, Args, Description, IconFile, ShortcutKey, IconNumber, RunState
````

## Linux-verified example excerpt

This excerpt is executed by the profile command above; surrounding setup and assertions remain in the linked source.

````ahk

; --- FileCreateShortcut/FileGetShortcut: .desktop (Linux .lnk equivalent). ---
FileCreateShortcut("/usr/bin/gedit", "/tmp/ahk_dc_test.desktop", "/home/user", "--new-window", "Test editor", "/usr/share/icons/gedit.png")
FileGetShortcut("/tmp/ahk_dc_test.desktop", &tg, &wd, &ar, &de, &ic)
Log("desktop_target=" (tg = "/usr/bin/gedit" ? 1 : 0))
Log("desktop_dir=" (wd = "/home/user" ? 1 : 0))
````

## Upstream reference example

Source: [docs-v2/docs/lib/FileCreateShortcut.htm](../../docs-v2/docs/lib/FileCreateShortcut.htm)

````ahk
FileCreateShortcut "Notepad.exe", A_Desktop "\My Shortcut.lnk", "C:\", A_ScriptFullPath, "My Description", "C:\My Icon.ico", "i"
````
