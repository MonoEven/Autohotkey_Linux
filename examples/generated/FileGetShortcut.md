# FileGetShortcut

- Linux status: `IMPL` (P2)
- Example kind: `verified`
- Environment profile: `x11`
- Verified source: [tests/doccheck/assert_display.ahk:13](../../tests/doccheck/assert_display.ahk#L13)
- Profile command: `bash tests/doccheck/run_check.sh --xvfb "$BIN"`
- Linux adaptation: Reads .desktop/.url instead of .lnk (documented field semantics)

Retrieves information about a shortcut (.lnk) file, such as its target file.

## Syntax

````text
FileGetShortcut LinkFile , &OutTarget, &OutDir, &OutArgs, &OutDescription, &OutIcon, &OutIconNum, &OutRunState
````

## Linux-verified example excerpt

This excerpt is executed by the profile command above; surrounding setup and assertions remain in the linked source.

````ahk
; --- FileCreateShortcut/FileGetShortcut: .desktop (Linux .lnk equivalent). ---
FileCreateShortcut("/usr/bin/gedit", "/tmp/ahk_dc_test.desktop", "/home/user", "--new-window", "Test editor", "/usr/share/icons/gedit.png")
FileGetShortcut("/tmp/ahk_dc_test.desktop", &tg, &wd, &ar, &de, &ic)
Log("desktop_target=" (tg = "/usr/bin/gedit" ? 1 : 0))
Log("desktop_dir=" (wd = "/home/user" ? 1 : 0))
Log("desktop_args=" (ar = "--new-window" ? 1 : 0))
````

## Upstream reference example

Source: [docs-v2/docs/lib/FileGetShortcut.htm](../../docs-v2/docs/lib/FileGetShortcut.htm)

````ahk
LinkFile := FileSelect(32,, "Pick a shortcut to analyze.", "Shortcuts (*.lnk)")
if LinkFile = ""
    return
FileGetShortcut LinkFile, &OutTarget, &OutDir, &OutArgs, &OutDesc, &OutIcon, &OutIconNum, &OutRunState
MsgBox OutTarget "`n" OutDir "`n" OutArgs "`n" OutDesc "`n" OutIcon "`n" OutIconN
````
