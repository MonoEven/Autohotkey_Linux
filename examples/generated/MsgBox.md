# MsgBox

- Linux status: `IMPL` (P1)
- Example kind: `verified`
- Environment profile: `x11`
- Verified source: [tests/doccheck/assert_display.ahk:9](../../tests/doccheck/assert_display.ahk#L9)
- Profile command: `bash tests/doccheck/run_check.sh --xvfb "$BIN"`

## Additional verified environments

- `headless`: [tests/doccheck/assert_general.ahk:25](../../tests/doccheck/assert_general.ahk#L25)

Displays the specified text in a small window containing one or more buttons (such as Yes and No).

## Syntax

````text
MsgBox Text, Title, Options Result := MsgBox(Text, Title, Options)
````

## Linux-verified example excerpt

This excerpt is executed by the profile command above; surrounding setup and assertions remain in the linked source.

````ahk

; Headless MsgBox prints to stdout, which run_check.sh captures.
Log(line) => MsgBox(line)

; --- FileCreateShortcut/FileGetShortcut: .desktop (Linux .lnk equivalent). ---
FileCreateShortcut("/usr/bin/gedit", "/tmp/ahk_dc_test.desktop", "/home/user", "--new-window", "Test editor", "/usr/share/icons/gedit.png")
````

## Upstream reference example

Source: [docs-v2/docs/lib/MsgBox.htm](../../docs-v2/docs/lib/MsgBox.htm)

````ahk
MsgBox "This is a string."
````
